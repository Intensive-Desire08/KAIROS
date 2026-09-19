'use strict';

const baseThreatData = {
  threat_score: 0.1,
  level: 'green',
  confidence: 0.9,
  attack_label: null,
  timestamp: '2026-08-31T14:30:10Z',
};

const baseSessionContext = {
  sessionId: 'sess_test_0001',
  sourceDevice: 'drone_001',
  targetDevice: 'base_station_002',
  sourceIP: '192.168.1.100',
  targetIP: '192.168.1.1',
  sourcePublicKey: '-----BEGIN PUBLIC KEY-----FAKE-----END PUBLIC KEY-----',
  targetPublicKey: '-----BEGIN PUBLIC KEY-----FAKE-----END PUBLIC KEY-----',
};

module.exports = { baseThreatData, baseSessionContext };
