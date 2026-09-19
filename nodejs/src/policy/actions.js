'use strict';

/**
 * Action execution functions for the Dynamic Threat Response Engine (DTRE).
 * See Technical Specification — Module 3, Section 6.5, 7.4.
 *
 * `ActionExecutor` is constructed with references to the Gateway's core
 * modules (Session Manager, Crypto Engine, Entropy Pool, WebSocket Server,
 * Device Registry, Logger) via dependency injection. Those modules live in
 * the Gateway (Module 2) and are NOT implemented here — this keeps the
 * DTRE testable in isolation (Section 12.2/12.3) and decoupled from the
 * Gateway's internals, integrating via the interfaces documented in
 * Section 7.4.
 *
 * Every dependency is expected to expose an async-friendly API; see the
 * `Deps` typedef-style comment below for the minimal contract each one
 * must satisfy. A `NullLogger`/no-op fallback is used for any dependency
 * that's omitted, so this module can also run in unit tests with zero
 * setup for actions that don't need it.
 *
 * Expected dependency shape (see Gateway spec, Module 2, for the real
 * implementations):
 *   sessionManager: {
 *     getSession(sessionId) -> { sourceDevice, targetDevice, sourceIP,
 *       targetIP, sourcePublicKey, targetPublicKey, rotationCount, ... }
 *     updateKey(sessionId, newKey)
 *     updateExpiry(sessionId, newExpiryTimestamp)
 *     incrementRotationCount(sessionId)
 *     terminateSession(sessionId)
 *     terminateSessionsByIP(ip)
 *     terminateAllSessionsForDevice(deviceId)
 *   }
 *   cryptoEngine: { rsaEncrypt(buffer, publicKeyPem) -> base64String }
 *   entropyPool: { getRandomBytes(n) -> Buffer }
 *   webSocketServer: {
 *     notifyDevice(deviceId, messageObject) -> Promise<boolean>
 *     sendError(deviceId, errorObject) -> Promise<boolean>
 *   }
 *   deviceRegistry: { getPublicKey(deviceId) -> string, clearRegistration(deviceId) }
 *   blocklist: Blocklist instance (see ./blocklist.js)
 *   rateLimiter: RateLimiter instance (see ./rateLimiter.js)
 *   logger: { info(msg), warn(msg), error(msg) }
 */

// Require the whole module rather than destructuring (see engine.js for
// why: it keeps thresholds.reload() and test spies working correctly).
const thresholds = require('./thresholds');

const noopLogger = {
  info: () => {},
  warn: () => {},
  error: () => {},
};

const KEY_LIFETIME_SECONDS = {
  GREEN: 300,
  YELLOW: 300,
  ORANGE: 60,
  RED: 30,
};

function nowIso() {
  return new Date().toISOString();
}

function jsonLog(logger, level, event, fields) {
  const entry = { level, timestamp: nowIso(), event, ...fields };
  const line = JSON.stringify(entry);
  if (level === 'error' || level === 'critical') logger.error(line);
  else if (level === 'warn') logger.warn(line);
  else logger.info(line);
}

class ActionExecutor {
  constructor(deps = {}) {
    this.sessionManager = deps.sessionManager || null;
    this.cryptoEngine = deps.cryptoEngine || null;
    this.entropyPool = deps.entropyPool || null;
    this.webSocketServer = deps.webSocketServer || null;
    this.deviceRegistry = deps.deviceRegistry || null;
    this.blocklist = deps.blocklist || null;
    this.rateLimiter = deps.rateLimiter || null;
    this.logger = deps.logger || noopLogger;
  }

  _requireSession(sessionId) {
    if (!this.sessionManager) {
      throw new Error('sessionManager dependency not configured');
    }
    const session = this.sessionManager.getSession(sessionId);
    if (!session) {
      throw new Error(`INVALID_SESSION: no session found for ${sessionId}`);
    }
    return session;
  }

  /**
   * rotateSessionKey() — Section 6.5.
   * Generates a new session key, RSA-encrypts it for both devices,
   * updates the session table, and notifies both devices.
   */
  async rotateSessionKey(sessionId) {
    let session;
    try {
      session = this._requireSession(sessionId);
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'rotate_key',
        session_id: sessionId,
        error: err.message,
        error_code: 'INVALID_SESSION',
      });
      return { success: false, error: err.message };
    }

    let newKey;
    try {
      newKey = this.entropyPool
        ? await this.entropyPool.getRandomBytes(32)
        : require('crypto').randomBytes(32); // fallback, Section 10.4-style resilience
    } catch (err) {
      // Retry once (Section 6.8)
      try {
        newKey = this.entropyPool
          ? await this.entropyPool.getRandomBytes(32)
          : require('crypto').randomBytes(32);
      } catch (err2) {
        jsonLog(this.logger, 'error', 'action_failed', {
          action: 'rotate_key',
          session_id: sessionId,
          error: 'Key generation failed',
          error_code: 'ENTROPY_UNAVAILABLE',
          retry_count: 1,
        });
        return { success: false, error: 'Key generation failed' };
      }
    }

    let encryptedSource;
    let encryptedTarget;
    try {
      encryptedSource = this.cryptoEngine
        ? await this.cryptoEngine.rsaEncrypt(newKey, session.sourcePublicKey)
        : newKey.toString('base64');
      encryptedTarget = this.cryptoEngine
        ? await this.cryptoEngine.rsaEncrypt(newKey, session.targetPublicKey)
        : newKey.toString('base64');
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'rotate_key',
        session_id: sessionId,
        error: 'RSA encryption failed',
        error_code: 'RSA_ENCRYPTION_FAILED',
      });
      return { success: false, error: 'RSA encryption failed' };
    }

    if (this.sessionManager) {
      this.sessionManager.updateKey(sessionId, newKey);
      this.sessionManager.incrementRotationCount(sessionId);
    }

    if (this.webSocketServer) {
      const message = {
        type: 'key_rotate',
        session_id: sessionId,
        reason: 'threat_detected',
      };
      try {
        await this.webSocketServer.notifyDevice(session.sourceDevice, {
          ...message,
          encrypted_key: encryptedSource,
        });
        await this.webSocketServer.notifyDevice(session.targetDevice, {
          ...message,
          encrypted_key: encryptedTarget,
        });
      } catch (err) {
        // Section 6.5: WebSocket notification fails -> log error, but continue
        jsonLog(this.logger, 'error', 'action_failed', {
          action: 'rotate_key',
          session_id: sessionId,
          error: 'WebSocket notification failed',
          error_code: 'WEBSOCKET_SEND_FAILED',
        });
      }
    }

    jsonLog(this.logger, 'info', 'key_rotated', { session_id: sessionId, success: true });

    return {
      success: true,
      newKey,
      encryptedKeys: { source: encryptedSource, target: encryptedTarget },
    };
  }

  /**
   * rateLimitClient() — Section 6.5.
   */
  rateLimitClient(sessionId, limit) {
    let session;
    try {
      session = this._requireSession(sessionId);
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'rate_limit',
        session_id: sessionId,
        error: err.message,
      });
      return { success: false, error: err.message };
    }

    const defaultLimit = thresholds.getActionParameters().rate_limit_default ?? 100;
    const appliedLimit = limit ?? defaultLimit;

    if (!this.rateLimiter) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'rate_limit',
        session_id: sessionId,
        error: 'Rate limiter unavailable',
      });
      return { success: false, error: 'Rate limiter unavailable' };
    }

    this.rateLimiter.setLimit(session.sourceDevice, appliedLimit);
    jsonLog(this.logger, 'info', 'rate_limit_set', {
      session_id: sessionId,
      device_id: session.sourceDevice,
      limit: appliedLimit,
    });

    return { success: true, deviceId: session.sourceDevice, limit: appliedLimit };
  }

  /**
   * blockIP() — Section 6.5. durationSeconds defaults to
   * action_parameters.block_duration_seconds (2 hours).
   */
  async blockIP(sessionId, durationSeconds) {
    let session;
    try {
      session = this._requireSession(sessionId);
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'block_ip',
        session_id: sessionId,
        error: err.message,
      });
      return { success: false, error: err.message };
    }

    if (!this.blocklist) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'block_ip',
        session_id: sessionId,
        error: 'Blocklist unavailable',
      });
      return { success: false, error: 'Blocklist unavailable' };
    }

    const alreadyBlocked = this.blocklist.isBlocked(session.sourceIP);
    const result = this.blocklist.addIP(session.sourceIP, {
      reason: 'dos_detected',
      sessionId,
      sourceDevice: session.sourceDevice,
      durationSeconds,
    });

    if (alreadyBlocked) {
      jsonLog(this.logger, 'info', 'ip_blocked', {
        session_id: sessionId,
        ip: session.sourceIP,
        note: 'already blocked, skipped duplicate',
      });
    } else {
      if (this.sessionManager) {
        this.sessionManager.terminateSessionsByIP(session.sourceIP);
      }
      jsonLog(this.logger, 'warn', 'ip_blocked', {
        session_id: sessionId,
        ip: session.sourceIP,
        reason: 'dos_detected',
        duration_seconds: durationSeconds ?? thresholds.getActionParameters().block_duration_seconds,
        expires_at: result.expiresAt ? new Date(result.expiresAt).toISOString() : undefined,
      });
    }

    if (!result.persisted) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'block_ip',
        session_id: sessionId,
        error: 'Blocklist save failed; IP blocked in memory only',
        error_code: 'BLOCKLIST_SAVE_FAILED',
      });
    }

    return { success: true, ip: session.sourceIP, expiresAt: result.expiresAt };
  }

  /**
   * forceReAuthentication() — Section 6.5.
   */
  async forceReAuthentication(sessionId) {
    let session;
    try {
      session = this._requireSession(sessionId);
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'force_re_auth',
        session_id: sessionId,
        error: err.message,
      });
      return { success: false, error: err.message };
    }

    if (this.sessionManager) {
      this.sessionManager.terminateSession(sessionId);
    }

    if (this.webSocketServer) {
      const message = {
        type: 'reauthentication_required',
        session_id: sessionId,
        reason: 'security_escalation',
      };
      try {
        await this.webSocketServer.notifyDevice(session.sourceDevice, message);
        await this.webSocketServer.notifyDevice(session.targetDevice, message);
      } catch (err) {
        jsonLog(this.logger, 'error', 'action_failed', {
          action: 'force_re_auth',
          session_id: sessionId,
          error: 'WebSocket notification failed; session terminated anyway',
        });
      }
    }

    jsonLog(this.logger, 'warn', 'reauthentication_forced', { session_id: sessionId });
    return { success: true, sessionId };
  }

  /**
   * alertAdmin() / alertAdminUrgent() — Section 6.5.
   */
  async alertAdmin(sessionId, message, { urgent = false, threatScore } = {}) {
    const severity = urgent ? 'high' : 'medium';
    const alertId = `alert_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`;

    jsonLog(this.logger, 'warn', 'admin_alert', {
      session_id: sessionId,
      severity,
      message,
      threat_score: threatScore,
    });

    if (this.webSocketServer) {
      try {
        await this.webSocketServer.notifyDevice('__dashboard__', {
          type: 'admin_alert',
          alert_id: alertId,
          severity,
          message,
          session_id: sessionId,
          timestamp: nowIso(),
        });
      } catch (err) {
        jsonLog(this.logger, 'warn', 'action_failed', {
          action: 'alert_admin',
          session_id: sessionId,
          error: 'Dashboard WebSocket unavailable; alert written to log only',
        });
      }
    }

    return { success: true, alertId };
  }

  async alertAdminUrgent(sessionId, message, opts = {}) {
    return this.alertAdmin(sessionId, message, { ...opts, urgent: true });
  }

  /**
   * enableVerboseLogging() — Section 6.5.
   */
  enableVerboseLogging(sessionId) {
    if (this.sessionManager && typeof this.sessionManager.setLogLevel === 'function') {
      this.sessionManager.setLogLevel(sessionId, 'debug');
    }
    jsonLog(this.logger, 'info', 'verbose_logging_enabled', { session_id: sessionId });
    return { success: true, sessionId };
  }

  /**
   * reduceKeyLifetime() — Section 6.5. lifetimeSeconds defaults based on
   * the current level's mapping (ORANGE=60s, RED=30s).
   */
  reduceKeyLifetime(sessionId, lifetimeSeconds) {
    let session;
    try {
      session = this._requireSession(sessionId);
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'reduce_lifetime',
        session_id: sessionId,
        error: err.message,
      });
      return { success: false, error: err.message };
    }

    const newExpiry = Date.now() + lifetimeSeconds * 1000;
    if (this.sessionManager) {
      this.sessionManager.updateExpiry(sessionId, newExpiry);
    }

    jsonLog(this.logger, 'info', 'key_lifetime_reduced', {
      session_id: sessionId,
      lifetime_seconds: lifetimeSeconds,
    });

    return { success: true, sessionId, newExpiry };
  }

  /**
   * fullSessionReset() — Section 6.5 (Nuclear Option).
   * Triggered when threat_score > 0.85 AND level === "red" (Section 6.4).
   */
  async fullSessionReset(sessionId) {
    let session;
    try {
      session = this._requireSession(sessionId);
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'full_session_reset',
        session_id: sessionId,
        error: err.message,
      });
      return { success: false, error: err.message };
    }

    try {
      if (this.sessionManager) {
        this.sessionManager.terminateAllSessionsForDevice(session.sourceDevice);
        this.sessionManager.terminateAllSessionsForDevice(session.targetDevice);
      }
      if (this.blocklist) {
        this.blocklist.addIP(session.sourceIP, { reason: 'severe_threat_detected', sessionId });
        this.blocklist.addIP(session.targetIP, { reason: 'severe_threat_detected', sessionId });
      }
      if (this.deviceRegistry) {
        this.deviceRegistry.clearRegistration(session.sourceDevice);
        this.deviceRegistry.clearRegistration(session.targetDevice);
      }
    } catch (err) {
      jsonLog(this.logger, 'error', 'action_failed', {
        action: 'full_session_reset',
        session_id: sessionId,
        error: err.message,
      });
      // Continue — Section 6.8: "attempt to continue" on failure
    }

    jsonLog(this.logger, 'critical', 'full_reset_executed', {
      session_id: sessionId,
      source_device: session.sourceDevice,
      target_device: session.targetDevice,
      reason: 'severe_threat_detected',
    });

    return { success: true, sourceDevice: session.sourceDevice, targetDevice: session.targetDevice };
  }
}

module.exports = { ActionExecutor, KEY_LIFETIME_SECONDS };
