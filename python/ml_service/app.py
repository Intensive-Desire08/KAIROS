"""
KAIROS ML Threat Detection Service — FastAPI application.
See Technical Specification — Module 3, Section 5.

Binds to 127.0.0.1 only (Section 5.1). No authentication (localhost-only
trust boundary, Section 3.3). Stateless (Section 2.1).
"""

from __future__ import annotations

import time
from datetime import datetime, timezone

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from loguru import logger
from pydantic import ValidationError

from . import model as model_module
from .config import get_config
from .constants import ErrorCode
from .features import build_feature_vector, normalize_features
from .schemas import ErrorResponse, HealthResponse, ThreatRequest, ThreatResponse

APP_VERSION = "1.0.0"

app = FastAPI(title="KAIROS ML Threat Detection Service", version=APP_VERSION)

_start_time: float = 0.0


def _now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# Pydantic v2 error `type` values that indicate a numeric value fell
# outside its allowed range (e.g. negative packet_rate). Everything else
# validation-related (missing aside) is a malformed/invalid field value
# (e.g. protocol "http"). This mirrors the three-way split in Section 5.7:
# MISSING_FIELD / INVALID_FIELD / INVALID_FEATURE.
_RANGE_ERROR_TYPES = {
    "greater_than_equal",
    "greater_than",
    "less_than_equal",
    "less_than",
}


def _classify_validation_error(error: dict) -> str:
    error_type = error.get("type", "")
    if error_type == "missing":
        return ErrorCode.MISSING_FIELD.value
    if error_type in _RANGE_ERROR_TYPES:
        return ErrorCode.INVALID_FEATURE.value
    return ErrorCode.INVALID_FIELD.value


def _error_body(error_code: str, message: str, details: dict | None = None) -> dict:
    # exclude_none so a bare error (no `details`) doesn't serialize a
    # spurious `"details": null` key — the spec's error examples (Section
    # 5.3, 5.7) only include `details` when there's actually something to say.
    return ErrorResponse(
        error_code=error_code,
        message=message,
        timestamp=_now_iso(),
        details=details,
    ).model_dump(exclude_none=True)


@app.on_event("startup")
def on_startup() -> None:
    """Load model + scaler at startup (Section 5.9 Startup Sequence)."""
    global _start_time
    _start_time = time.monotonic()

    get_config()  # warm the config cache

    model_module.model_state.load()
    if model_module.model_state.model_loaded:
        logger.info("model_loaded | Isolation Forest and scaler loaded successfully")
    else:
        logger.error(f"model_load_failed | {model_module.model_state.load_error}")

    logger.info(f"service_started | KAIROS ML Threat Detection Service v{APP_VERSION}")


@app.on_event("shutdown")
def on_shutdown() -> None:
    """Graceful shutdown (Section 5.9). Stateless — no cleanup required."""
    logger.info("service_stopped | shutdown complete (stateless, no cleanup required)")


@app.get("/health")
def health_check():
    """Health check endpoint (Section 5.2)."""
    uptime = time.monotonic() - _start_time if _start_time else 0.0
    body = HealthResponse(
        status="healthy" if model_module.model_state.model_loaded else "unhealthy",
        version=APP_VERSION,
        model_loaded=model_module.model_state.model_loaded,
        uptime_seconds=round(uptime, 2),
        timestamp=_now_iso(),
        error=model_module.model_state.load_error,
    )
    status_code = 200 if model_module.model_state.model_loaded else 503
    return JSONResponse(status_code=status_code, content=body.model_dump(exclude_none=True))


@app.post("/api/threat/analyze")
async def analyze_threat(request: Request):
    """
    Analyze network traffic features and return a threat assessment
    (Section 5.3). Errors follow the Gateway's error format (Section 5.7).

    NOTE: `plaintext` is accepted per the request schema but is used only
    for potential future NLP/analysis — it is NEVER logged or persisted
    (Section 11: Memory Safety & Plaintext Cleanup).
    """
    try:
        payload = await request.json()
    except Exception:
        return JSONResponse(
            status_code=400,
            content=_error_body(ErrorCode.INVALID_FIELD.value, "Request body is not valid JSON"),
        )

    try:
        threat_request = ThreatRequest(**payload)
    except ValidationError as exc:
        first_error = exc.errors()[0]
        field = ".".join(str(loc) for loc in first_error.get("loc", []))
        return JSONResponse(
            status_code=400,
            content=_error_body(
                _classify_validation_error(first_error),
                f"Invalid request: {first_error.get('msg', 'validation error')} ({field})",
                details={"field": field},
            ),
        )

    if not model_module.model_state.model_loaded:
        logger.error("inference_error | model not loaded")
        return JSONResponse(
            status_code=503,
            content=_error_body(ErrorCode.MODEL_NOT_LOADED.value, "Model is not loaded"),
        )

    start = time.monotonic()
    try:
        vector = build_feature_vector(threat_request.features)
        scaled = normalize_features(vector, model_module.model_state.scaler)
        threat_score, level, confidence, attack_label = model_module.analyze(
            threat_request.features, scaled
        )
    except ValueError as exc:
        logger.error(f"validation_error | {exc}")
        return JSONResponse(
            status_code=400,
            content=_error_body(ErrorCode.INVALID_FEATURE.value, str(exc)),
        )
    except Exception as exc:
        logger.error(f"inference_error | {exc}")
        return JSONResponse(
            status_code=500,
            content=_error_body(ErrorCode.INFERENCE_FAILED.value, "Model inference failed", details={"error": str(exc)}),
        )

    inference_time_ms = round((time.monotonic() - start) * 1000, 2)

    response = ThreatResponse(
        threat_score=round(threat_score, 4),
        level=level.value,
        confidence=round(confidence, 4),
        attack_label=attack_label,
        timestamp=_now_iso(),
    )

    # Structured JSON logging (Section 5.8) — NEVER logs plaintext (Section 11.6)
    log_event = "attack_detected" if attack_label else "threat_analyzed"
    logger.info(
        f"{log_event} | session_id={threat_request.session_id} "
        f"source={threat_request.source} target={threat_request.target} "
        f"threat_score={response.threat_score} level={response.level} "
        f"attack_label={attack_label} confidence={response.confidence} "
        f"inference_time_ms={inference_time_ms}"
    )

    return JSONResponse(status_code=200, content=response.model_dump())
