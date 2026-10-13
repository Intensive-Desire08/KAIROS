"""
Feature normalization for the KAIROS ML Threat Detection Service.
See Technical Specification — Module 3, Section 4.4 / 5.5.

The Gateway sends RAW features; normalization is this service's
responsibility via a fitted StandardScaler loaded at startup.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Dict

import numpy as np

from .constants import FEATURE_ORDER, PROTOCOL_ENCODING

if TYPE_CHECKING:
    from .schemas import ThreatFeatures


def encode_protocol(protocol: str) -> int:
    """Map protocol string to integer per Section 4.4 (websocket=0, tcp=1, udp=2, icmp=3)."""
    key = protocol.lower()
    if key not in PROTOCOL_ENCODING:
        raise ValueError(f"Unknown protocol '{protocol}'")
    return PROTOCOL_ENCODING[key]


def build_feature_vector(features: ThreatFeatures) -> np.ndarray:
    """
    Build the canonical 8-element feature vector in FEATURE_ORDER.

    Per Section 4.4 and Section 5.5:
      - session_duration and total_packets are used if provided by the Gateway;
        otherwise default to 0.0.
    """
    row: Dict[str, float] = {
        "data_length": float(features.data_length),
        "protocol_encoded": float(encode_protocol(features.protocol)),
        "packet_rate": float(features.packet_rate),
        "avg_packet_size": float(features.avg_packet_size),
        "destination_diversity": float(features.destination_diversity),
        "failed_logins": float(features.failed_logins),
        "session_duration": float(getattr(features, "session_duration", 0.0) or 0.0),
        "total_packets": float(getattr(features, "total_packets", 0.0) or 0.0),
    }
    vector = np.array([[row[name] for name in FEATURE_ORDER]], dtype=np.float64)
    return vector


def normalize_features(vector: np.ndarray, scaler) -> np.ndarray:
    """Apply the fitted StandardScaler. Raises ValueError on dimension mismatch."""
    if vector.shape[1] != len(FEATURE_ORDER):
        raise ValueError(
            f"Feature dimension mismatch: expected {len(FEATURE_ORDER)}, got {vector.shape[1]}"
        )
    return scaler.transform(vector)
