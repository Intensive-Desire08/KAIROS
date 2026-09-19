'use strict';

/**
 * Dynamic Threat Response Engine (DTRE) — core decision logic.
 * See Technical Specification — Module 3, Section 6.3 / 6.4 / 6.6.
 *
 * `evaluate(threatData, sessionContext)` takes the ML Service's threat
 * assessment plus Gateway session context and returns an ordered array
 * of action descriptors for the Gateway to execute (Section 6.3 Output
 * Schema). This module is pure decision logic — it does NOT execute
 * actions itself (Section 6.1: "Decide -> DTRE determines appropriate
 * actions" is separate from "Execute -> DTRE calls Gateway modules").
 *
 * Decision priority (Section 6.4):
 *   1. threat_score > full_reset_threshold (0.85) AND level === "red"
 *      -> fullSessionReset (overrides everything else)
 *   2. level-based actions (green/yellow/orange/red)
 *   3. attack-specific actions (only if attack_label != null AND
 *      threat_score > activation_threshold)
 *   4. confidence-gated RED override (Section 4.6 / 6.4, stretch v1.1):
 *      when level === red and threat_score > 0.75, this both ADDS
 *      block_ip (if confidence is sufficient) and REMOVES it (if
 *      confidence is too low), independent of whether an attack_label
 *      already contributed it in step 3. See applyConfidenceGate() below.
 */

// NOTE: require the whole module (not a destructured `const { getPolicy } =
// require(...)`) and call `thresholds.getX()` at call time. Destructuring at
// module-load time would bind these to the specific function objects that
// existed when engine.js first loaded, which breaks `jest.spyOn(thresholds,
// 'getConfidenceConfig')` in tests (the spy replaces the property on the
// module's exports object, but a destructured reference captured earlier
// wouldn't see it) and would also ignore any later thresholds.reload().
const thresholds = require('./thresholds');

const LEVEL_ACTION_KEYS = {
  green: null, // no actions
  yellow: 'yellow_actions',
  orange: 'orange_actions',
  red: 'red_actions',
};

const LABEL_ACTION_KEYS = {
  DOS: 'dos_actions',
  BRUTE_FORCE: 'bruteforce_actions',
  PORT_SCAN: 'portscan_actions',
};

const KEY_LIFETIME_SECONDS = {
  green: 300,
  yellow: 300,
  orange: 60,
  red: 30,
};

/**
 * Build a single action descriptor matching the Section 6.3 Output Schema.
 */
function makeAction(type, sessionContext, extra = {}) {
  return {
    type,
    sessionId: sessionContext.sessionId,
    severity: extra.severity || 'medium',
    reason: extra.reason || 'threat_detected',
    ...extra,
  };
}

const LEVEL_DESCRIPTIONS = {
  yellow: 'Low-level anomaly',
  orange: 'Moderate threat detected',
  red: 'Severe threat detected',
};

/**
 * Build a human-readable admin alert message, e.g.
 * "Moderate threat detected: DOS attack suspected (score 0.78)" —
 * matching the style of the spec's own example (Section 6.5).
 */
function buildAlertMessage(threatData) {
  const base = LEVEL_DESCRIPTIONS[threatData.level] || 'Threat detected';
  const suffix = threatData.attack_label
    ? `: ${threatData.attack_label} attack suspected`
    : '';
  const score =
    typeof threatData.threat_score === 'number' ? ` (score ${threatData.threat_score.toFixed(2)})` : '';
  return `${base}${suffix}${score}`;
}

/**
 * Expand a policy action-name string (e.g. "rotate_key") into a concrete
 * action descriptor. Some action names need extra parameters drawn from
 * context (e.g. rate_limit needs deviceId; block_ip needs duration).
 */
function expandAction(actionName, threatData, sessionContext, { severity, reason }) {
  switch (actionName) {
    case 'rotate_key':
      return makeAction('rotate_key', sessionContext, { severity, reason });
    case 'reduce_lifetime':
      return makeAction('reduce_lifetime', sessionContext, {
        severity,
        reason,
        lifetimeSeconds: KEY_LIFETIME_SECONDS[threatData.level] ?? 300,
      });
    case 'rate_limit':
      return makeAction('rate_limit', sessionContext, {
        severity,
        reason,
        deviceId: sessionContext.sourceDevice,
        limit: 100,
      });
    case 'force_re_auth':
      return makeAction('force_re_auth', sessionContext, { severity, reason });
    case 'alert_admin':
      return makeAction('alert_admin', sessionContext, {
        severity,
        reason,
        urgent: false,
        message: buildAlertMessage(threatData),
        threatScore: threatData.threat_score,
      });
    case 'alert_admin_urgent':
      return makeAction('alert_admin', sessionContext, {
        severity,
        reason,
        urgent: true,
        message: buildAlertMessage(threatData),
        threatScore: threatData.threat_score,
      });
    case 'verbose_logging':
      return makeAction('verbose_logging', sessionContext, { severity, reason });
    case 'block_ip':
      return makeAction('block_ip', sessionContext, {
        severity,
        reason,
        ip: sessionContext.sourceIP,
      });
    case 'lock_account':
      return makeAction('lock_account', sessionContext, {
        severity,
        reason,
        deviceId: sessionContext.sourceDevice,
      });
    default:
      return makeAction(actionName, sessionContext, { severity, reason });
  }
}

/**
 * Level-based actions (Section 6.4 "Level-Based Actions (Primary)").
 * GREEN returns no actions.
 */
function evaluateLevelActions(threatData, sessionContext, policy) {
  const key = LEVEL_ACTION_KEYS[threatData.level];
  if (!key) return [];
  const actionNames = policy[key] || [];
  const severity = threatData.level === 'red' ? 'high' : threatData.level === 'orange' ? 'medium' : 'low';
  return actionNames.map((name) =>
    expandAction(name, threatData, sessionContext, { severity, reason: `${threatData.level}_level` })
  );
}

/**
 * Attack-specific actions (Section 6.4 "Attack-Specific Actions (Conditional)").
 * Only triggered when attack_label is set AND threat_score exceeds the
 * activation threshold (default 0.5) — this activation check is expected
 * to have already been applied upstream by the ML Service (Section 4.5),
 * but we re-check here defensively since the DTRE must not assume the
 * ML Service enforced it correctly (fail-safe, Section 10.1).
 */
function evaluateAttackSpecificActions(threatData, sessionContext, policy, activationThreshold) {
  if (!threatData.attack_label) return [];
  if (threatData.threat_score <= activationThreshold) return [];

  const key = LABEL_ACTION_KEYS[threatData.attack_label];
  if (!key) return [];

  const actionNames = policy[key] || [];
  const reason = `${threatData.attack_label.toLowerCase()}_detected`;
  return actionNames.map((name) =>
    expandAction(name, threatData, sessionContext, { severity: 'high', reason })
  );
}

/**
 * Threat score override — "nuclear option" (Section 6.4).
 * threat_score > full_reset_threshold (0.85) AND level === "red"
 * overrides ALL other actions.
 */
function evaluateFullResetOverride(threatData, sessionContext, fullResetThreshold) {
  if (threatData.level === 'red' && threatData.threat_score > fullResetThreshold) {
    return [
      makeAction('full_session_reset', sessionContext, {
        severity: 'critical',
        reason: 'severe_threat_detected',
      }),
    ];
  }
  return null;
}

/**
 * Confidence-gated RED override (Section 4.6 / 6.4, stretch v1.1).
 *
 * Spec pseudocode (Section 6.4):
 *   if (level === "red" && threat_score > 0.75) {
 *     if (confidence < 0.2) {
 *       return [rotateKey, rateLimit, reAuth, alertUrgent]        // no blockIP
 *     } else {
 *       return [rotateKey, rateLimit, reAuth, alertUrgent, blockIP] // WITH blockIP
 *     }
 *   }
 *
 * NOTE: this is deliberately BOTH subtractive and additive. It's not just
 * "remove block_ip if it's already there from an attack-specific rule" —
 * a high-confidence RED-level anomaly gets blockIP added even with NO
 * attack_label at all (the pseudocode's "else" branch adds it
 * unconditionally). An earlier version of this function only ever
 * removed block_ip, which meant high-confidence RED threats with no
 * attack label were never blocked — that undercounted the spec's intent.
 */
function applyConfidenceGate(actions, threatData, sessionContext, confidenceConfig) {
  if (!confidenceConfig.enabled) return actions;
  if (threatData.level !== 'red' || threatData.threat_score <= 0.75) return actions;

  const confidence = typeof threatData.confidence === 'number' ? threatData.confidence : 1.0;
  const minConfidence = confidenceConfig.min_confidence_for_block ?? 0.2;
  const hasBlockIP = actions.some((action) => action.type === 'block_ip');

  if (confidence < minConfidence) {
    // Low confidence: strip block_ip even if an attack-specific rule added it.
    return actions.filter((action) => action.type !== 'block_ip');
  }

  // Sufficient confidence: ensure block_ip is present, even with no
  // attack_label at all (per the spec's "else" branch above).
  if (!hasBlockIP) {
    return [
      ...actions,
      makeAction('block_ip', sessionContext, {
        severity: 'high',
        reason: 'red_high_confidence_anomaly',
        ip: sessionContext.sourceIP,
      }),
    ];
  }
  return actions;
}

/**
 * Deduplicate actions by `type` (later occurrences win), preserving the
 * order of first appearance. Prevents e.g. rotate_key showing up twice
 * if both level-based and attack-specific policies name it.
 */
function dedupeActions(actions) {
  const seen = new Map();
  for (const action of actions) {
    seen.set(action.type, action);
  }
  return Array.from(seen.values());
}

/**
 * Main entry point: evaluate threat data + session context and return
 * the final ordered list of actions for the Gateway to execute.
 *
 * @param {object} threatData - { threat_score, level, confidence, attack_label, timestamp }
 * @param {object} sessionContext - { sessionId, sourceDevice, targetDevice, sourceIP, targetIP, ... }
 * @returns {Array<object>} ordered action descriptors (Section 6.3 Output Schema)
 */
function evaluate(threatData, sessionContext) {
  if (!threatData || typeof threatData.threat_score !== 'number' || !threatData.level) {
    throw new Error('evaluate() requires threatData with numeric threat_score and level');
  }
  if (!sessionContext || !sessionContext.sessionId) {
    throw new Error('evaluate() requires sessionContext with sessionId');
  }

  const policy = thresholds.getPolicy();
  const severityOverrides = thresholds.getSeverityOverrides();
  const confidenceConfig = thresholds.getConfidenceConfig();

  const activationThreshold = severityOverrides.label_activation_threshold ?? 0.5;
  const fullResetThreshold = severityOverrides.full_reset_threshold ?? 0.85;

  // Priority 1: full session reset overrides everything (Section 6.4).
  const resetOverride = evaluateFullResetOverride(threatData, sessionContext, fullResetThreshold);
  if (resetOverride) {
    return resetOverride;
  }

  // Priority 2 + 3: level-based actions + attack-specific actions, merged.
  let actions = [
    ...evaluateLevelActions(threatData, sessionContext, policy),
    ...evaluateAttackSpecificActions(threatData, sessionContext, policy, activationThreshold),
  ];

  actions = dedupeActions(actions);

  // Priority 4: confidence gate (stretch v1.1, Section 4.6).
  actions = applyConfidenceGate(actions, threatData, sessionContext, confidenceConfig);

  return actions;
}

class DynamicThreatResponseEngine {
  evaluate(threatData, sessionContext) {
    return evaluate(threatData, sessionContext);
  }
}

module.exports = {
  evaluate,
  DynamicThreatResponseEngine,
  // exported for unit testing individual stages
  evaluateLevelActions,
  evaluateAttackSpecificActions,
  evaluateFullResetOverride,
  applyConfidenceGate,
  dedupeActions,
};
