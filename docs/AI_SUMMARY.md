# KAIROS — Comprehensive AI-Readable Project Summary

> **Purpose:** This document is the master context file for KAIROS. It is intended for consumption by AI models, technical reviewers, and architects who need a complete understanding of the system without access to individual specification files. It describes the complete system design — hardware, cryptographic, gateway, ML, policy engine, and frontend — as a unified architecture.

---

## A. Project Overview

### Identity

**KAIROS** (*Adaptive AI-Powered Cryptographic Gateway Platform*) is a modular, edge-deployable security platform combining hardware-harvested pseudo-quantum entropy, AES-256-GCM cryptographic session management, ML-based network anomaly detection, and an adaptive policy engine into a unified system. The full title is the *Adaptive AI-Powered Cryptographic Gateway Platform*.

### Core Problem and Motivation

Conventional IoT and edge communication security relies on software-only random number generators for session key material and static, signature-based intrusion detection systems that are ineffective against zero-day and novel attack patterns. KAIROS addresses both limitations simultaneously:

1. **Entropy quality:** Software CSPRNGs seeded by OS entropy pools offer weaker guarantees than physical randomness sources. KAIROS harvests photon shot noise from reverse-biased photodiodes to produce hardware-origin random material for all cryptographic key operations.
2. **Threat detection:** Static rule-based or signature-based systems cannot detect unknown attacks. KAIROS uses unsupervised anomaly detection (Isolation Forest) trained on benign traffic to flag behavioral deviations without requiring labeled attack signatures.
3. **Adaptive response:** Most systems detect threats but respond with a fixed, blunt policy. KAIROS's Dynamic Threat Response Engine (DTRE) selects and executes tiered, proportionate security actions based on both threat score and confidence.

### Primary Objectives

- Provide cryptographically strong, hardware-seeded randomness for all session and key operations.
- Mediate and secure all IoT device communication through an authenticated gateway.
- Continuously evaluate network traffic for anomalies using lightweight ML inference (~2 ms per call).
- Automatically enforce adaptive security policies in proportion to detected threat level.
- Operate entirely headless as a set of REST/WebSocket services, with an optional SOC dashboard.

### Differentiating Characteristics

- **Pseudo-quantum entropy at the physical layer:** Random material originates from optical shot noise (photodiode + LM358 op-amp differential circuit on an ESP32), not OS or hardware RNG fallbacks. The "pseudo-quantum" characterization is technically precise: photon arrival statistics are quantum-mechanical, but unavoidable thermal and amplifier noise coexist in the signal, making it a hybrid rather than an idealized pure QRNG.
- **End-to-end entropy chain:** The entropy pipeline runs from physical quantum photon statistics → 4-bit differential extraction → UART framing → host-side whitening (Galois LFSR or SHA-256) → C++ circular FIFO pool → cryptographic key material. Every cryptographic primitive derives its randomness from this chain.
- **Unsupervised zero-day-capable detection:** The Isolation Forest model requires no labeled attack data at training time. It learns the distribution of benign traffic and scores any deviation, enabling detection of previously unseen attack patterns.
- **Layered response architecture:** Threat detection (Python/ML) and security response (Node.js/DTRE) are strictly separated services. The ML service makes no security decisions; the DTRE makes no ML inferences. This separation is enforced at the API boundary.

---

## B. Overall System Architecture

KAIROS is organized into four numbered modules that interact through explicit API contracts:

```
[ESP32 Hardware — GPIO 34/35 Continuous DMA ADC @ 60 kHz]
        │  UART 921,600 baud — 66-byte frames [0xAA][0x55][64B]
        ▼
[Module 1: C++ Entropy Module — EntropyModule.exe — :8080]
        │  REST: /entropy, /keygen, /encrypt, /decrypt, /test, /status
        │  Subprocess pipe (stdin/stdout JSON)
        ├──────────────────────────────► [Python NIST STS Analyzer]
        │
        │  HTTP POST /api/threat/analyze (localhost only)
        ├──────────────────────────────► [Module 3: Python ML Service — :5000]
        │                                       │
        │◄───── { threat_score, level,           │
        │         confidence, attack_label } ────┘
        │
        │  Policy dispatch (to Node.js DTRE)
        ├──────────────────────────────► [Module 3: Node.js DTRE — :3000]
        │
        │  WebSocket + REST
        └──────────────────────────────► [Optional: Module 4 — React SOC Dashboard]
```

### Module Responsibilities

| Module | Name | Runtime | Port | Responsibility |
|--------|------|---------|------|----------------|
| **Module 1 (HW)** | ESP32 Firmware | Arduino C++ on ESP32 | UART | Raw entropy harvesting and framing |
| **Module 1 (SW)** | Entropy Pool & Crypto Core | C++17 | 8080 | Entropy accumulation, whitening, AES-256-GCM, key generation, REST API |
| **Module 2** | Gateway Router & Session Manager | C++ / Node.js (spec) | 3000 | Device registration, session lifecycle, traffic monitoring, ML+DTRE orchestration |
| **Module 3 (ML)** | ML Threat Detection Service | Python 3.11 / FastAPI | 5000 | Network feature evaluation → threat score, level, confidence, attack label |
| **Module 3 (DTRE)** | Dynamic Threat Response Engine | Node.js (ESM/CommonJS) | (embedded in Gateway) | Policy evaluation → ordered action descriptors |
| **Module 4** | SOC Dashboard | React 18 / Vite | (static) | Security operations visualization and administrative interface |

### Inter-Module Trust Boundaries

- The ML service (`127.0.0.1:5000`) accepts connections from localhost only. No authentication is required between the gateway and ML service; the trust boundary is the loopback interface.
- The Entropy Module (`0.0.0.0:8080`) exposes its REST API to any configured client. The Gateway is its primary consumer.
- The DTRE is a pure decision logic library that receives ML output and returns action descriptors; it does not independently network-connect.
- The frontend never communicates with the ML service or entropy module directly; all communication routes through the gateway layer.

---

## C. Hardware and Entropy Generation

### Physical Architecture

The entropy subsystem consists of two reverse-biased photodiodes connected to an ESP32 development board via an LM358 dual op-amp. The LM358 provides differential amplification and a stable voltage reference (`Vref`). The two photodiode channels are connected to ESP32 GPIO 34 (`ADC1_CHANNEL_6`) and GPIO 35 (`ADC1_CHANNEL_7`).

### Sampling Methodology

The ESP32 operates its hardware SAR ADC1 in **Continuous DMA Mode** at 60 kHz. This entirely eliminates CPU-polling (`analogRead()`) latency (~45 µs per call). The DMA engine fills a 256-byte frame buffer autonomously; the CPU reads completed frames from RAM. Both channels are sampled alternately; when a matched pair of readings arrives:

```
noiseNibble = (val1 XOR val2) AND 0x0F
```

This 4-bit differential extraction exploits the statistical independence of shot noise across the two photodiodes. Two nibbles are combined into one byte. When 64 bytes accumulate, a 66-byte framed packet is transmitted:

```
Frame: [0xAA][0x55][64 bytes raw payload]
```

Output rate: 921,600 baud (8N1), yielding continuous entropy delivery to the host. A button on GPIO 25 provides hardware pause/resume with 200 ms debounce; during pause, the DMA buffer is actively drained to prevent stale data accumulation. Diagnostic LEDs on GPIO 4 (green) and GPIO 5 (red) tap the stream without additional ADC reads.

### Host-Side Entropy Pipeline

The C++ `SerialEntropySource` class (using Boost.Asio for cross-platform async serial I/O) receives the 66-byte frames from the ESP32. On Windows, `DTR/RTS` lines are explicitly disabled to prevent the ESP32 auto-reset triggered by serial port open/close events. Port health is checked non-invasively via `ClearCommError` (Windows `COMSTAT.cbInQue`) to avoid resetting the hardware.

Upon receiving each frame, the 64-byte payload is whitened by one of two algorithms selected at runtime:

| Whitening Algorithm | Mechanism | Output Rate |
|--------------------|----------|------------|
| **Galois LFSR** (default) | 32-bit LFSR, polynomial `0x80200003` (taps 32/22/2/1), XOR-mask each payload byte | ~14.7 KB/s |
| **NIST SP 800-90B SHA-256** | SHA-256 applied per 64-byte block → 32 whitened output bytes | ~7.3 KB/s |

Whitening algorithm selection is switchable at runtime via `POST /settings`. The LFSR seed is `0x01` (non-zero initial state). SHA-256 whitening reduces throughput by half but satisfies NIST SP 800-90B conditioning requirements more rigorously.

### Entropy Pool

The `EntropyPool` is an 8,192-byte thread-safe circular FIFO implemented in C++. A background collection thread continuously draws from the active `IEntropySource`, using a refill threshold of 4,096 bytes. When the pool reaches capacity, new bytes overwrite old ones (circular overwrite), preventing serial backpressure. The pool maintains a 1-second sliding-window throughput metric (`current_speed_bps`).

### Fallback Architecture

If the ESP32 is unplugged, paused, or communication is interrupted for more than 1,200 ms, `SerialEntropySource::isAvailable()` returns `false` and `EntropyCollector` automatically switches to `OpenSSLEntropySource` (backed by `RAND_bytes()`). Reconnect hysteresis prevents flip-flopping. This ensures uninterrupted gateway operation without physical hardware.

### Statistical Validation

The Python `analyzer/` module provides three test modes against the entropy stream:

- **Quick mode:** 5 fundamental tests — Monobit Frequency, Block Frequency, Runs, Longest Run of Ones, Byte Distribution.
- **Full mode:** 8 NIST SP 800-22 tests including Serial, Approximate Entropy, and Cumulative Sums.
- **Exhaustive mode:** 100 independent runs per test; evaluates per-test p-value proportions (pass rate ≥ 0.96 using chi-square goodness-of-fit) and 10-bin p-value uniformity (Kolmogorov-Smirnov, threshold p_T ≥ 0.0001), as specified in NIST SP 800-22 Section 4.2.2.

The analyzer is invoked as a subprocess; the C++ backend pipes raw bytes to `stdin` and receives JSON-structured results on `stdout`.

---

## D. Cryptographic Architecture

All cryptographic operations are implemented in C++17 using the OpenSSL library (EVP API).

### AES-256-GCM Authenticated Encryption

The `Encryptor` class implements AES-GCM for all payload encryption. Key characteristics:

- **Mode:** AES-256-GCM (32-byte key, 12-byte recommended IV, 16-byte authentication tag).
- **IV generation:** `RAND_bytes()` from OpenSSL (or the entropy pool, depending on context). Each encryption call generates a fresh IV.
- **AAD support:** Additional Authenticated Data can be passed to bind context metadata to the ciphertext without encrypting it.
- **Tag verification:** `EVP_DecryptFinal_ex()` enforces authentication tag verification atomically on decryption. Authentication failure throws immediately.
- **Key sizes:** Supports 128-bit (16B), 192-bit (24B), and 256-bit (32B) keys, though 256-bit is the configured default.

The REST API exposes encrypt/decrypt via `POST /encrypt` and `POST /decrypt`, accepting Base64-encoded data and hex-encoded keys.

### Key Generation

The `KeyGenerator` class provides:

- **Random key generation:** Draws 32 bytes directly from the `EntropyPool` (hardware-seeded when available; OpenSSL fallback otherwise). Used for AES session keys and IV generation.
- **Password-based key derivation:** PBKDF2-HMAC-SHA256 via `PKCS5_PBKDF2_HMAC()`. Default: 100,000 iterations, 32-byte output, minimum 16-byte salt. Used for passphrase-derived keys exposed via the `/keygen` endpoint with `type=password`.
- **Salt and nonce generation:** Also sourced from the entropy pool.

### SHA-256 Hashing

The `Hasher` class provides SHA-256 digest computation via OpenSSL for checksum verification and data integrity.

### RSA-Based Key Exchange (Gateway Layer)

The Gateway specification (documented in `docs/Gateway.docx`) defines **RSA-OAEP** for the session key exchange ceremony:

- **RSA key size:** 2048 bits; padding: OAEP with SHA-256.
- Each IoT device registers with the gateway, submitting its RSA public key (PEM format).
- When a session is established, the gateway generates a fresh 32-byte AES-256 session key using the entropy pool (`get_random_bytes(32)` via ffi-napi) and encrypts it separately for each endpoint using their respective RSA public keys.
- The 12-byte AES-GCM nonce is also sourced from the entropy pool per operation (`get_random_bytes(12)`).
- The `ActionExecutor.rotateSessionKey()` method re-performs this exchange during key rotation: it draws 32 fresh entropy bytes, RSA-encrypts them for both the source and target device using the Node.js built-in `crypto` module, updates the session table, and delivers the new key via WebSocket notifications.

### Session Key Lifecycle

Session keys carry threat-level-dependent lifetimes managed by the DTRE:

| Threat Level | Default Session Key Lifetime |
|-------------|------------------------------|
| GREEN | 300 seconds |
| YELLOW | 300 seconds |
| ORANGE | 60 seconds |
| RED | 30 seconds |

Key rotation is triggered by the DTRE's `rotate_key` action (on ORANGE or RED). At extreme threat (score > 0.85, level = RED), a full session reset terminates all sessions for the offending devices and clears their device registry entries.

---

## E. Gateway Architecture

The Gateway is the central mediation and security boundary layer of KAIROS.

### Specification vs. Current State

The Gateway is fully specified in `docs/Gateway.docx` and its architecture is reflected in the DTRE codebase (`nodejs/src/policy/`), configuration (`nodejs/config/config.json`), and the REST API contract defined by the Entropy Module (`backend/src/WebServer.cpp`). The C++ Entropy Module (`EntropyModule.exe`) functions as the embryonic gateway service on port 8080, providing entropy and crypto endpoints. The full gateway routing, session management, and inter-module wiring constitutes the next development phase.

### Runtime and Framework

The Gateway runs on **Node.js v18+ LTS** (port 3000), combining HTTP REST endpoints with WebSocket for real-time device communication. The framework stack is **Express.js v4.x** for the REST layer and **ws v8.x** for the WebSocket server — both served on a single port 3000. The C++ Entropy Pool is integrated into the Node.js process via **ffi-napi** (Foreign Function Interface), calling the exported `get_random_bytes(uint8_t* buffer, size_t len)` C function from the compiled shared library. Its architecture integrates with the C++ Entropy Module on port 8080 and the Python ML service on port 5000.

### REST API Surface

| Method | Path | Function |
|--------|------|----------|
| `GET` | `/health` | Service health probe |
| `GET` | `/status` | Entropy pool metrics, whitening algorithm, speed, source type |
| `GET` | `/entropy` | Raw random bytes (query: `?bytes=N`, response: Base64 JSON) |
| `POST` | `/keygen` | AES key generation (random or PBKDF2) |
| `POST` | `/encrypt` | AES-256-GCM encryption |
| `POST` | `/decrypt` | AES-256-GCM decryption |
| `POST` | `/test` | Trigger NIST STS entropy statistical test |
| `GET/POST` | `/settings` | Read/write whitening algorithm and entropy source settings |
| `POST` | `/reseed` | Force immediate entropy pool replenishment |
| `POST` | `/api/device/register` | IoT device registration with public key |
| `POST` | `/api/session/request` | Session key negotiation and challenge-response |

### WebSocket Communication

The Gateway uses WebSocket for real-time, bidirectional device communication. Key messages include `key_rotate` (delivering RSA-encrypted new session key), `reauthentication_required` (terminating session and demanding re-authentication), and `admin_alert` (pushing threat notifications to the SOC dashboard).

### Device Registration and Session Management

Devices authenticate via a challenge-response protocol using their registered RSA public key. The gateway maintains a device registry (`nodejs/data/devices.json`) and a session store tracking per-session context including: `sessionId`, `sourceDevice`, `targetDevice`, `sourceIP`, `targetIP`, `sourcePublicKey`, `targetPublicKey`, `rotationCount`, and expiry timestamp.

Session cleanup runs every 60 seconds to remove expired sessions (default max lifetime: 3,600 seconds). On DTRE escalation, session lifetime is reduced dynamically.

### Traffic Monitoring and Feature Extraction

For each message transiting the gateway, it extracts an 8-dimensional feature vector from network flow metadata (not payload content) and forwards it to the ML service. The feature extraction pipeline produces the exact feature schema consumed by the ML model (see Section F).

### ML Service Integration

The gateway sends `HTTP POST http://127.0.0.1:5000/api/threat/analyze` with the flow feature vector for each evaluated session. If the ML service is unavailable (timeout: 1,000 ms), a `fallback_score` of 0.02 is used. After three consecutive failures (`degradation_threshold: 3`), the gateway enters a degraded-ML mode. The ML service response (`threat_score`, `level`, `confidence`, `attack_label`) is passed directly to the DTRE `evaluate()` function.

### DTRE Integration

The gateway calls `engine.evaluate(threatData, sessionContext)` and receives an ordered array of action descriptors. It then invokes the corresponding `ActionExecutor` methods against its own session manager, crypto engine, entropy pool, WebSocket server, device registry, blocklist, and rate limiter subsystems.

### Blocklist and Rate Limiting

- **IP Blocklist (`Blocklist` class):** JSON-persisted map of blocked IPs with expiry timestamps. Default block duration: 7,200 seconds (2 hours). Auto-expiry is checked lazily on `isBlocked()` calls.
- **Rate Limiter (`RateLimiter` class):** Per-device in-memory token counter with 1-second rolling window. Default limit: 100 packets/second when rate limiting is active.

---

## F. Machine Learning Architecture

### Service Overview

The ML Threat Detection Service is a stateless FastAPI microservice running on Python 3.11 at `127.0.0.1:5000`. It is loaded once at startup with pre-trained serialized artifacts (`isolation_forest.pkl`, `scaler.pkl`, `metadata.json`) and performs inference synchronously per request.

### Dataset

The model targets the **CIC-IDS 2017** (Canadian Institute for Cybersecurity Intrusion Detection System 2017) dataset as its primary training corpus. The training pipeline filters to benign traffic only for unsupervised model fitting. Since the full CIC-IDS 2017 dataset requires external download, the training script also includes a synthetic benign traffic generator (`generate_synthetic_benign()`) for development. The shipped pre-trained model was trained on 40,000 synthetic benign samples (80/20 train/validation split, `random_state=42`).

### Feature Schema

The ML model operates on an 8-dimensional feature vector:

| Feature | Type | Description |
|---------|------|-------------|
| `data_length` | int [0, 65535] | Plaintext payload size in bytes |
| `protocol_encoded` | int {0,1,2,3} | Transport protocol: WebSocket=0, TCP=1, UDP=2, ICMP=3 |
| `packet_rate` | float ≥ 0 | Packets per second (rolling window measurement) |
| `avg_packet_size` | float [0, 65535] | Running average packet size in bytes |
| `destination_diversity` | int [0, 255] | Count of unique target devices in the session |
| `failed_logins` | int ≥ 0 | Failed authentication attempts in the session |
| `session_duration` | float ≥ 0 | Session duration in seconds |
| `total_packets` | int ≥ 0 | Total packet count in the session |

The gateway sends raw (unnormalized) features. The ML service applies a pre-fitted `StandardScaler` before passing the vector to the model. The canonical feature order is strictly fixed in `constants.FEATURE_ORDER`.

### Model Architecture: Isolation Forest

The primary detection model is **Isolation Forest** from scikit-learn, configured as:

- `n_estimators`: 100 trees
- `contamination`: 0.01 (1% expected anomaly rate in training data)
- `max_samples`: auto (subsampling per tree)
- `bootstrap`: False
- `random_state`: 42

Isolation Forest is trained exclusively on benign traffic. Anomalies (attacks) are detected by their isolation characteristic: they require fewer splits to isolate from the rest of the dataset, producing a more negative `score_samples()` output. No labeled attack data is used during training, enabling the model to detect zero-day attacks and novel traffic patterns that do not match known signatures.

### Score Calibration and Threat Score Computation

The raw Isolation Forest `score_samples()` output clusters in a narrow negative range (e.g., −0.7 to −0.3 for the trained model), making naive sigmoid conversion unreliable. KAIROS uses a calibration-based normalization:

```
threat_score = clip((score_median − raw_score) / (score_median − score_p01), 0.0, 1.0)
```

Where `score_median` and `score_p01` are computed from benign training data at training time and stored in `metadata.json`. For the shipped model: `score_median = −0.4287`, `score_p01 = −0.6850`. This mapping produces `threat_score ≈ 0` for typical benign traffic and `threat_score → 1.0` for maximally anomalous traffic.

**Confidence** is computed analogously as the absolute distance of `raw_score` from `score_median`, normalized by the same spread. Confidence is used by the DTRE's confidence-gating logic to modulate the `block_ip` action.

### Threat Level Classification

The calibrated `threat_score` [0.0, 1.0] is mapped to a discrete security level:

| Level | Score Range | Semantic Meaning |
|-------|------------|----------------|
| **GREEN** | [0.00, 0.25] | Normal / benign traffic |
| **YELLOW** | (0.25, 0.50] | Low-level anomaly; elevated monitoring |
| **ORANGE** | (0.50, 0.75] | Moderate threat; active countermeasures |
| **RED** | (0.75, 1.00] | Severe threat; aggressive response |

Threshold values are configurable at runtime via `nodejs/config/config.json`.

### Rule-Based Attack Labeling

Complementing the unsupervised Isolation Forest, a deterministic rule-based layer applies heuristic labels based on feature thresholds (execution order: DOS → BRUTE_FORCE → PORT_SCAN → None):

| Label | Triggering Condition | Default Threshold |
|-------|---------------------|-------------------|
| `DOS` | `packet_rate > dos_threshold` | 1,000 pkt/s |
| `BRUTE_FORCE` | `failed_logins > bruteforce_threshold` | 10 attempts |
| `PORT_SCAN` | `destination_diversity > portscan_threshold` | 50 unique targets |

Labels are only applied if `threat_score > activation_threshold` (default 0.50) to prevent false labels from triggering attack-specific DTRE actions on borderline flows.

### XGBoost / Hybrid Architecture

The ML architecture documentation identifies **XGBoost** as the supervised component of an extended hybrid detection architecture. In this hybrid design:

- The Isolation Forest handles unsupervised anomaly detection for zero-day threats (no labeled attack data needed).
- XGBoost provides supervised classification for known labeled attack types using the CIC-IDS 2017 full labeled dataset (labels: DOS, BRUTE_FORCE, PORT_SCAN, BOTNET, BENIGN).
- XGBoost parameters: `n_estimators=100, max_depth=6, learning_rate=0.1, objective='multi:softprob'`.
- Logic: If XGBoost confidence > 0.5 → use XGBoost label; otherwise fall back to rule-based labeling.
- Combined threat score: `max(isolation_score, xgboost_confidence)` when XGBoost is highly confident (> 0.8).
- The protocol encoding constant in `constants.py` explicitly states: `# must stay consistent with Gateway + XGBoost (Section 4.7)`.

Both models operate on the same 8-feature vector, ensuring the feature pipeline serves both unsupervised and supervised detection layers without modification.

### Inference Performance

Model inference time is measured per request. With the pre-trained model on synthetic data, inference time is approximately 2 ms per request, suitable for inline gateway operation.

### Performance Targets (per technical specification)

- False positive rate on benign validation data: < 5% (achieved: 4.49% on training set)
- Anomaly detection rate on synthetic attack traffic: > 90%
- Attack label precision targets: DOS > 0.90, BRUTE_FORCE > 0.85, PORT_SCAN > 0.80
- ML service throughput: > 100 requests/second
- ML service inference latency: < 10 ms per request (achieved: ~2 ms)
- ML service memory footprint: < 200 MB
- Trained model artifact size: < 20 MB
- DTRE decision latency (policy evaluation): < 1 ms
- DTRE memory footprint: < 50 MB

### API Contract

```
POST http://127.0.0.1:5000/api/threat/analyze
Body: {
  "session_id": string,
  "source": string,
  "target": string,
  "plaintext": string,    // metadata-only; never logged or persisted
  "features": ThreatFeatures
}
Response: {
  "threat_score": float [0.0, 1.0],
  "level": "green"|"yellow"|"orange"|"red",
  "confidence": float [0.0, 1.0],
  "attack_label": "DOS"|"BRUTE_FORCE"|"PORT_SCAN"|null,
  "timestamp": ISO8601
}
```

The `plaintext` field is present in the schema for potential future NLP analysis but is explicitly never logged, persisted, or included in error traces (strict memory hygiene, Section 11 of the spec).

---

## G. Dynamic Threat Response Engine (DTRE)

### Architecture Position

The DTRE is a pure decision logic module implemented in Node.js (`nodejs/src/policy/engine.js`). It operates as a library within the Gateway process, not as a separate service. It receives the ML service's `threatData` and the gateway's `sessionContext` and returns an ordered array of action descriptors. It makes no network calls itself.

### Decision Priority

The DTRE evaluates threat data through a four-priority decision cascade:

**Priority 1 — Full Session Reset (Nuclear Option)**
- Condition: `threat_score > 0.85 AND level === "red"`
- Action: `full_session_reset` — immediately terminates all sessions for both involved devices, adds both IPs to the blocklist, and clears their device registry entries.
- This overrides all other actions.

**Priority 2 — Level-Based Actions**

| Level | Actions |
|-------|---------|
| GREEN | None |
| YELLOW | `verbose_logging` |
| ORANGE | `rotate_key`, `reduce_lifetime` (60s), `alert_admin` |
| RED | `rotate_key`, `rate_limit` (100 pkt/s), `force_re_auth`, `alert_admin_urgent` |

**Priority 3 — Attack-Specific Actions (Conditional)**
Applied only when `attack_label ≠ null AND threat_score > 0.50`:

| Attack Label | Actions |
|-------------|---------|
| DOS | `block_ip` (2h default) |
| BRUTE_FORCE | `lock_account`, `block_ip` |
| PORT_SCAN | `block_ip`, `verbose_logging` |

The DTRE defensively re-checks the activation threshold even if the ML service already enforced it, as a fail-safe.

**Priority 4 — Confidence-Gated RED Override**
When `level === "red" AND threat_score > 0.75`:
- `confidence < 0.20`: Remove `block_ip` from the action set (even if an attack-specific rule added it).
- `confidence ≥ 0.20`: Add `block_ip` to the action set (even with no `attack_label`).

This mechanism prevents aggressive IP blocking on uncertain high-score detections while ensuring high-confidence RED anomalies are blocked regardless of label assignment. This feature is configurable (`confidence.enabled` in config; defaults to `false` in the current shipped configuration).

### Action Execution

The `ActionExecutor` class receives dependency-injected references to the gateway's session manager, crypto engine, entropy pool, WebSocket server, device registry, blocklist, and rate limiter. Each action type has a concrete execution method:

| Action | Execution |
|--------|----------|
| `rotate_key` | Draws 32 fresh entropy bytes, RSA-encrypts for both devices, notifies via WebSocket |
| `reduce_lifetime` | Updates session expiry to `now + lifetimeSeconds` |
| `rate_limit` | Sets per-device packet limit in the `RateLimiter` |
| `force_re_auth` | Terminates the session and sends `reauthentication_required` WebSocket message |
| `block_ip` | Adds IP to persistent `Blocklist` with expiry, terminates all sessions from that IP |
| `lock_account` | Terminates all sessions for the source device |
| `alert_admin` | Logs structured JSON alert and notifies the dashboard WebSocket channel (`__dashboard__`) |
| `verbose_logging` | Elevates session log level to debug |
| `full_session_reset` | Terminates all sessions for both devices, blocks both IPs, clears registry entries |

On `rotate_key` failure (entropy unavailable), one automatic retry is attempted. On persistent failure, the error is logged and the action returns `{ success: false }` rather than crashing the gateway.

### Deduplication

After merging level-based and attack-specific actions, duplicate action types are removed (last occurrence wins, first-appearance order preserved), preventing double-execution of e.g. `rotate_key` when both level and attack label policies request it.

---

## H. Frontend and Reporting Architecture

### Technology Stack

The KAIROS frontend is a modern Security Operations Center (SOC) dashboard implemented in:

- **React 18** with functional components and hooks
- **TypeScript** for full type coverage of gateway data schemas
- **Vite** as the build and development server
- **Tailwind CSS** for utility-first styling (dark slate/cyan aesthetic)
- **Recharts** for time-series threat score visualization (AreaChart)
- **Framer Motion** for animated event feed transitions
- **xterm.js** for the embedded admin terminal console
- **Zustand** (`useDashboardStore`) for reactive global state management

A legacy SPA also exists in `frontend/js/` (vanilla JavaScript, Bootstrap 5) with separate panels for Encrypt/Decrypt, KeyGen, Settings, and Entropy Test operations against the entropy module REST API.

### Dashboard Layout and Panels

The React SOC dashboard (`frontend/src/App.tsx`) presents the following panels:

1. **Metric Cards (4-grid):** Live displays of current threat score, entropy pool available bytes + source type, active session count, and total request count. Polls the gateway `/status` endpoint every 15 seconds.
2. **Threat Telemetry Chart:** Recharts AreaChart showing `threat_score` over time with gradient fill. Domain [0, 1] with time-series x-axis.
3. **Threat Posture Panel:** Current risk level badge (GREEN/YELLOW/ORANGE/RED), color-coded progress bar from emerald → rose, and status mini-cards for Gateway, ML model, entropy source, and active threat event count.
4. **Live Event Feed:** AnimatePresence-animated cards showing recent `ThreatEvent` objects (type, level badge, source, target, score, confidence, timestamp, description).
5. **Security Actions Panel:** DTRE action queue display showing action type, severity, target, status, and summary.
6. **Session Matrix:** Tabular view of active sessions with `sessionId`, device, status (active/isolated/rotating/blocked), key age in seconds, and threat score.
7. **Topology Visualization:** SVG-based network topology diagram showing the Gateway, ML, DTRE, QRNG sensor, and connected device nodes with directional connection lines weighted by signal strength.
8. **Device Health Panel:** Per-device status cards showing entropy percentage, latency (ms), and risk score.
9. **Admin Terminal:** `xterm.js` terminal emulator embedded in the dashboard for gateway command interaction.

### Data Model

The TypeScript type system (`frontend/src/types.ts`) defines the full dashboard snapshot contract:

```typescript
interface DashboardSnapshot {
  status: GatewayStatus;       // entropy pool state, server metrics
  threatHistory: ThreatPoint[];// time-series threat scores
  events: ThreatEvent[];       // detected anomalies with scores
  sessions: SessionEntry[];    // active cryptographic sessions
  devices: DeviceHealth[];     // per-device entropy/latency/risk
  actions: ActionEntry[];      // DTRE policy action queue
  topology: TopologyNode[];    // network graph nodes
  connections: ConnectionEdge[];// topology edges with strength
}
```

### Offline Fallback

A complete mock state store (`frontend/src/mockData.ts`) provides realistic placeholder data for all dashboard panels when the backend is unavailable. This allows frontend development and demonstration independent of the live gateway.

### Admin Authentication

The SOC dashboard's administrative interface is protected by **JWT (JSON Web Token)** authentication:

- **Login endpoint:** `POST /api/admin/login` — accepts `{ username, password }` JSON body.
- **Token expiry:** 10,800 seconds (3 hours).
- **Token storage:** `localStorage` in the browser; all subsequent admin API calls carry the token as an `Authorization: Bearer <jwt>` header.
- **v1 default credentials:** `admin` / `changeme` (hardcoded; rotation deferred to v2).
- **Rate limit:** 5 login attempts per 5-minute window per IP.

All admin-only endpoints (`/api/admin/*`, `/api/dashboard/chat`, `/api/dashboard/report`) require a valid JWT. The public SOC dashboard panels (threat telemetry, event feed, session matrix) are accessible without authentication.

### Admin Terminal

The embedded `xterm.js` terminal console is a **button-driven interface, not a keyboard-input command shell**. All commands are issued via button clicks that map to specific gateway API calls:

| Button | API Call | Effect |
|--------|----------|--------|
| Rotate Key | `POST /api/admin/rotate-key` | Triggers DTRE `rotate_key` for the selected session |
| Block IP | `POST /api/admin/block-ip` | Adds an IP to the blocklist |
| Unblock IP | `POST /api/admin/unblock-ip` | Removes an IP from the blocklist |
| Force Re-Auth | `POST /api/admin/force-reauth` | Sends `reauthentication_required` WebSocket message |
| Clear Blocklist | `POST /api/admin/clear-blocklist` | Removes all non-expired blocklist entries |
| View Sessions | `GET /api/dashboard/sessions` | Refreshes the session matrix panel |
| Generate Report | `POST /api/dashboard/report` | Invokes the report generation engine |
| Run NIST Test | `POST /test` (entropy module) | Triggers NIST STS statistical test run |

This design prevents command injection by eliminating free-text terminal input entirely.

### Security Assistant Chatbot

The gateway exposes a rule-based chatbot at `POST /api/dashboard/chat` (requires JWT). It performs intent classification on the operator's natural-language query and returns structured text responses with a target latency of **< 10 ms**.

The chatbot recognizes **14 intent categories**:

| Intent | Example Query | Response |
|--------|--------------|---------|
| `THREAT_LEVEL` | "What is the current threat level?" | Current level badge + score |
| `THREAT_HIGHEST` | "What was the highest threat today?" | Peak score + timestamp |
| `SHOW_SESSIONS` | "Show active sessions" | Active session count + IDs |
| `DEVICE_STATUS` | "Is device X online?" | Per-device health status |
| `BLOCKLIST_COUNT` | "How many IPs are blocked?" | Blocklist entry count |
| `BLOCKLIST_SHOW` | "Show the blocklist" | List of blocked IPs + expiry |
| `SHOW_ACTIONS` | "What actions were taken?" | Recent DTRE action summary |
| `LOGS_SESSION` | "Show logs for session Y" | Session-scoped log excerpt |
| `LOGS_TIME` | "Show logs from 2 PM" | Time-scoped log query |
| `SYSTEM_STATUS` | "Is the ML service healthy?" | Service health check summary |
| `UNBLOCK_IP` | "Unblock 192.168.1.5" | Disclaimer + confirmation prompt |
| `ROTATE_KEY` | "Rotate the key for session Z" | Disclaimer + confirmation prompt |
| `HELP` | "What can you do?" | Intent catalog listing |
| `FALLBACK` | (unrecognized) | Reprompt with suggestion |

`UNBLOCK_IP` and `ROTATE_KEY` respond with a disclaimer and require confirmation before executing a state-changing action.

**v2 chatbot architecture:** Replace the rule-based intent matching with **Qwen2.5-3B** (3-billion-parameter LLM) served via **Ollama** on the gateway host, enabling free-form semantic query understanding.

### Report Generation Engine

The gateway exposes `POST /api/dashboard/report` (requires JWT) which produces template-based natural language security reports. Three report types are supported:

| Report Type | Contents |
|-------------|---------|
| **Session Summary** | Session activity, total packets, unique devices, peak threat score, key rotation events |
| **Incident Report** | Specific detected anomalies, DTRE actions taken, timeline of escalation events |
| **Daily Security Digest** | Aggregate statistics over a 24-hour window, top threat sources, blocklist changes |

Templates use named placeholders populated from gateway log and session data:

```
{{session_id}}   {{source_device}}   {{target_device}}
{{peak_threat}}  {{avg_threat}}      {{action_count}}
{{actions}}      {{recommendations}} {{timestamp}}
{{blocklist_additions}}  {{key_rotation_count}}
```

**Conditional recommendation rules** embedded in the report engine:

| Condition | Recommendation Generated |
|-----------|------------------------|
| `dos_count > 10` | "Review upstream rate limiting; consider ISP-level filtering" |
| `brute_force_count > 5` | "Enforce MFA; rotate device credentials" |
| `avg_threat_score > 0.3` | "Baseline anomaly rate elevated; retrain model on recent traffic" |
| `blocked_ip_count > 10` | "Multiple sources blocked; check for coordinated attack pattern" |

Reports are downloadable in **`.txt`** (plain text) and **`.json`** (structured data) formats.

### WebSocket Event Catalog

The gateway's WebSocket server pushes **8 event types** to the SOC dashboard, enabling real-time panel updates without polling:

| Event Type | Direction | Payload Summary |
|-----------|-----------|----------------|
| `summary_update` | Gateway → Dashboard | Full `DashboardSnapshot` refresh |
| `threat_update` | Gateway → Dashboard | `{ session_id, threat_score, level, confidence, attack_label }` |
| `session_created` | Gateway → Dashboard | New session metadata |
| `session_terminated` | Gateway → Dashboard | Session ID + reason code |
| `action_executed` | Gateway → Dashboard | DTRE action type + target + status |
| `key_rotated` | Gateway → Dashboard | Session ID + rotation count |
| `ip_blocked` | Gateway → Dashboard | IP address + expiry timestamp |
| `health_check` | Dashboard → Gateway | Ping; gateway responds with `{ status: "ok" }` |

**Reconnect strategy:** Exponential backoff starting at 1 second (1s → 2s → 4s → 8s), capped at 30 seconds. Indefinite retry on disconnect.

### 3D Network Topology Visualization

The visualization branch extends the current SVG topology diagram with an interactive **Three.js + ForceGraph3D** force-directed graph:

- **Capacity limits:** Maximum 50 nodes and 100 edges rendered simultaneously.
- **Node encoding:** Node size proportional to active session count; color encodes device status (green = healthy, amber = elevated threat, red = blocked/isolated).
- **Edge encoding:** Edge thickness and color encode per-session threat score (thin/blue = benign, thick/red = high-threat).
- **Performance fallback:** If WebGL frame rate drops below 20 fps, the visualization automatically degrades to a 2D SVG rendering to maintain dashboard usability on low-GPU hosts.

### Communication Architecture

The frontend communicates exclusively with the gateway layer via REST polling and WebSocket. It does not call the ML service or entropy module directly. All data is mediated through the gateway's dashboard API endpoints (`/api/dashboard/summary`, `/api/dashboard/sessions`, `/api/dashboard/blocklist`, etc.).

---

## I. Technology Stack

### Hardware

| Component | Role |
|-----------|------|
| ESP32 Dev Module (Espressif Arduino Core 3.x / ESP-IDF v5) | Entropy harvesting MCU |
| Dual reverse-biased photodiodes (GPIO 34, GPIO 35) | Optical shot-noise source |
| LM358 dual op-amp | Differential amplification, Vref generation |
| USB UART (921,600 baud 8N1) | ESP32-to-host serial link |

### Programming Languages

| Language | Usage |
|----------|-------|
| C++17 | Entropy pool, whitening, crypto engine, REST API, CLI tools |
| Arduino C++ (ESP32) | Hardware firmware (Continuous DMA ADC) |
| Python 3.11+ | ML inference service, NIST STS analyzer, training pipeline |
| TypeScript | SOC dashboard (React), Node.js DTRE, Playwright E2E tests |
| JavaScript (Node.js) | DTRE policy engine, action executor, blocklist, rate limiter |

### Cryptography

| Algorithm/Standard | Usage |
|--------------------|-------|
| AES-256-GCM | All payload authenticated encryption (via OpenSSL EVP) |
| RSA-OAEP | Session key exchange (device registration, key rotation) |
| PBKDF2-HMAC-SHA256 | Password-based key derivation (100,000 iterations) |
| SHA-256 | Entropy whitening (NIST SP 800-90B mode), data hashing |
| Galois LFSR (poly `0x80200003`) | Fast entropy whitening (~14.7 KB/s) |
| NIST SP 800-22 STS | Statistical entropy validation (Monobit through Serial tests) |
| NIST SP 800-90B | Conditioning standard for SHA-256 whitening |

### Machine Learning

| Component | Details |
|-----------|---------|
| Isolation Forest | scikit-learn, 100 estimators, contamination=0.01 |
| StandardScaler | Per-feature Z-score normalization (fitted on benign training data) |
| XGBoost v2 | Supervised hybrid classifier (`n_estimators=100, max_depth=6, learning_rate=0.1, objective='multi:softprob'`) |
| CIC-IDS 2017 | Primary training dataset (benign traffic for unsupervised; full labeled set for XGBoost) |
| CSE-CIC-IDS2018 | Secondary evaluation dataset |
| UNSW-NB15 | Tertiary evaluation dataset |
| TON_IoT | IoT-specific evaluation dataset |
| joblib | Model artifact serialization (`.pkl`) |
| numpy, pandas | Feature processing and data manipulation |
| scipy | Statistical tests in NIST STS analyzer |

### Backend and Gateway

| Technology | Usage |
|-----------|-------|
| FastAPI (uvicorn) | Python ML microservice framework |
| Pydantic v2 | Request/response schema validation |
| cpp-httplib | C++ single-header HTTP server (headless REST) |
| nlohmann/json | C++ JSON parsing |
| Boost.Asio | Cross-platform async serial I/O |
| OpenSSL | AES-GCM, PBKDF2, RAND_bytes, SHA-256 |
| loguru | Structured JSON logging for Python ML service |
| Node.js (CommonJS/ESM) | DTRE policy engine and action executor |
| Express.js v4.x | Gateway REST API framework |
| ws v8.x | Gateway WebSocket server |
| ffi-napi | C++ shared library → Node.js FFI bridge (entropy pool integration) |

### Frontend

| Technology | Usage |
|-----------|-------|
| React 18 | SOC dashboard UI framework |
| Vite | Build tool and dev server |
| Tailwind CSS | Utility-first CSS |
| Recharts | Threat score time-series charts |
| Framer Motion | Animated event feed |
| xterm.js | Embedded admin terminal |
| Zustand | Dashboard state management |
| Bootstrap 5 | Legacy SPA (vanilla JS frontend) |
| Axios v1.6+ | HTTP client for REST API calls |
| React Router v6.4+ | Client-side routing |
| Three.js + ForceGraph3D | 3D force-directed network topology graph (visualization branch) |

### Communication Protocols

| Protocol | Usage |
|---------|-------|
| UART 921,600 baud 8N1 | ESP32 → Host serial entropy stream |
| HTTP/1.1 REST | All inter-service communication (localhost-only for ML) |
| WebSocket | Gateway ↔ IoT device real-time communication |
| stdin/stdout (subprocess pipe) | C++ backend → Python NIST analyzer |

### Storage and Data

| Storage | Content |
|---------|---------|
| In-memory circular FIFO (8,192 bytes) | Entropy pool |
| `config/backend_config.json` | C++ entropy module runtime configuration |
| `nodejs/config/config.json` | DTRE policy thresholds and actions |
| `nodejs/data/devices.json` | Device registry seed data |
| `nodejs/data/blocklist.json` | Persisted IP blocklist |
| `python/models/*.pkl` | Serialized Isolation Forest + StandardScaler |
| `python/models/metadata.json` | Training parameters, calibration points, validation metrics |
| `python/logs/ml_service.log` | Structured JSON ML service log (rotation: 10 MB, retention: 7 days) |

### Testing and Development Tools

| Tool | Usage |
|------|-------|
| pytest | Python ML service unit and integration tests (58 tests) |
| Jest | Node.js DTRE unit tests |
| Playwright | E2E UI audit and navigation tests |
| CMake + vcpkg | C++ build system with dependency management |
| Ninja | C++ build backend |
| EntropyCLI.exe | Interactive C++ CLI test bench (keygen, encrypt, NIST, speedometer) |
| ml_menu.py | Interactive Python ML test bench (preset scenarios, batch simulation) |

---

## J. Security Architecture and Design Principles

### Security Boundaries

The system defines the following trust boundaries:

1. **Physical boundary:** The ESP32 + photodiode circuit is a trusted entropy source. The host trusts the hardware channel (921,600 baud UART) as the primary randomness source.
2. **Loopback boundary:** The ML service accepts connections only from `127.0.0.1`. No external authentication is required; process isolation is the trust mechanism.
3. **Gateway boundary:** The gateway is the single point of entry for external IoT devices. All device communication passes through it; no device can reach the ML service or entropy module directly.
4. **Frontend boundary:** The SOC dashboard communicates only with the gateway API. It has no direct access to backend services.

### Entropy and Cryptographic Security

- All session keys are derived from the hardware entropy pool (ESP32 photon shot noise → whitened → C++ pool → `KeyGenerator`).
- When hardware is unavailable, `OpenSSL RAND_bytes` provides the fallback. This fallback is transparent to callers.
- IVs and nonces are regenerated per operation from the entropy pool.
- AES-256-GCM provides both confidentiality and authenticity; authentication tag failure is a hard error that blocks decryption.
- PBKDF2 with 100,000 iterations provides brute-force resistance for password-derived keys.

### Plaintext Memory Hygiene

The `plaintext` field in `ThreatRequest` is explicitly never logged, stored in any database, or included in error traces. The ML service log entries include only `session_id`, `source`, `target`, `threat_score`, `level`, `attack_label`, `confidence`, and `inference_time_ms` — never payload content.

### Session Security

- Sessions are individually keyed and tracked with rotation counts.
- Key lifetimes are enforced and dynamically shortened under threat escalation.
- Suspicious sessions are isolated (key rotated), rate-limited, forced to re-authenticate, or terminated entirely, in proportion to threat severity.

### Adaptive Defensive Posture

The DTRE's four-priority decision cascade ensures that the system's security posture adapts continuously as threat scores change. The system does not apply a fixed policy; instead, each session's policy is evaluated at every ML inference cycle. The confidence gate prevents over-aggressive blocking when the ML model is uncertain.

### Authentication and Authorization

- **Device authentication:** RSA challenge-response at registration and session establishment.
- **Session token validation:** Each message is associated with a validated `sessionId`.
- **Admin interface:** The SOC dashboard is an operator interface; its chatbot and report endpoints require authentication (per the frontend specification).

### Documented Security Constraints and Assumptions

1. The ML service's localhost-only trust model assumes that process isolation on the gateway host is sufficient. A compromised gateway process could make arbitrary ML service calls.
2. The Isolation Forest's false positive rate target is < 5%. Under adversarial traffic that mimics benign patterns, detection rates may be lower.
3. The confidence gate (`min_confidence_for_block: 0.20`) is disabled by default. Enabling it changes the `block_ip` behavior for RED-level anomalies.
4. RSA public key registration is specified but its revocation and rotation mechanism is not detailed in the current specification.
5. The blocklist uses lazy expiry checking (on `isBlocked()` calls), not a background timer. IPs may remain in the in-memory map until the next check.

---

## K. Project Novelty and Differentiation

### Technical Contributions

KAIROS's novelty resides in the **integration** of four distinct technical domains into a single coherent system:

1. **Photon-shot-noise entropy at the edge:** The use of dual-photodiode optical shot noise as a hardware entropy source for an edge IoT security gateway is novel in the context of lightweight, commodity-hardware deployments. While QRNGs exist in high-end HSMs, KAIROS demonstrates the same principle using off-the-shelf ESP32 hardware and passive optical components. The "pseudo-quantum" characterization is technically accurate and honestly represents the unavoidable classical noise floor.

2. **End-to-end entropy chain integration:** KAIROS creates a continuous provenance chain from physical quantum effects through host-side statistical whitening (dual algorithm: LFSR for throughput, SHA-256 for cryptographic conditioning), into a buffered pool, and directly into cryptographic key material. This chain is validated by a full NIST SP 800-22 test suite with exhaustive multi-sample uniformity analysis.

3. **Decoupled detection/response architecture:** The strict separation between the ML inference service and the DTRE policy engine is an architectural choice that improves testability, replaceability, and auditability. The ML service makes no security decisions; the DTRE makes no inferences. This separation means either component can be replaced or upgraded independently.

4. **Unsupervised + supervised hybrid detection:** The combination of Isolation Forest (zero-day capable, no labeled data required) with rule-based labeling and an XGBoost supervised layer provides both breadth (unknown attack detection) and precision (known attack classification) in a single ML pipeline.

5. **Confidence-gated adaptive blocking:** The DTRE's confidence gate (Priority 4) prevents aggressive IP blocking when the Isolation Forest's anomaly confidence is low, reducing the security-availability tradeoff inherent in purely score-threshold-based blocking policies.

### Research Gap (from Novelty Paper)

The Novelty Paper (`docs/Novelty Paper.docx`) identifies the following gap in the existing literature, which KAIROS directly addresses:

> *"There remains a comparatively less-developed area concerning the integration of these mechanisms into a lightweight gateway architecture in which ML-derived threat assessment dynamically influences both network enforcement and the cryptographic posture of active communication sessions, while hardware-derived entropy is incorporated into the cryptographic key-generation pipeline."*

### Competitive Differentiation

The following table contrasts KAIROS against a representative 2026 eBPF/ML-based gateway system from the literature (as cited in the Novelty Paper):

| Capability | KAIROS | 2026 eBPF/ML Gateway |
|-----------|--------|----------------------|
| Anomaly detection (unsupervised) | ✓ Isolation Forest | ✓ eBPF-integrated ML |
| Known-attack classification | ✓ Rule-based + XGBoost (hybrid) | ✓ Supervised classifier |
| Adaptive cryptographic session policy | ✓ DTRE key rotation / re-auth / lifetime | ✗ Not present |
| Hardware-derived entropy | ✓ Photodiode shot noise → C++ pool | ✗ Not present |
| Human-readable security reporting | ✓ Template NLG + chatbot | ✗ Not present |
| Edge commodity hardware deployment | ✓ ESP32 + off-the-shelf components | ✗ Server-grade kernel eBPF |

### Target Deployment Context

The Novelty Paper identifies the following target environments for KAIROS deployments:
- **Educational institutions** (small-scale IoT networks, constrained IT staff)
- **Small medical clinics** (medical device communication, regulatory security sensitivity)
- **Small businesses** (low-budget gateway security)
- **Agricultural IoT** (exemplified by the *GreenLeaf Farms* case study — remote sensor networks requiring authenticated, anomaly-resilient communication over unreliable links)

### Literature Context (from Novelty Paper)

The Novelty Paper (`docs/Novelty Paper.docx`) situates KAIROS within the literature on:
- Hardware QRNG/TRNG deployments and their applicability to resource-constrained edge environments.
- ML-based network intrusion detection systems (NIDS) and their limitations with fixed-signature approaches.
- Adaptive policy enforcement and its relationship to Software-Defined Networking (SDN) and Zero Trust architectures.

The IoT case study (GreenLeaf Farms agricultural IoT deployment) demonstrates the applicability of the KAIROS architecture to real-world edge deployments with low-powered, constrained devices requiring authenticated communication and anomaly-resilient key management.

---

## L. Testing, Evaluation, and Performance

### Python ML Service (58 pytest tests)

Tests are organized across four suites:

- **`test_api.py`:** HTTP endpoint contracts — `/health` (model loaded/unloaded states), `/api/threat/analyze` (valid requests, field validation errors, model-unavailable 503 responses).
- **`test_features.py`:** Feature vector construction, protocol encoding, dimension validation, `build_feature_vector()` correctness.
- **`test_model.py`:** Inference pipeline — `raw_anomaly_score()`, `score_to_threat_score()` calibration, `calculate_confidence()`, `map_score_to_level()` threshold boundaries, `identify_attack_label()` rule logic.
- **`test_schemas.py`:** Pydantic schema validation — required fields, type constraints, range enforcement, protocol whitelist, error response structure.

### Node.js DTRE (Jest unit tests)

Coverage includes:
- `engine.evaluate()` — all four priority paths (full reset override, level actions, attack-specific actions, confidence gate).
- `ActionExecutor` — each action method (`rotateSessionKey`, `blockIP`, `rateLimitClient`, `forceReAuthentication`, `alertAdmin`, `fullSessionReset`) with mocked dependencies.
- `Blocklist` — add, `isBlocked()` expiry, remove, persistence failure handling.
- `RateLimiter` — `setLimit()`, `checkLimit()` with window reset.

### C++ Entropy Module (Manual/CLI)

The `EntropyCLI.exe` interactive test bench validates:
- Real-time entropy speed (bytes/sec) with live speedometer.
- Keygen (256-bit random, PBKDF2) correctness.
- AES-256-GCM encrypt/decrypt round-trips.
- NIST SP 800-22 Quick/Full/Exhaustive modes with ASCII p-value uniformity histogram and chi-square/KS uniformity verdict.

### Entropy Statistical Validation

The NIST STS exhaustive mode runs 100 independent samples, evaluates per-test pass proportion (chi-square, target ≥ 0.96 for α=0.01), and evaluates p-value uniformity across 10 bins (KS test, p_T ≥ 0.0001 required). All tests verified passing for both Galois LFSR and SHA-256 whitening modes.

### ML Performance Metrics

| Metric | Target | Achieved (synthetic dev data) |
|--------|--------|-------------------------------|
| False positive rate (benign) | < 5% | 4.49% |
| Attack detection rate | > 90% | Measured at training validation |
| DOS label precision | > 0.90 | Rule-based, deterministic |
| BRUTE_FORCE label precision | > 0.85 | Rule-based, deterministic |
| PORT_SCAN label precision | > 0.80 | Rule-based, deterministic |
| Inference latency | < 10 ms | ~2 ms |

### End-to-End Testing

Playwright E2E tests (`tests/site-audit.spec.ts`) perform automated UI audits of the dashboard, verifying navigation, panel rendering, and component availability.

---

## M. Scope and Future Direction

### Complete System Vision

KAIROS is designed as an extensible security platform. The gateway layer (Node.js, port 3000) serves as the integration hub connecting all subsystems. The complete integrated system delivers:

- Continuous hardware-seeded cryptographic key material for all protected sessions.
- Per-message ML-based anomaly scoring at gateway ingress.
- Automatic, proportionate security response to all detected anomalies.
- Real-time SOC visibility into the entire security posture.
- Administrative control plane for policy tuning, blocklist management, and incident reporting.

### System Startup Order

The correct operational startup sequence is:

1. **ESP32** — Powers on and begins DMA entropy streaming over UART.
2. **C++ Entropy Pool** (`EntropyModule.exe`) — Opens UART, begins serial ingestion, whitening, and pool fill; registers the shared library used by the Node.js gateway via ffi-napi.
3. **Python ML Service** (`uvicorn app:app`, port 5000) — Loads pre-trained `isolation_forest.pkl`, `scaler.pkl`, `metadata.json`; begins accepting inference requests.
4. **Node.js Gateway** (`node server.js`, port 3000) — Connects to the entropy pool shared library, verifies ML service health, begins accepting IoT device registrations.

The gateway performs a startup-time ML health check (`GET http://127.0.0.1:5000/health`). If the ML service is unreachable at startup, the gateway enters degraded-ML mode immediately.

### Versioning Roadmap

The project specification documents a five-phase versioning roadmap:

| Version | Milestone | Key Additions |
|---------|-----------|---------------|
| **v1** | Current architecture | Isolation Forest anomaly detection, AES-256-GCM, PQRNG entropy chain, React SOC dashboard, DTRE policy engine |
| **v2** | Federated learning + supervised layer | XGBoost supervised classifier integrated, federated ML for shared threat intelligence across gateway instances, multi-gateway deployment |
| **v3** | Post-Quantum Cryptography | Replace RSA-OAEP with **CRYSTALS-Kyber** (NIST PQC standard) for key encapsulation; integrate **CRYSTALS-Dilithium** for digital signatures; TPM/Secure Element hardware attestation; Edge AI acceleration |
| **v4** | Enterprise scale | Multi-site deployment with centralized management console, enterprise SOC dashboard with multi-tenant support, long-term security analytics and trend reporting |
| **v5** | Autonomous policy optimization | Reinforcement Learning (RL) agent replaces the static DTRE decision matrix; distributed gateway cluster with consensus-based policy synchronization; industrial IoT hardening |

### Architectural Expansion Directions

- **Gateway completeness:** Implementing the full Node.js Gateway routing layer (session management, device registration, ML telemetry forwarding, DTRE dispatch) to connect the independently tested modules into a fully integrated platform.
- **XGBoost supervised layer:** Extending the ML pipeline with the XGBoost classifier for high-precision known-attack categorization, operating in parallel with or cascading from the Isolation Forest.
- **3D network topology visualization:** Replacing the current SVG topology diagram with an interactive Three.js force-directed graph for real-time network monitoring.
- **Security assistant chatbot:** Rule-based NLP chatbot at `/api/dashboard/chat` for operator queries about current security posture, active incidents, and recommended actions.
- **Automated incident reporting:** Structured Markdown/HTML security digest generation at `/api/dashboard/report`, aggregating ML events, DTRE actions, session activity, and blocklist changes.
- **CI/CD pipeline:** GitHub Actions workflow for cross-platform C++ builds (vcpkg), Python pytest, Jest, and Playwright test automation.
- **C++ unit tests:** GoogleTest or Catch2 harness for the entropy, crypto, and pool subsystems.
- **Multi-platform entropy support:** Linux/macOS serial entropy path alongside the current Windows-optimized implementation.
- **TLS for the gateway REST API:** The configuration supports TLS (`tls.enabled`, `cert_path`, `key_path` in config.json); enabling it would secure the gateway's public-facing REST endpoints.

### Design Philosophy

KAIROS is architected as a **headless-first** platform. All functionality is accessible via REST APIs without requiring a frontend. The SOC dashboard is an operational convenience layer, not a dependency. This philosophy enables KAIROS to operate as an embedded library, a system service, or an API-only gateway in constrained environments.

The modular separation of entropy generation, cryptographic operations, ML inference, and policy execution allows each component to be independently upgraded — for example, swapping the Isolation Forest for a different unsupervised model, or replacing the LFSR whitening with a stronger conditioner — without architectural changes to the rest of the system.

---

## Cross-Reference: Module-to-File Mapping

| Subsystem | Key Files |
|-----------|-----------|
| ESP32 Firmware | `hardware/hardware.ino` |
| Serial Entropy Source | `backend/src/entropy/SerialEntropySource.cpp/h` |
| OpenSSL Fallback | `backend/src/entropy/OpenSSLEntropySource.cpp/h` |
| Entropy Pool | `backend/src/EntropyPool.cpp/h` |
| Entropy Orchestration | `backend/src/EntropyCollector.cpp/h` |
| AES-256-GCM | `backend/src/crypto/Encryptor.cpp/h` |
| Key Generation / PBKDF2 | `backend/src/crypto/KeyGenerator.cpp/h` |
| SHA-256 Hashing | `backend/src/crypto/Hasher.cpp/h` |
| REST API Server | `backend/src/WebServer.cpp/h` |
| CLI Test Bench | `backend/src/cli/EntropyCLI.cpp` |
| NIST STS Analyzer | `analyzer/nist_tests.py`, `analyzer/entropy_analyzer.py` |
| ML FastAPI Service | `python/ml_service/app.py` |
| ML Model Inference | `python/ml_service/model.py` |
| Feature Extraction | `python/ml_service/features.py` |
| API Schemas | `python/ml_service/schemas.py` |
| Constants / Feature Order | `python/ml_service/constants.py` |
| Training Pipeline | `python/training/train.py`, `preprocess.py`, `data_loader.py` |
| Model Evaluation | `python/training/evaluate.py` |
| Trained Artifacts | `python/models/isolation_forest.pkl`, `scaler.pkl`, `metadata.json` |
| ML Interactive Test Bench | `python/ml_menu.py` |
| DTRE Decision Engine | `nodejs/src/policy/engine.js` |
| DTRE Action Executor | `nodejs/src/policy/actions.js` |
| IP Blocklist | `nodejs/src/policy/blocklist.js` |
| Rate Limiter | `nodejs/src/policy/rateLimiter.js` |
| Policy Thresholds | `nodejs/src/policy/thresholds.js` |
| DTRE Configuration | `nodejs/config/config.json` |
| SOC Dashboard | `frontend/src/App.tsx` |
| Dashboard Types | `frontend/src/types.ts` |
| Mock Data Store | `frontend/src/mockData.ts` |
| Entropy Module Config | `config/backend_config.json` |
| Project Context | `CONTEXT.md` |
| Technical Specifications | `docs/Hardware Entropy.docx`, `docs/Gateway.docx`, `docs/ML and Engine.docx`, `docs/Frontend.docx`, `docs/Novelty Paper.docx`, `docs/Project summary.docx` |

---

*This document was generated from source code analysis, CONTEXT.md, and technical documentation. For implementation-level depth on any subsystem, consult the corresponding file in `docs/` or the source files listed above.*
