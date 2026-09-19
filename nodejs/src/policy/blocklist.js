'use strict';

/**
 * IP blocklist manager for the Dynamic Threat Response Engine (DTRE).
 * See Technical Specification — Module 3, Section 6.5 (blockIP), 9.3.
 *
 * Persists to a JSON file (default: nodejs/data/blocklist.json). IPs
 * auto-expire based on `expires_at` — checked lazily on `isBlocked()`
 * rather than via a timer, per the spec's "Auto-Reset Logic" (Section 6.5).
 */

const fs = require('fs');
const path = require('path');

const thresholds = require('./thresholds');

class Blocklist {
  constructor(persistenceFile) {
    this.persistenceFile =
      persistenceFile ||
      thresholds.getActionParameters().blocklist_persistence_file ||
      path.join(__dirname, '..', '..', 'data', 'blocklist.json');
    this.entries = new Map(); // ip -> { ip, reason, blocked_at, expires_at, session_id, source_device }
    this._load();
  }

  _resolvePath() {
    return path.isAbsolute(this.persistenceFile)
      ? this.persistenceFile
      : path.join(__dirname, '..', '..', this.persistenceFile.replace(/^\.\//, ''));
  }

  _load() {
    const filePath = this._resolvePath();
    try {
      const raw = fs.readFileSync(filePath, 'utf-8');
      const parsed = JSON.parse(raw);
      const blocked = parsed.blocked_ips || {};
      for (const [ip, entry] of Object.entries(blocked)) {
        this.entries.set(ip, entry);
      }
    } catch (err) {
      // File missing or corrupt — start with an empty blocklist and
      // create the file on first save (Section 10.8: Storage Error Handling).
      this.entries = new Map();
    }
  }

  save() {
    const filePath = this._resolvePath();
    const blocked_ips = {};
    for (const [ip, entry] of this.entries.entries()) {
      blocked_ips[ip] = entry;
    }
    const body = {
      version: '1.0.0',
      last_updated: new Date().toISOString(),
      blocked_ips,
    };
    try {
      fs.mkdirSync(path.dirname(filePath), { recursive: true });
      fs.writeFileSync(filePath, JSON.stringify(body, null, 2), 'utf-8');
      return { success: true };
    } catch (err) {
      // Section 6.8: blocklist save fails -> IP blocked in memory only; log error.
      return { success: false, error: err.message };
    }
  }

  /**
   * Add an IP to the blocklist with an expiry.
   * durationSeconds defaults to action_parameters.block_duration_seconds (2h).
   */
  addIP(ip, { reason, sessionId, sourceDevice, durationSeconds } = {}) {
    if (this.isBlocked(ip)) {
      return { success: true, alreadyBlocked: true, ip };
    }
    const duration = durationSeconds ?? thresholds.getActionParameters().block_duration_seconds ?? 7200;
    const blockedAt = new Date();
    const expiresAt = new Date(blockedAt.getTime() + duration * 1000);

    const entry = {
      ip,
      reason: reason || 'unspecified',
      blocked_at: blockedAt.toISOString(),
      expires_at: expiresAt.toISOString(),
      session_id: sessionId,
      source_device: sourceDevice,
    };
    this.entries.set(ip, entry);
    const saveResult = this.save();

    return {
      success: true,
      ip,
      expiresAt: expiresAt.getTime(),
      persisted: saveResult.success,
    };
  }

  /**
   * Check if an IP is currently blocked. Auto-removes expired entries
   * (Section 6.5 Auto-Reset Logic).
   */
  isBlocked(ip) {
    const entry = this.entries.get(ip);
    if (!entry) return false;
    if (new Date(entry.expires_at).getTime() < Date.now()) {
      this.entries.delete(ip);
      return false;
    }
    return true;
  }

  removeIP(ip) {
    const existed = this.entries.delete(ip);
    if (existed) this.save();
    return { success: true, removed: existed };
  }

  getEntry(ip) {
    return this.entries.get(ip) || null;
  }

  size() {
    return this.entries.size;
  }

  /** Remove all expired entries proactively (e.g. on a periodic sweep). */
  pruneExpired() {
    let removed = 0;
    const now = Date.now();
    for (const [ip, entry] of this.entries.entries()) {
      if (new Date(entry.expires_at).getTime() < now) {
        this.entries.delete(ip);
        removed += 1;
      }
    }
    if (removed > 0) this.save();
    return removed;
  }
}

module.exports = { Blocklist };
