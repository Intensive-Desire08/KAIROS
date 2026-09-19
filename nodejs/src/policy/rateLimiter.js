'use strict';

/**
 * Rate limiter for the Dynamic Threat Response Engine (DTRE).
 * See Technical Specification — Module 3, Section 6.5 (rateLimitClient).
 *
 * In-memory Map keyed by deviceId: { limit, currentCount, resetTime }.
 * Counter resets every second (rolling window semantics matching the
 * Gateway's packet_rate feature calculation, Section 2.2).
 */

const RESET_INTERVAL_MS = 1000;

class RateLimiter {
  constructor() {
    this.limits = new Map(); // deviceId -> { limit, currentCount, resetTime }
  }

  setLimit(deviceId, limit = 100) {
    const existing = this.limits.get(deviceId);
    this.limits.set(deviceId, {
      limit,
      currentCount: existing ? existing.currentCount : 0,
      resetTime: Date.now() + RESET_INTERVAL_MS,
    });
    return { success: true, deviceId, limit };
  }

  /**
   * Check (and increment) the packet count for a device against its limit.
   * Returns { allowed: boolean, limit, currentCount }.
   */
  checkLimit(deviceId) {
    let entry = this.limits.get(deviceId);
    const now = Date.now();

    if (!entry) {
      // No limit configured for this device — allow by default.
      return { allowed: true, limit: null, currentCount: 0 };
    }

    if (now >= entry.resetTime) {
      entry.currentCount = 0;
      entry.resetTime = now + RESET_INTERVAL_MS;
    }

    entry.currentCount += 1;
    const allowed = entry.currentCount <= entry.limit;
    return { allowed, limit: entry.limit, currentCount: entry.currentCount };
  }

  reset(deviceId) {
    const entry = this.limits.get(deviceId);
    if (entry) {
      entry.currentCount = 0;
      entry.resetTime = Date.now() + RESET_INTERVAL_MS;
    }
    return { success: true, deviceId };
  }

  removeLimit(deviceId) {
    return this.limits.delete(deviceId);
  }

  getLimit(deviceId) {
    return this.limits.get(deviceId) || null;
  }
}

module.exports = { RateLimiter };
