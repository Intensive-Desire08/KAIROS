"""
Unit tests for Pydantic schemas in the KAIROS ML Threat Detection Service.
See Technical Specification — Module 3, Section 5.2, 5.3, 5.6, 14.2.
"""

import sys
from pathlib import Path

import pytest
from pydantic import ValidationError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ml_service.schemas import (
    ErrorResponse,
    HealthResponse,
    ThreatFeatures,
    ThreatRequest,
    ThreatResponse,
)


def test_threat_features_valid():
    features = ThreatFeatures(
        data_length=128,
        protocol="tcp",
        packet_rate=15.0,
        avg_packet_size=512.0,
        destination_diversity=2,
    )
    assert features.data_length == 128
    assert features.protocol == "tcp"
    assert features.packet_rate == 15.0
    assert features.avg_packet_size == 512.0
    assert features.destination_diversity == 2
    assert features.failed_logins == 0
    assert features.session_duration == 0.0
    assert features.total_packets == 0
    assert features.timestamp is None


def test_threat_features_with_session_metrics():
    features = ThreatFeatures(
        data_length=64,
        protocol="websocket",
        packet_rate=5.0,
        avg_packet_size=128.0,
        destination_diversity=1,
        failed_logins=3,
        session_duration=60.5,
        total_packets=300,
        timestamp="2026-10-13T20:00:00Z",
    )
    assert features.failed_logins == 3
    assert features.session_duration == 60.5
    assert features.total_packets == 300
    assert features.timestamp == "2026-10-13T20:00:00Z"


@pytest.mark.parametrize("proto", ["websocket", "tcp", "udp", "icmp", "WebSocket", "TCP", "UDP", "ICMP"])
def test_threat_features_allowed_protocols(proto):
    features = ThreatFeatures(
        data_length=10,
        protocol=proto,
        packet_rate=1.0,
        avg_packet_size=10.0,
        destination_diversity=1,
    )
    assert features.protocol == proto.lower()


def test_threat_features_invalid_protocol():
    with pytest.raises(ValidationError):
        ThreatFeatures(
            data_length=10,
            protocol="http",
            packet_rate=1.0,
            avg_packet_size=10.0,
            destination_diversity=1,
        )


@pytest.mark.parametrize(
    "invalid_kwargs",
    [
        {"data_length": -1},
        {"data_length": 70000},
        {"packet_rate": -0.5},
        {"avg_packet_size": -1.0},
        {"avg_packet_size": 70000.0},
        {"destination_diversity": -1},
        {"destination_diversity": 256},
        {"failed_logins": -1},
        {"session_duration": -1.0},
        {"total_packets": -1},
    ],
)
def test_threat_features_range_validation(invalid_kwargs):
    base_kwargs = {
        "data_length": 100,
        "protocol": "tcp",
        "packet_rate": 10.0,
        "avg_packet_size": 200.0,
        "destination_diversity": 1,
    }
    base_kwargs.update(invalid_kwargs)
    with pytest.raises(ValidationError):
        ThreatFeatures(**base_kwargs)


def test_threat_request_valid():
    req = ThreatRequest(
        session_id="sess_abc123",
        source="drone_01",
        target="base_station",
        plaintext="ping",
        features=ThreatFeatures(
            data_length=4,
            protocol="udp",
            packet_rate=2.0,
            avg_packet_size=32.0,
            destination_diversity=1,
        ),
    )
    assert req.session_id == "sess_abc123"
    assert req.source == "drone_01"
    assert req.target == "base_station"
    assert req.plaintext == "ping"
    assert req.features.data_length == 4


def test_threat_request_empty_strings_invalid():
    with pytest.raises(ValidationError):
        ThreatRequest(
            session_id="",
            source="drone_01",
            target="base_station",
            plaintext="ping",
            features=ThreatFeatures(
                data_length=4,
                protocol="udp",
                packet_rate=2.0,
                avg_packet_size=32.0,
                destination_diversity=1,
            ),
        )


def test_threat_response_valid():
    resp = ThreatResponse(
        threat_score=0.82,
        level="orange",
        confidence=0.91,
        attack_label="DOS",
        timestamp="2026-10-13T20:00:00Z",
    )
    assert resp.threat_score == 0.82
    assert resp.level == "orange"
    assert resp.confidence == 0.91
    assert resp.attack_label == "DOS"


@pytest.mark.parametrize("score", [-0.1, 1.1])
def test_threat_response_score_bounds(score):
    with pytest.raises(ValidationError):
        ThreatResponse(
            threat_score=score,
            level="green",
            confidence=0.5,
            timestamp="2026-10-13T20:00:00Z",
        )


def test_error_response_serialization():
    err = ErrorResponse(
        error_code="INVALID_FIELD",
        message="Invalid field",
        timestamp="2026-10-13T20:00:00Z",
        details={"field": "protocol"},
    )
    dumped = err.model_dump(exclude_none=True)
    assert dumped["status"] == "error"
    assert dumped["error_code"] == "INVALID_FIELD"
    assert dumped["details"] == {"field": "protocol"}


def test_health_response_serialization():
    health = HealthResponse(
        status="healthy",
        version="1.0.0",
        model_loaded=True,
        uptime_seconds=42.5,
        timestamp="2026-10-13T20:00:00Z",
    )
    dumped = health.model_dump(exclude_none=True)
    assert dumped["status"] == "healthy"
    assert dumped["model_loaded"] is True
    assert dumped["uptime_seconds"] == 42.5
