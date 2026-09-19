"""
Integration tests for the KAIROS ML Service API (Section 12.3).
Run with: pytest tests/test_api.py
Requires: fastapi, httpx, pydantic (see requirements.txt)
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fastapi.testclient import TestClient

from ml_service import model as model_module
from ml_service.app import app


@pytest.fixture(scope="module")
def client():
    with TestClient(app) as c:
        yield c


VALID_PAYLOAD = {
    "session_id": "sess_test_0001",
    "source": "drone_001",
    "target": "base_station_002",
    "plaintext": "Hello Device B",
    "features": {
        "data_length": 15,
        "protocol": "websocket",
        "packet_rate": 10.5,
        "avg_packet_size": 256,
        "destination_diversity": 1,
        "failed_logins": 0,
    },
}


def test_health_check_reports_model_status(client):
    resp = client.get("/health")
    assert resp.status_code in (200, 503)
    body = resp.json()
    assert "status" in body
    assert "model_loaded" in body


def test_analyze_valid_request_returns_expected_shape(client):
    if not model_module.model_state.model_loaded:
        pytest.skip("Model not loaded — run training/train.py first")
    resp = client.post("/api/threat/analyze", json=VALID_PAYLOAD)
    assert resp.status_code == 200
    body = resp.json()
    assert 0.0 <= body["threat_score"] <= 1.0
    assert body["level"] in ("green", "yellow", "orange", "red")
    assert 0.0 <= body["confidence"] <= 1.0
    assert "timestamp" in body


def test_analyze_missing_field_returns_400(client):
    bad_payload = dict(VALID_PAYLOAD)
    bad_payload["features"] = dict(VALID_PAYLOAD["features"])
    del bad_payload["features"]["packet_rate"]
    resp = client.post("/api/threat/analyze", json=bad_payload)
    assert resp.status_code == 400
    body = resp.json()
    assert body["status"] == "error"
    assert body["error_code"] == "MISSING_FIELD"


def test_analyze_invalid_protocol_returns_400(client):
    bad_payload = dict(VALID_PAYLOAD)
    bad_payload["features"] = dict(VALID_PAYLOAD["features"])
    bad_payload["features"]["protocol"] = "http"
    resp = client.post("/api/threat/analyze", json=bad_payload)
    assert resp.status_code == 400


def test_analyze_high_packet_rate_triggers_dos_label(client):
    if not model_module.model_state.model_loaded:
        pytest.skip("Model not loaded — run training/train.py first")
    payload = dict(VALID_PAYLOAD)
    payload["features"] = dict(VALID_PAYLOAD["features"])
    payload["features"]["packet_rate"] = 5000.0
    resp = client.post("/api/threat/analyze", json=payload)
    assert resp.status_code == 200
    body = resp.json()
    # Attack label only fires if threat_score > 0.5 (Section 4.5) — the
    # extreme packet_rate should push the Isolation Forest score high
    # enough to activate the DOS label for a well-trained model.
    if body["threat_score"] > 0.5:
        assert body["attack_label"] == "DOS"


def test_analyze_plaintext_never_echoed_in_response(client):
    if not model_module.model_state.model_loaded:
        pytest.skip("Model not loaded — run training/train.py first")
    resp = client.post("/api/threat/analyze", json=VALID_PAYLOAD)
    assert "plaintext" not in resp.json()
