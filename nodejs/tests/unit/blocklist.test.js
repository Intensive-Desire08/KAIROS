'use strict';

const fs = require('fs');
const { Blocklist } = require('../../src/policy/blocklist');

const TEST_PATH = '/tmp/kairos_jest_blocklist.json';

function cleanup() {
  try {
    fs.unlinkSync(TEST_PATH);
  } catch (e) {
    /* ignore */
  }
}

beforeEach(cleanup);
afterEach(cleanup);

describe('Blocklist', () => {
  test('starts empty when no file exists', () => {
    const bl = new Blocklist(TEST_PATH);
    expect(bl.isBlocked('1.2.3.4')).toBe(false);
    expect(bl.size()).toBe(0);
  });

  test('addIP blocks the IP and persists to disk', () => {
    const bl = new Blocklist(TEST_PATH);
    const result = bl.addIP('1.2.3.4', { reason: 'dos_detected', sessionId: 'sess_1' });

    expect(result.success).toBe(true);
    expect(bl.isBlocked('1.2.3.4')).toBe(true);
    expect(fs.existsSync(TEST_PATH)).toBe(true);

    const persisted = JSON.parse(fs.readFileSync(TEST_PATH, 'utf-8'));
    expect(persisted.blocked_ips['1.2.3.4']).toBeDefined();
    expect(persisted.blocked_ips['1.2.3.4'].reason).toBe('dos_detected');
  });

  test('a new Blocklist instance loads persisted entries from disk', () => {
    const bl1 = new Blocklist(TEST_PATH);
    bl1.addIP('5.6.7.8', { reason: 'test' });

    const bl2 = new Blocklist(TEST_PATH);
    expect(bl2.isBlocked('5.6.7.8')).toBe(true);
  });

  test('addIP is idempotent for an already-blocked IP', () => {
    const bl = new Blocklist(TEST_PATH);
    bl.addIP('1.2.3.4', { reason: 'first' });
    const second = bl.addIP('1.2.3.4', { reason: 'second' });
    expect(second.alreadyBlocked).toBe(true);
  });

  test('expired entries are auto-removed on isBlocked() check', (done) => {
    const bl = new Blocklist(TEST_PATH);
    bl.addIP('9.9.9.9', { reason: 'test', durationSeconds: 0.05 });
    expect(bl.isBlocked('9.9.9.9')).toBe(true);

    setTimeout(() => {
      expect(bl.isBlocked('9.9.9.9')).toBe(false);
      done();
    }, 150);
  });

  test('removeIP removes an entry', () => {
    const bl = new Blocklist(TEST_PATH);
    bl.addIP('1.2.3.4', { reason: 'test' });
    const result = bl.removeIP('1.2.3.4');
    expect(result.removed).toBe(true);
    expect(bl.isBlocked('1.2.3.4')).toBe(false);
  });

  test('handles a corrupt blocklist file gracefully (starts empty)', () => {
    fs.writeFileSync(TEST_PATH, '{ this is not valid json', 'utf-8');
    const bl = new Blocklist(TEST_PATH);
    expect(bl.size()).toBe(0);
  });

  test('pruneExpired removes only expired entries', () => {
    const bl = new Blocklist(TEST_PATH);
    bl.addIP('1.1.1.1', { reason: 'test', durationSeconds: 7200 });
    bl.entries.set('2.2.2.2', {
      ip: '2.2.2.2',
      reason: 'test',
      blocked_at: new Date(Date.now() - 10000).toISOString(),
      expires_at: new Date(Date.now() - 1000).toISOString(),
    });
    const removed = bl.pruneExpired();
    expect(removed).toBe(1);
    expect(bl.isBlocked('1.1.1.1')).toBe(true);
    expect(bl.entries.has('2.2.2.2')).toBe(false);
  });
});
