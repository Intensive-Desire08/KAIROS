import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ml_service.constants import FEATURE_ORDER
from ml_service.features import build_feature_vector, encode_protocol, normalize_features


class DummyFeatures:
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)


def test_encode_protocol_known_values():
    assert encode_protocol("websocket") == 0
    assert encode_protocol("tcp") == 1
    assert encode_protocol("udp") == 2
    assert encode_protocol("icmp") == 3


def test_encode_protocol_case_insensitive():
    assert encode_protocol("WebSocket") == 0
    assert encode_protocol("TCP") == 1


def test_encode_protocol_unknown_raises():
    with pytest.raises(ValueError):
        encode_protocol("http")


def test_build_feature_vector_order_and_shape():
    features = DummyFeatures(
        data_length=100,
        protocol="tcp",
        packet_rate=5.0,
        avg_packet_size=200.0,
        destination_diversity=2,
        failed_logins=0,
    )
    vector = build_feature_vector(features)
    assert vector.shape == (1, len(FEATURE_ORDER))
    # protocol_encoded should be at index 1 (Section 4.4)
    assert vector[0][FEATURE_ORDER.index("protocol_encoded")] == 1.0
    assert vector[0][FEATURE_ORDER.index("data_length")] == 100.0
    # session_duration and total_packets are not provided by the Gateway
    # in the analyze request; must default to 0 (Section 5.5).
    assert vector[0][FEATURE_ORDER.index("session_duration")] == 0.0
    assert vector[0][FEATURE_ORDER.index("total_packets")] == 0.0


class DummyScaler:
    def transform(self, X):
        return X * 2  # trivial deterministic transform for testing


def test_normalize_features_dimension_mismatch():
    bad_vector = np.zeros((1, len(FEATURE_ORDER) - 1))
    with pytest.raises(ValueError):
        normalize_features(bad_vector, DummyScaler())


def test_normalize_features_applies_scaler():
    vector = np.ones((1, len(FEATURE_ORDER)))
    result = normalize_features(vector, DummyScaler())
    assert np.allclose(result, vector * 2)
