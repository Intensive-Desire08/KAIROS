"""
Constants and enums for the KAIROS ML Threat Detection Service.
See Technical Specification — Module 3, Section 4.
"""

from enum import Enum


class SecurityLevel(str, Enum):
    GREEN = "green"
    YELLOW = "yellow"
    ORANGE = "orange"
    RED = "red"


class AttackLabel(str, Enum):
    DOS = "DOS"
    BRUTE_FORCE = "BRUTE_FORCE"
    PORT_SCAN = "PORT_SCAN"


# Protocol encoding (Section 4.4) — must stay consistent with Gateway + XGBoost (Section 4.7)
PROTOCOL_ENCODING = {
    "websocket": 0,
    "tcp": 1,
    "udp": 2,
    "icmp": 3,
}

# Canonical feature order fed into the scaler / model (Section 4.4 / 5.5)
FEATURE_ORDER = [
    "data_length",
    "protocol_encoded",
    "packet_rate",
    "avg_packet_size",
    "destination_diversity",
    "failed_logins",
    "session_duration",
    "total_packets",
]

# Default threshold values (overridden by config.json — Section 4.3 / 8.2)
DEFAULT_THRESHOLDS = {
    "green": 0.25,
    "yellow": 0.50,
    "orange": 0.75,
    "red": 1.00,
}

DEFAULT_ATTACK_LABEL_THRESHOLDS = {
    "dos_threshold": 1000,
    "bruteforce_threshold": 10,
    "portscan_threshold": 50,
    "activation_threshold": 0.50,
}

LEVEL_COLORS = {
    SecurityLevel.GREEN: "#00FF00",
    SecurityLevel.YELLOW: "#FFFF00",
    SecurityLevel.ORANGE: "#FFA500",
    SecurityLevel.RED: "#FF0000",
}

# Error codes (Section 10.11)
class ErrorCode(str, Enum):
    MISSING_FIELD = "MISSING_FIELD"
    INVALID_FIELD = "INVALID_FIELD"
    INVALID_FEATURE = "INVALID_FEATURE"
    MODEL_NOT_LOADED = "MODEL_NOT_LOADED"
    MODEL_LOAD_FAILED = "MODEL_LOAD_FAILED"
    INFERENCE_FAILED = "INFERENCE_FAILED"
    INTERNAL_ERROR = "INTERNAL_ERROR"
