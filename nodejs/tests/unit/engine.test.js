'use strict';

const path = require('path');
const { evaluate, dedupeActions } = require('../../src/policy/engine');
const thresholds = require('../../src/policy/thresholds');
const { baseThreatData, baseSessionContext } = require('../fixtures/testData');

// Use the repo's real config.json so tests exercise the actual policy.
beforeAll(() => {
  thresholds.reload(path.join(__dirname, '..', '..', 'config', 'config.json'));
});

function withThreat(overrides) {
  return { ...baseThreatData, ...overrides };
}

describe('Level-based actions (Section 6.4)', () => {
  test('GREEN level returns no actions', () => {
    const actions = evaluate(withThreat({ level: 'green', threat_score: 0.1 }), baseSessionContext);
    expect(actions).toEqual([]);
  });

  test('YELLOW level returns verbose_logging', () => {
    const actions = evaluate(withThreat({ level: 'yellow', threat_score: 0.4 }), baseSessionContext);
    expect(actions.map((a) => a.type)).toEqual(['verbose_logging']);
  });

  test('ORANGE level returns rotate_key, reduce_lifetime, alert_admin', () => {
    const actions = evaluate(withThreat({ level: 'orange', threat_score: 0.6 }), baseSessionContext);
    expect(actions.map((a) => a.type).sort()).toEqual(
      ['rotate_key', 'reduce_lifetime', 'alert_admin'].sort()
    );
  });

  test('RED level returns rotate_key, rate_limit, force_re_auth, alert_admin (urgent)', () => {
    const actions = evaluate(withThreat({ level: 'red', threat_score: 0.78 }), baseSessionContext);
    const types = actions.map((a) => a.type).sort();
    expect(types).toEqual(['rotate_key', 'rate_limit', 'force_re_auth', 'alert_admin'].sort());
    const alert = actions.find((a) => a.type === 'alert_admin');
    expect(alert.urgent).toBe(true);
  });
});

describe('Attack-specific actions (Section 6.4)', () => {
  test('DOS label with threat_score > 0.5 adds block_ip', () => {
    const actions = evaluate(
      withThreat({ level: 'orange', threat_score: 0.78, attack_label: 'DOS' }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).toContain('block_ip');
  });

  test('DOS label with threat_score <= 0.5 does NOT trigger block_ip', () => {
    const actions = evaluate(
      withThreat({ level: 'green', threat_score: 0.4, attack_label: 'DOS' }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).not.toContain('block_ip');
  });

  test('BRUTE_FORCE label adds lock_account and block_ip', () => {
    const actions = evaluate(
      withThreat({ level: 'orange', threat_score: 0.8, attack_label: 'BRUTE_FORCE' }),
      baseSessionContext
    );
    const types = actions.map((a) => a.type);
    expect(types).toContain('lock_account');
    expect(types).toContain('block_ip');
  });

  test('PORT_SCAN label adds block_ip and verbose_logging', () => {
    const actions = evaluate(
      withThreat({ level: 'orange', threat_score: 0.8, attack_label: 'PORT_SCAN' }),
      baseSessionContext
    );
    const types = actions.map((a) => a.type);
    expect(types).toContain('block_ip');
    expect(types).toContain('verbose_logging');
  });
});

describe('Threat score override — full session reset (Section 6.4)', () => {
  test('threat_score > 0.85 AND level red triggers ONLY full_session_reset', () => {
    const actions = evaluate(withThreat({ level: 'red', threat_score: 0.92 }), baseSessionContext);
    expect(actions).toHaveLength(1);
    expect(actions[0].type).toBe('full_session_reset');
  });

  test('threat_score > 0.85 but level is NOT red does not trigger full reset', () => {
    const actions = evaluate(withThreat({ level: 'orange', threat_score: 0.9 }), baseSessionContext);
    expect(actions.map((a) => a.type)).not.toContain('full_session_reset');
  });

  test('level red but threat_score <= 0.85 does not trigger full reset', () => {
    const actions = evaluate(withThreat({ level: 'red', threat_score: 0.8 }), baseSessionContext);
    expect(actions.map((a) => a.type)).not.toContain('full_session_reset');
  });
});

describe('Confidence-gated RED override (Section 4.6 / 6.4, stretch v1.1)', () => {
  // NOTE: threat_score must stay in (0.75, 0.85] to isolate the confidence
  // gate — anything > 0.85 triggers the full_session_reset override first
  // (Section 6.4 priority order), which returns early and never reaches
  // the confidence gate at all.

  test('low confidence skips block_ip on RED + attack label, when confidence gating enabled', () => {
    const originalModule = require('../../src/policy/thresholds');
    const spy = jest.spyOn(originalModule, 'getConfidenceConfig').mockReturnValue({
      enabled: true,
      min_confidence_for_block: 0.2,
    });

    const actions = evaluate(
      withThreat({ level: 'red', threat_score: 0.78, attack_label: 'DOS', confidence: 0.1 }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).not.toContain('block_ip');
    // Other RED actions should still be present
    expect(actions.map((a) => a.type)).toContain('rotate_key');

    spy.mockRestore();
  });

  test('sufficient confidence keeps block_ip on RED + attack label, when confidence gating enabled', () => {
    const originalModule = require('../../src/policy/thresholds');
    const spy = jest.spyOn(originalModule, 'getConfidenceConfig').mockReturnValue({
      enabled: true,
      min_confidence_for_block: 0.2,
    });

    const actions = evaluate(
      withThreat({ level: 'red', threat_score: 0.78, attack_label: 'DOS', confidence: 0.95 }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).toContain('block_ip');

    spy.mockRestore();
  });

  test('high confidence ADDS block_ip on RED even with NO attack_label at all (Section 6.4 pseudocode)', () => {
    // This is the additive half of the spec's confidence logic: the
    // "else" branch of the pseudocode returns blockIP unconditionally
    // for a high-confidence RED anomaly, independent of attack_label.
    const originalModule = require('../../src/policy/thresholds');
    const spy = jest.spyOn(originalModule, 'getConfidenceConfig').mockReturnValue({
      enabled: true,
      min_confidence_for_block: 0.2,
    });

    const actions = evaluate(
      withThreat({ level: 'red', threat_score: 0.8, attack_label: null, confidence: 0.95 }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).toContain('block_ip');

    spy.mockRestore();
  });

  test('low confidence does NOT add block_ip on RED with no attack_label', () => {
    const originalModule = require('../../src/policy/thresholds');
    const spy = jest.spyOn(originalModule, 'getConfidenceConfig').mockReturnValue({
      enabled: true,
      min_confidence_for_block: 0.2,
    });

    const actions = evaluate(
      withThreat({ level: 'red', threat_score: 0.8, attack_label: null, confidence: 0.05 }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).not.toContain('block_ip');

    spy.mockRestore();
  });

  test('block_ip is not duplicated when both attack-specific rule and confidence gate would add it', () => {
    const originalModule = require('../../src/policy/thresholds');
    const spy = jest.spyOn(originalModule, 'getConfidenceConfig').mockReturnValue({
      enabled: true,
      min_confidence_for_block: 0.2,
    });

    const actions = evaluate(
      withThreat({ level: 'red', threat_score: 0.78, attack_label: 'DOS', confidence: 0.95 }),
      baseSessionContext
    );
    expect(actions.filter((a) => a.type === 'block_ip')).toHaveLength(1);

    spy.mockRestore();
  });

  test('confidence gate has no effect when confidence.enabled is false (default)', () => {
    // Default config.json has confidence.enabled = false, so RED with no
    // attack_label should NOT get block_ip even at high confidence.
    const actions = evaluate(
      withThreat({ level: 'red', threat_score: 0.8, attack_label: null, confidence: 0.99 }),
      baseSessionContext
    );
    expect(actions.map((a) => a.type)).not.toContain('block_ip');
  });
});

describe('dedupeActions', () => {
  test('keeps only the last occurrence of each action type, preserving first-seen order', () => {
    const actions = [
      { type: 'rotate_key', severity: 'low' },
      { type: 'block_ip', severity: 'high' },
      { type: 'rotate_key', severity: 'high' },
    ];
    const result = dedupeActions(actions);
    expect(result).toHaveLength(2);
    expect(result[0].type).toBe('rotate_key');
    expect(result[0].severity).toBe('high');
  });
});

describe('Input validation', () => {
  test('throws if threatData is missing threat_score/level', () => {
    expect(() => evaluate({}, baseSessionContext)).toThrow();
  });

  test('throws if sessionContext is missing sessionId', () => {
    expect(() => evaluate(baseThreatData, {})).toThrow();
  });
});
