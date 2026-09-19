'use strict';

const { RateLimiter } = require('../../src/policy/rateLimiter');

describe('RateLimiter', () => {
  test('checkLimit allows unlimited devices by default', () => {
    const rl = new RateLimiter();
    const result = rl.checkLimit('device_without_limit');
    expect(result.allowed).toBe(true);
    expect(result.limit).toBeNull();
  });

  test('setLimit configures a limit for a device', () => {
    const rl = new RateLimiter();
    const result = rl.setLimit('drone_001', 5);
    expect(result.success).toBe(true);
    expect(rl.getLimit('drone_001').limit).toBe(5);
  });

  test('checkLimit allows packets under the limit', () => {
    const rl = new RateLimiter();
    rl.setLimit('drone_001', 3);
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
  });

  test('checkLimit rejects packets over the limit within the same window', () => {
    const rl = new RateLimiter();
    rl.setLimit('drone_001', 2);
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
    const third = rl.checkLimit('drone_001');
    expect(third.allowed).toBe(false);
    expect(third.currentCount).toBe(3);
  });

  test('reset() clears the current count', () => {
    const rl = new RateLimiter();
    rl.setLimit('drone_001', 1);
    rl.checkLimit('drone_001');
    expect(rl.checkLimit('drone_001').allowed).toBe(false);
    rl.reset('drone_001');
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
  });

  test('the counter rolls over after the 1-second window', (done) => {
    const rl = new RateLimiter();
    rl.setLimit('drone_001', 1);
    expect(rl.checkLimit('drone_001').allowed).toBe(true);
    expect(rl.checkLimit('drone_001').allowed).toBe(false);

    setTimeout(() => {
      expect(rl.checkLimit('drone_001').allowed).toBe(true);
      done();
    }, 1100);
  });

  test('removeLimit deletes the device entry', () => {
    const rl = new RateLimiter();
    rl.setLimit('drone_001', 5);
    expect(rl.removeLimit('drone_001')).toBe(true);
    expect(rl.getLimit('drone_001')).toBeNull();
  });
});
