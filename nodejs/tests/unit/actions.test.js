'use strict';

const { ActionExecutor } = require('../../src/policy/actions');
const { Blocklist } = require('../../src/policy/blocklist');
const { RateLimiter } = require('../../src/policy/rateLimiter');
const { baseSessionContext } = require('../fixtures/testData');

function makeMockSessionManager(session = baseSessionContext) {
  return {
    getSession: jest.fn((id) => (id === session.sessionId ? { ...session } : null)),
    updateKey: jest.fn(),
    updateExpiry: jest.fn(),
    incrementRotationCount: jest.fn(),
    terminateSession: jest.fn(),
    terminateSessionsByIP: jest.fn(),
    terminateAllSessionsForDevice: jest.fn(),
  };
}

function makeExecutor(overrides = {}) {
  const sessionManager = overrides.sessionManager || makeMockSessionManager();
  const cryptoEngine = overrides.cryptoEngine || {
    rsaEncrypt: jest.fn(async (buf) => buf.toString('base64')),
  };
  const entropyPool = overrides.entropyPool || {
    getRandomBytes: jest.fn(async (n) => require('crypto').randomBytes(n)),
  };
  const webSocketServer = overrides.webSocketServer || {
    notifyDevice: jest.fn(async () => true),
    sendError: jest.fn(async () => true),
  };
  const blocklist = overrides.blocklist || new Blocklist('/tmp/kairos_test_blocklist.json');
  const rateLimiter = overrides.rateLimiter || new RateLimiter();
  const logger = overrides.logger || { info: jest.fn(), warn: jest.fn(), error: jest.fn() };

  return new ActionExecutor({
    sessionManager,
    cryptoEngine,
    entropyPool,
    webSocketServer,
    deviceRegistry: overrides.deviceRegistry || { clearRegistration: jest.fn(), getPublicKey: jest.fn() },
    blocklist,
    rateLimiter,
    logger,
  });
}

describe('rotateSessionKey()', () => {
  test('generates a new key, encrypts for both devices, notifies both', async () => {
    const executor = makeExecutor();
    const result = await executor.rotateSessionKey(baseSessionContext.sessionId);

    expect(result.success).toBe(true);
    expect(result.newKey).toBeInstanceOf(Buffer);
    expect(result.newKey.length).toBe(32);
    expect(result.encryptedKeys.source).toBeDefined();
    expect(result.encryptedKeys.target).toBeDefined();
    expect(executor.webSocketServer.notifyDevice).toHaveBeenCalledTimes(2);
    expect(executor.sessionManager.updateKey).toHaveBeenCalled();
    expect(executor.sessionManager.incrementRotationCount).toHaveBeenCalled();
  });

  test('fails gracefully for an unknown session', async () => {
    const executor = makeExecutor();
    const result = await executor.rotateSessionKey('nonexistent_session');
    expect(result.success).toBe(false);
  });

  test('continues (success=true) even if WebSocket notification fails', async () => {
    const webSocketServer = {
      notifyDevice: jest.fn(async () => {
        throw new Error('socket closed');
      }),
    };
    const executor = makeExecutor({ webSocketServer });
    const result = await executor.rotateSessionKey(baseSessionContext.sessionId);
    expect(result.success).toBe(true);
  });
});

describe('rateLimitClient()', () => {
  test('sets a rate limit for the session source device', () => {
    const executor = makeExecutor();
    const result = executor.rateLimitClient(baseSessionContext.sessionId, 50);
    expect(result.success).toBe(true);
    expect(result.deviceId).toBe(baseSessionContext.sourceDevice);
    expect(result.limit).toBe(50);
    expect(executor.rateLimiter.getLimit(baseSessionContext.sourceDevice).limit).toBe(50);
  });

  test('uses default limit when none specified', () => {
    const executor = makeExecutor();
    const result = executor.rateLimitClient(baseSessionContext.sessionId);
    expect(result.limit).toBe(100);
  });
});

describe('blockIP()', () => {
  const testBlocklistPath = '/tmp/kairos_test_blocklist_blockip.json';

  afterEach(() => {
    const fs = require('fs');
    try {
      fs.unlinkSync(testBlocklistPath);
    } catch (e) {
      /* ignore */
    }
  });

  test('adds the session source IP to the blocklist and terminates sessions', async () => {
    const blocklist = new Blocklist(testBlocklistPath);
    const executor = makeExecutor({ blocklist });
    const result = await executor.blockIP(baseSessionContext.sessionId, 7200);

    expect(result.success).toBe(true);
    expect(result.ip).toBe(baseSessionContext.sourceIP);
    expect(blocklist.isBlocked(baseSessionContext.sourceIP)).toBe(true);
    expect(executor.sessionManager.terminateSessionsByIP).toHaveBeenCalledWith(baseSessionContext.sourceIP);
  });

  test('skips duplicate block without re-terminating sessions', async () => {
    const blocklist = new Blocklist(testBlocklistPath);
    const executor = makeExecutor({ blocklist });
    await executor.blockIP(baseSessionContext.sessionId, 7200);
    executor.sessionManager.terminateSessionsByIP.mockClear();

    const result = await executor.blockIP(baseSessionContext.sessionId, 7200);
    expect(result.success).toBe(true);
    expect(executor.sessionManager.terminateSessionsByIP).not.toHaveBeenCalled();
  });
});

describe('forceReAuthentication()', () => {
  test('terminates the session and notifies both devices', async () => {
    const executor = makeExecutor();
    const result = await executor.forceReAuthentication(baseSessionContext.sessionId);
    expect(result.success).toBe(true);
    expect(executor.sessionManager.terminateSession).toHaveBeenCalledWith(baseSessionContext.sessionId);
    expect(executor.webSocketServer.notifyDevice).toHaveBeenCalledTimes(2);
  });
});

describe('alertAdmin() / alertAdminUrgent()', () => {
  test('alertAdmin returns an alertId and does not throw without a dashboard', async () => {
    const executor = makeExecutor({ webSocketServer: undefined });
    const result = await executor.alertAdmin(baseSessionContext.sessionId, 'test message');
    expect(result.success).toBe(true);
    expect(result.alertId).toBeDefined();
  });

  test('alertAdminUrgent logs with high severity', async () => {
    const logger = { info: jest.fn(), warn: jest.fn(), error: jest.fn() };
    const executor = makeExecutor({ logger });
    await executor.alertAdminUrgent(baseSessionContext.sessionId, 'urgent message');
    const warnCall = logger.warn.mock.calls.find((c) => c[0].includes('admin_alert'));
    expect(warnCall).toBeDefined();
    expect(JSON.parse(warnCall[0]).severity).toBe('high');
  });
});

describe('reduceKeyLifetime()', () => {
  test('updates session expiry', () => {
    const executor = makeExecutor();
    const result = executor.reduceKeyLifetime(baseSessionContext.sessionId, 30);
    expect(result.success).toBe(true);
    expect(executor.sessionManager.updateExpiry).toHaveBeenCalled();
  });
});

describe('fullSessionReset()', () => {
  const testBlocklistPath = '/tmp/kairos_test_blocklist_reset.json';
  afterEach(() => {
    const fs = require('fs');
    try {
      fs.unlinkSync(testBlocklistPath);
    } catch (e) {
      /* ignore */
    }
  });

  test('terminates all sessions for both devices and blocks both IPs', async () => {
    const blocklist = new Blocklist(testBlocklistPath);
    const executor = makeExecutor({ blocklist });
    const result = await executor.fullSessionReset(baseSessionContext.sessionId);

    expect(result.success).toBe(true);
    expect(executor.sessionManager.terminateAllSessionsForDevice).toHaveBeenCalledWith(
      baseSessionContext.sourceDevice
    );
    expect(executor.sessionManager.terminateAllSessionsForDevice).toHaveBeenCalledWith(
      baseSessionContext.targetDevice
    );
    expect(blocklist.isBlocked(baseSessionContext.sourceIP)).toBe(true);
    expect(blocklist.isBlocked(baseSessionContext.targetIP)).toBe(true);
  });
});
