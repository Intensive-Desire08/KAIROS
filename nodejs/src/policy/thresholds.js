'use strict';

/**
 * Configuration loader for the Dynamic Threat Response Engine (DTRE).
 * See Technical Specification — Module 3, Section 6.7 / 8.
 *
 * Loads the shared config.json (nodejs/config/config.json) and exposes
 * cached accessors for thresholds, policy, action parameters, and
 * confidence settings. Config is loaded once and cached; call
 * `reload()` after editing config.json (no hot-reload per Section 8.1).
 */

const fs = require('fs');
const path = require('path');

const DEFAULT_CONFIG_PATH =
  process.env.CONFIG_PATH || path.join(__dirname, '..', '..', 'config', 'config.json');

const DEFAULTS = Object.freeze({
  thresholds: { green: 0.25, yellow: 0.5, orange: 0.75, red: 1.0 },
  attack_labels: {
    dos_threshold: 1000,
    bruteforce_threshold: 10,
    portscan_threshold: 50,
    activation_threshold: 0.5,
  },
  policy: {
    yellow_actions: ['verbose_logging'],
    orange_actions: ['rotate_key', 'reduce_lifetime', 'alert_admin'],
    red_actions: ['rotate_key', 'rate_limit', 'force_re_auth', 'alert_admin_urgent'],
    dos_actions: ['block_ip'],
    bruteforce_actions: ['lock_account', 'block_ip'],
    portscan_actions: ['block_ip', 'verbose_logging'],
  },
  severity_overrides: {
    full_reset_threshold: 0.85,
    label_activation_threshold: 0.5,
  },
  action_parameters: {
    rate_limit_default: 100,
    block_duration_seconds: 7200,
    orange_key_lifetime_seconds: 60,
    red_key_lifetime_seconds: 30,
    blocklist_persistence_file: './data/blocklist.json',
  },
  confidence: {
    enabled: false,
    min_confidence_for_block: 0.2,
  },
});

let _cache = null;
let _cachePath = null;

/**
 * Deep-merge `override` on top of `base` (shallow per top-level key is
 * enough here since config.json sections don't nest further than one
 * level of plain objects/arrays).
 */
function mergeDefaults(base, override) {
  const result = { ...base };
  for (const key of Object.keys(base)) {
    if (override && Object.prototype.hasOwnProperty.call(override, key)) {
      result[key] =
        typeof base[key] === 'object' && !Array.isArray(base[key]) && base[key] !== null
          ? { ...base[key], ...override[key] }
          : override[key];
    }
  }
  return result;
}

/**
 * Load and cache configuration from config.json (Section 8.2).
 * Falls back to safe defaults if the file is missing or invalid, so the
 * DTRE can still start (fail-safe philosophy, Section 10.1).
 */
function loadThresholds(configPath = DEFAULT_CONFIG_PATH) {
  if (_cache && _cachePath === configPath) {
    return _cache;
  }

  let loaded = {};
  try {
    const raw = fs.readFileSync(configPath, 'utf-8');
    loaded = JSON.parse(raw);
  } catch (err) {
    // File missing or invalid JSON — use defaults (Section 10.8 philosophy)
    loaded = {};
  }

  _cache = mergeDefaults(DEFAULTS, loaded);
  _cachePath = configPath;
  return _cache;
}

function reload(configPath = _cachePath || DEFAULT_CONFIG_PATH) {
  _cache = null;
  return loadThresholds(configPath);
}

function getPolicy() {
  return loadThresholds().policy;
}

function getThresholdLevels() {
  return loadThresholds().thresholds;
}

function getSeverityOverrides() {
  return loadThresholds().severity_overrides;
}

function getActionParameters() {
  return loadThresholds().action_parameters;
}

function getConfidenceConfig() {
  return loadThresholds().confidence;
}

module.exports = {
  loadThresholds,
  reload,
  getPolicy,
  getThresholdLevels,
  getSeverityOverrides,
  getActionParameters,
  getConfidenceConfig,
  DEFAULT_CONFIG_PATH,
};
