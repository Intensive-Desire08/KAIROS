# KAIROS

### Quantum-Inspired Cryptographic Gateway & Security Operations Platform

<p align="center">
  <strong>Secure. Adaptive. Observable.</strong>
</p>

<p align="center">
  KAIROS combines entropy collection, cryptographic session management, ML-based threat detection, dynamic threat response, and real-time security monitoring into a unified security platform.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Active%20Development-00ff88?style=for-the-badge" alt="Status">
  <img src="https://img.shields.io/badge/Frontend-React%20%2B%20TypeScript-61dafb?style=for-the-badge&logo=react&logoColor=white" alt="Frontend">
  <img src="https://img.shields.io/badge/Backend-C%2B%2B-00599c?style=for-the-badge&logo=cplusplus&logoColor=white" alt="Backend">
  <img src="https://img.shields.io/badge/ML-Python%20%2B%20FastAPI-3776ab?style=for-the-badge&logo=python&logoColor=white" alt="ML">
  <img src="https://img.shields.io/badge/DTRE-Node.js-339933?style=for-the-badge&logo=nodedotjs&logoColor=white" alt="DTRE">
</p>

---

## Overview

**KAIROS** is a modular security platform designed around a cryptographic gateway architecture.

The system combines:

- Hardware-assisted entropy collection
- Cryptographic session management
- Secure key generation and distribution
- ML-based anomaly and threat detection
- Dynamic Threat Response Engine (DTRE)
- Real-time security telemetry
- SOC-style security visualization
- Network topology monitoring
- Administrative security controls

The architecture separates **detection**, **decision-making**, **cryptographic operations**, and **visualization**, allowing each subsystem to be developed, tested, and extended independently.

---

# Architecture

```text
                              KAIROS
                                 │
             ┌───────────────────┴───────────────────┐
             │                                       │
             ▼                                       ▼
   ┌─────────────────────┐                ┌─────────────────────┐
   │   Security SOC      │                │  Entropy / Hardware │
   │      Dashboard      │                │       Layer         │
   │                     │                │                     │
   │ React + TypeScript  │                │ ESP32 / QRNG        │
   │ Vite + Tailwind     │                │ C++ Entropy Pool    │
   │ Recharts + xterm    │                │ OpenSSL Fallback    │
   └──────────┬──────────┘                └──────────┬──────────┘
              │                                      │
              │ REST / WebSocket                     │
              │                                      │
              └──────────────────┬───────────────────┘
                                 │
                                 ▼
                     ┌─────────────────────────┐
                     │     KAIROS GATEWAY      │
                     │                         │
                     │ Session Management      │
                     │ Cryptographic Engine    │
                     │ Device Registry         │
                     │ Key Distribution        │
                     │ Policy Integration       │
                     │ Logging                 │
                     └────────────┬────────────┘
                                  │
                     ┌────────────┴────────────┐
                     │                         │
                     ▼                         ▼
          ┌─────────────────────┐   ┌─────────────────────┐
          │   ML Threat Engine  │   │ Dynamic Threat      │
          │                     │   │ Response Engine     │
          │ Python              │   │                     │
          │ FastAPI             │   │ Node.js             │
          │ Isolation Forest    │   │ Policy Engine       │
          │ Threat Scoring      │   │ Security Actions    │
          └─────────────────────┘   └─────────────────────┘
```

---

# Core Modules

## 01 — Entropy & QRNG Layer

The entropy subsystem provides randomness for cryptographic operations.

### Components

- Hardware entropy collection
- ESP32-based entropy source
- Serial/USB entropy interface
- C++ entropy pool
- OpenSSL entropy fallback
- Entropy analysis tools
- Statistical randomness testing

The system is designed to maintain entropy availability even when the external hardware source is unavailable.

---

## 02 — KAIROS Cryptographic Gateway

The Gateway forms the central security boundary of the platform.

### Responsibilities

- Session management
- Device registration
- Key distribution
- Cryptographic operations
- Entropy management
- WebSocket communication
- REST APIs
- ML integration
- Policy integration
- Security logging

### Cryptographic Components

The project includes support for:

- RSA-OAEP key exchange
- AES-256-GCM encryption
- Secure session keys
- Nonce generation
- Hashing
- Key generation

The Gateway acts as the controlled interface between external clients and internal security services.

---

## 03 — ML Threat Detection & Dynamic Threat Response

KAIROS separates **threat detection** from **security response**.

### ML Threat Detection

The Python service analyzes connection and session features and produces a threat assessment.

Current implementation uses:

```text
Isolation Forest
```

The service produces:

```text
Threat Score
Security Level
Attack Label
```

Supported attack labels include:

```text
DOS
BRUTE_FORCE
PORT_SCAN
```

### Technology

- Python
- FastAPI
- scikit-learn
- NumPy
- pandas
- joblib
- Pydantic

---

## 04 — Dynamic Threat Response Engine

The Node.js DTRE consumes threat assessments and applies configurable security policies.

### Response Actions

Depending on the policy and threat level, actions can include:

```text
rotateSessionKey
blockIP
rateLimitClient
forceReAuthentication
alertAdmin
```

Additional policy functionality includes:

- Threat thresholds
- Attack-specific rules
- Confidence gates
- IP blocklists
- Rate limiting
- Automatic expiry
- Dependency-injected actions

### Design Principle

```text
ML Service
    │
    │ Threat Assessment
    ▼
DTRE Policy Engine
    │
    │ Decision
    ▼
Security Action
```

The ML service identifies suspicious behavior.

The DTRE decides what should happen.

This separation keeps detection and response logic independently configurable.

---

# 05 — KAIROS Security Operations Center

The KAIROS frontend provides a real-time SOC-style monitoring interface.

### Frontend Stack

| Technology | Purpose |
|---|---|
| React | UI framework |
| TypeScript | Type safety |
| Vite | Frontend tooling |
| Tailwind CSS | UI styling |
| Zustand | State management |
| Recharts | Security telemetry visualization |
| Framer Motion | Controlled UI animation |
| xterm.js | Administrative terminal |
| WebSocket | Real-time events |
| Axios | Gateway API communication |

### Dashboard Capabilities

The dashboard provides visibility into:

- Live threat telemetry
- Threat score trends
- Security levels
- Security events
- Active sessions
- Device health
- Security actions
- Network topology
- Gateway status
- Administrative operations

### Security Boundary

The frontend communicates with the **Gateway**, not directly with the ML service.

```text
                    FRONTEND
                       │
                       │
                REST / WebSocket
                       │
                       ▼
                KAIROS GATEWAY
                       │
            ┌──────────┴──────────┐
            │                     │
            ▼                     ▼
       ML SERVICE              DTRE
```

This prevents internal ML infrastructure from becoming a direct client-facing interface.

---

# Frontend Features

### Threat Monitoring

Real-time visualization of:

- Threat scores
- Security levels
- Attack classifications
- Threat events
- Historical trends

### Session Matrix

Provides operational visibility into active sessions while exposing only safe session metadata.

### Device Health

Displays connected devices and their operational state.

### Security Action Queue

Shows actions generated by the Dynamic Threat Response Engine.

### Network Topology

Visualizes relationships between security components and connected devices.

### Administrative Terminal

Provides a terminal-style interface for supported administrative operations.

### Offline Development Mode

When the Gateway is unavailable, the frontend can use a controlled mock-data layer for development and UI testing.

The mock layer does not replace the real Gateway integration.

---

# Repository Structure

```text
KAIROS/
│
├── analyzer/
│   ├── entropy_analyzer.py
│   ├── nist_tests.py
│   └── test_results_schema.json
│
├── backend/
│   ├── include/
│   └── src/
│       ├── Crypto/
│       ├── entropy/
│       └── utilities/
│
├── config/
│   ├── backend_config.json
│   └── logging_config.json
│
├── docs/
│   ├── CONTEXT.md
│   └── report_text.txt
│
├── frontend/
│   ├── src/
│   │   ├── services/
│   │   ├── store/
│   │   └── ...
│   ├── package.json
│   ├── vite.config.ts
│   └── index.html
│
├── hardware/
│   └── PQHardware.ino
│
├── nodejs/
│   ├── config/
│   ├── src/
│   │   └── policy/
│   └── tests/
│
├── python/
│   ├── ml_service/
│   ├── models/
│   ├── training/
│   ├── tests/
│   └── main.py
│
├── scripts/
│   ├── build.sh
│   ├── run.bat
│   └── run.sh
│
├── tests/
│   ├── example.spec.ts
│   └── site-audit.spec.ts
│
├── CMakeLists.txt
├── CMakePresets.json
├── package.json
├── playwright.config.ts
├── tsconfig.json
└── vcpkg.json
```

---

# Getting Started

## Prerequisites

Install:

- Git
- Node.js
- npm
- Python 3.x
- CMake
- A C++ compiler
- vcpkg
- OpenSSL

Hardware functionality additionally requires the supported entropy hardware.

---

# Frontend

Navigate to the frontend:

```bash
cd frontend
```

Install dependencies:

```bash
npm install
```

Start the development server:

```bash
npm run dev
```

Build for production:

```bash
npm run build
```

The frontend currently builds successfully using the Vite production build.

---

# ML Service

Navigate to the Python service:

```bash
cd python
```

Install dependencies:

```bash
pip install -r requirements.txt
```

Train the model:

```bash
python training/train.py
```

Start the service:

```bash
python main.py
```

The ML service exposes threat-analysis functionality through the Gateway architecture.

---

# Dynamic Threat Response Engine

Navigate to the Node.js engine:

```bash
cd nodejs
```

Install dependencies:

```bash
npm install
```

Run tests:

```bash
npm test
```

---

# C++ Backend

Configure the C++ project using the appropriate CMake preset:

```bash
cmake --preset <preset>
```

Build:

```bash
cmake --build --preset <preset>
```

On Windows, the repository also provides:

```text
scripts/run.bat
```

The script launches the compiled backend executable using the configured backend configuration.

---

# Testing

KAIROS contains tests across multiple layers.

### Python

```bash
cd python
pytest
```

### Node.js

```bash
cd nodejs
npm test
```

### Frontend

```bash
cd frontend
npm run build
```

### End-to-End

From the project root:

```bash
npx playwright test
```

---

# Configuration

Configuration is separated from application code.

```text
config/
nodejs/config/
```

Configuration includes:

- Backend settings
- Logging settings
- DTRE thresholds
- Security policies
- Action parameters

Local secrets and environment-specific credentials should never be committed to the repository.

---

# Security Principles

KAIROS follows a security-first architecture.

### Gateway Boundary

The frontend communicates through the Gateway rather than directly accessing internal services.

### Detection / Response Separation

ML determines whether activity appears anomalous.

DTRE determines how the system responds.

### Sensitive Data Protection

The dashboard must never expose:

```text
Private Keys
Session Keys
Passwords
JWT Secrets
Raw Entropy
Credentials
Sensitive Decrypted Payloads
```

Only appropriate operational telemetry should be exposed.

### Fail-Safe Operation

Security components are designed to maintain safe behavior when dependent services become unavailable.

### Modular Architecture

Each subsystem can be developed and tested independently while maintaining defined interfaces between components.

---

# Threat Processing Flow

```text
Client / Device
      │
      ▼
KAIROS Gateway
      │
      ├── Session Metadata
      │
      ▼
ML Threat Detection
      │
      │ threat_score
      │ security_level
      │ attack_label
      ▼
DTRE Policy Engine
      │
      ▼
Security Decision
      │
      ├── Rotate Session Key
      ├── Rate Limit Client
      ├── Block IP
      ├── Force Re-authentication
      └── Alert Administrator
      │
      ▼
SOC Dashboard
```

---

# Threat Levels

Threat assessments use a normalized score:

```text
0.0 ─────────────────────────────── 1.0
 │                                    │
Low                                  High
Threat                               Threat
```

The dashboard visualizes the resulting security state and associated events in real time.

---

# Development Philosophy

KAIROS is designed around several principles:

**Security by design**  
Security controls are integrated into the architecture rather than added as an afterthought.

**Clear service boundaries**  
Each subsystem has a defined responsibility and communication boundary.

**Observable security**  
Security events and decisions should be visible through operational telemetry.

**Modular engineering**  
Components should remain independently testable and replaceable.

**Fail-safe behavior**  
Service failures should not silently disable security controls.

**Minimal exposure**  
Sensitive cryptographic material and internal service details should remain outside the presentation layer.

---

# Current Status

### Implemented

- [x] Entropy collection infrastructure
- [x] C++ entropy pool
- [x] OpenSSL entropy fallback
- [x] Cryptographic utilities
- [x] Python ML threat detection service
- [x] Isolation Forest anomaly detection
- [x] Threat scoring
- [x] Attack classification
- [x] Node.js Dynamic Threat Response Engine
- [x] Threat policies and thresholds
- [x] IP blocklist
- [x] Rate limiting
- [x] Security response actions
- [x] React + TypeScript SOC dashboard
- [x] Gateway API abstraction
- [x] Mock telemetry fallback
- [x] Production frontend build
- [x] Automated testing infrastructure

### In Development

- [ ] Complete Gateway REST API integration
- [ ] Complete Gateway WebSocket event integration
- [ ] Full live telemetry pipeline
- [ ] Production authentication flow
- [ ] Expanded administrative controls
- [ ] Additional threat models
- [ ] Hardware-to-Gateway integration refinement

---

# Roadmap

```text
[✓] Entropy subsystem
        │
        ▼
[✓] Cryptographic Gateway components
        │
        ▼
[✓] ML Threat Detection
        │
        ▼
[✓] Dynamic Threat Response Engine
        │
        ▼
[✓] SOC Dashboard foundation
        │
        ▼
[ ] Full Gateway integration
        │
        ▼
[ ] Real-time production telemetry
        │
        ▼
[ ] Expanded threat intelligence
        │
        ▼
[ ] Production deployment
```

---

# Technology Stack

| Layer | Technologies |
|---|---|
| SOC Frontend | React, TypeScript, Vite, Tailwind CSS |
| State | Zustand |
| Visualization | Recharts, Three.js |
| Animation | Framer Motion |
| Terminal | xterm.js |
| Gateway / Core | C++, CMake, OpenSSL |
| Entropy | ESP32, QRNG, Serial/USB |
| ML | Python, FastAPI, scikit-learn |
| ML Model | Isolation Forest |
| DTRE | Node.js |
| Testing | pytest, Jest, Playwright |
| Configuration | JSON |
| Build | CMake, npm, vcpkg |

---

# Project Philosophy

KAIROS is built around a simple principle:

> **Detect threats. Decide intelligently. Respond dynamically. Observe everything that matters.**

The goal is not simply to encrypt traffic or detect anomalies independently, but to connect entropy, cryptography, intelligence, response, and observability into one coherent security architecture.

---

<p align="center">
  <strong>KAIROS</strong><br>
  Quantum-Inspired Cryptographic Gateway & Security Operations Platform
</p>

<p align="center">
  <sub>Built for secure systems research, engineering, and experimentation.</sub>
</p>