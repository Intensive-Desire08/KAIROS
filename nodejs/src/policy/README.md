# Dynamic Threat Response Engine (DTRE)

Node.js policy engine module. See Technical Specification — Module 3, Section 6.

## Files

| File | Purpose |
|---|---|
| `engine.js` | Pure decision logic: `evaluate(threatData, sessionContext) -> actions[]` |
| `actions.js` | `ActionExecutor` — the 8 action functions, dependency-injected against Gateway modules |
| `thresholds.js` | Loads and caches `config.json` |
| `blocklist.js` | Persistent IP blocklist with auto-expiry |
| `rateLimiter.js` | Per-device packet rate limiting |
| `index.js` | Module exports + `executeAction()` dispatcher |

## Usage

```js
const { DynamicThreatResponseEngine, ActionExecutor, executeAction } = require('./src/policy');

const executor = new ActionExecutor({
  sessionManager,   // from Gateway Module 2
  cryptoEngine,     // from Gateway Module 2
  entropyPool,      // from Module 1 (C++ entropy pool wrapper)
  webSocketServer,  // from Gateway Module 2
  deviceRegistry,   // from Gateway Module 2
  blocklist: new Blocklist(),
  rateLimiter: new RateLimiter(),
  logger: myLogger,
});

const engine = new DynamicThreatResponseEngine();

// Called after the Gateway receives a threat assessment from the ML Service:
const actions = engine.evaluate(threatData, sessionContext);
for (const action of actions) {
  await executeAction(executor, action);
}
```

## Integration note

This module (Module 3) does **not** implement the Gateway's Session Manager,
Crypto Engine, WebSocket Server, Device Registry, or Entropy Pool — those
belong to Module 1 (Entropy Pool) and Module 2 (KAIROS Gateway). `actions.js`
is built with dependency injection specifically so it can be developed,
tested, and integrated independently of those modules being finished first.
Every dependency has a documented minimal interface at the top of
`actions.js`, and safe fallbacks are used for anything missing (e.g. it
falls back to Node's `crypto.randomBytes()` if no entropy pool is wired in).

## Testing

```bash
npm test               # run all Jest unit tests
npm test -- --coverage
```

Covers: `engine.test.js` (decision logic — level actions, attack-specific
actions, full-reset override, confidence gate), `actions.test.js` (all 8
action functions against mocked Gateway dependencies), `blocklist.test.js`,
`rateLimiter.test.js`.
