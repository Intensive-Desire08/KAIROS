'use strict';

/**
 * Module entry point for the Dynamic Threat Response Engine (DTRE).
 * See Technical Specification — Module 3, Section 6.2.
 *
 * Usage from the Gateway:
 *
 *   const { DynamicThreatResponseEngine, ActionExecutor } = require('./policy');
 *
 *   const executor = new ActionExecutor({
 *     sessionManager, cryptoEngine, entropyPool,
 *     webSocketServer, deviceRegistry, blocklist, rateLimiter, logger,
 *   });
 *   const engine = new DynamicThreatResponseEngine();
 *
 *   const actions = engine.evaluate(threatData, sessionContext);
 *   for (const action of actions) {
 *     await executeAction(executor, action); // dispatch by action.type
 *   }
 */

const { evaluate, DynamicThreatResponseEngine } = require('./engine');
const { ActionExecutor, KEY_LIFETIME_SECONDS } = require('./actions');
const { Blocklist } = require('./blocklist');
const { RateLimiter } = require('./rateLimiter');
const thresholds = require('./thresholds');

/**
 * Convenience dispatcher: given an ActionExecutor and an action
 * descriptor produced by engine.evaluate(), calls the matching
 * executor method with the right arguments.
 */
async function executeAction(executor, action) {
  switch (action.type) {
    case 'rotate_key':
      return executor.rotateSessionKey(action.sessionId);
    case 'reduce_lifetime':
      return executor.reduceKeyLifetime(action.sessionId, action.lifetimeSeconds);
    case 'rate_limit':
      return executor.rateLimitClient(action.sessionId, action.limit);
    case 'force_re_auth':
      return executor.forceReAuthentication(action.sessionId);
    case 'alert_admin': {
      const message = action.message || action.reason;
      return action.urgent
        ? executor.alertAdminUrgent(action.sessionId, message, { threatScore: action.threatScore })
        : executor.alertAdmin(action.sessionId, message, { threatScore: action.threatScore });
    }
    case 'verbose_logging':
      return executor.enableVerboseLogging(action.sessionId);
    case 'block_ip':
      return executor.blockIP(action.sessionId);
    case 'full_session_reset':
      return executor.fullSessionReset(action.sessionId);
    case 'lock_account':
      // Account locking is a Gateway/Device-Registry concern outside the
      // scope of Module 3; exposed here as a no-op hook so integrators
      // can wire it up without modifying the engine.
      return { success: true, skipped: true, reason: 'lock_account not implemented in Module 3' };
    default:
      throw new Error(`Unknown action type: ${action.type}`);
  }
}

module.exports = {
  DynamicThreatResponseEngine,
  evaluate,
  ActionExecutor,
  KEY_LIFETIME_SECONDS,
  Blocklist,
  RateLimiter,
  executeAction,
  thresholds,
};
