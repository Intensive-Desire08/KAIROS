"""
Model loading and inference for the KAIROS ML Threat Detection Service.
See Technical Specification — Module 3, Section 4, 5.4, 5.5, 5.6.
"""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Optional, Tuple

import joblib
import numpy as np

from .config import get_attack_label_config, get_thresholds
from .constants import AttackLabel, SecurityLevel

if TYPE_CHECKING:
    # Only needed for type hints; avoids a hard pydantic dependency for
    # non-API consumers of this module (e.g. training/evaluate.py).
    from .schemas import ThreatFeatures

MODELS_DIR = Path(__file__).resolve().parents[1] / "models"
MODEL_PATH = MODELS_DIR / "isolation_forest.pkl"
SCALER_PATH = MODELS_DIR / "scaler.pkl"


class ModelState:
    """Holds the loaded model + scaler as process-wide state (Section 5.9 startup)."""

    def __init__(self) -> None:
        self.model = None
        self.scaler = None
        self.model_loaded: bool = False
        self.load_error: Optional[str] = None

    def load(self, model_path: Path = MODEL_PATH, scaler_path: Path = SCALER_PATH) -> None:
        try:
            self.model = joblib.load(model_path)
            self.scaler = joblib.load(scaler_path)
            self.model_loaded = True
            self.load_error = None

            metadata_path = model_path.parent / "metadata.json"
            if metadata_path.is_file():
                import json

                with open(metadata_path, "r", encoding="utf-8") as f:
                    load_calibration(json.load(f))
        except FileNotFoundError as exc:
            self.model_loaded = False
            self.load_error = f"Model file not found: {exc}"
        except Exception as exc:  # pickle errors, incompatible sklearn version, etc.
            self.model_loaded = False
            self.load_error = f"Model load failed: {exc}"


# Process-wide singleton, populated by FastAPI startup event in app.py
model_state = ModelState()


def raw_anomaly_score(features_scaled: np.ndarray) -> float:
    """
    Returns the raw Isolation Forest anomaly score via score_samples().
    More negative = more anomalous (Section 5.4).
    """
    if model_state.model is None:
        raise RuntimeError("Model is not loaded")
    return float(model_state.model.score_samples(features_scaled)[0])


# Calibration reference points, populated from training/metadata.json at
# startup (see load_calibration()). Falls back to uncalibrated defaults if
# unavailable so the service can still start (Section 10.3).
_calibration = {"score_median": 0.0, "score_p01": -0.05}


def load_calibration(metadata: dict) -> None:
    """Load score calibration reference points written by training/train.py."""
    cal = metadata.get("score_calibration")
    if cal and "score_median" in cal and "score_p01" in cal:
        _calibration["score_median"] = cal["score_median"]
        _calibration["score_p01"] = cal["score_p01"]


def score_to_threat_score(raw_score: float) -> float:
    """
    Convert the raw Isolation Forest anomaly score to a calibrated 0.0-1.0
    threat_score (Section 5.5 step 4).

    RATIONALE (deviation from the spec's literal pseudocode formula):
    Isolation Forest's score_samples() output is not naturally centered at
    0 or bounded the way a plain sigmoid assumes — for a given dataset it
    typically clusters in a narrow band (e.g. -0.7 to -0.3), so applying
    `1/(1+exp(raw_score))` directly saturates every point toward the same
    threat_score regardless of how anomalous it actually is. Instead we
    calibrate against the training set's own score distribution:

      normalized = (score_median - raw_score) / (score_median - score_p01)

    where `score_median` is the median raw score of benign training data
    (normalized -> 0) and `score_p01` is its 1st percentile (normalized ->
    1, i.e. roughly the contamination-rate boundary). Scores more anomalous
    than the 1st percentile naturally push normalized above 1 and clip to
    a maximum threat_score of 1.0.
    """
    median = _calibration["score_median"]
    p01 = _calibration["score_p01"]
    spread = median - p01
    if spread <= 1e-9:
        # Degenerate calibration (e.g. no variance in training scores) —
        # fall back to a simple sign-based heuristic rather than dividing
        # by ~zero.
        return 1.0 if raw_score < median else 0.0
    normalized = (median - raw_score) / spread
    return float(np.clip(normalized, 0.0, 1.0))


def calculate_confidence(raw_score: float) -> float:
    """
    Confidence per Section 4.6: the model's certainty in its anomaly
    assessment, derived from the raw Isolation Forest score.

    Calibrated the same way as score_to_threat_score(): confidence grows
    with how far raw_score sits from the training set's typical (median)
    benign score, relative to the same spread used for threat scoring.
    A point right at the median (perfectly typical traffic) yields low
    confidence-of-anomaly; a point far into anomalous territory yields
    confidence approaching 1.0. This keeps confidence meaningful for the
    DTRE's low-confidence RED override (Section 4.6 / 6.4).
    """
    median = _calibration["score_median"]
    p01 = _calibration["score_p01"]
    spread = median - p01
    if spread <= 1e-9:
        return 0.0
    confidence = abs(median - raw_score) / spread
    return float(np.clip(confidence, 0.0, 1.0))


def map_score_to_level(threat_score: float) -> SecurityLevel:
    """
    Map threat_score to green/yellow/orange/red using configured thresholds
    (Section 5.4).

    AMBIGUITY NOTE: the spec itself is inconsistent at the boundaries.
    Section 5.4's display table shows discrete ranges ("0.50 - 0.74 orange",
    "0.75 - 1.00 red"), which would put a score of exactly 0.75 in RED.
    But Section 8.2's config schema describes `orange: 0.75` as the
    "Upper bound for ORANGE level", which would put 0.75 in ORANGE.
    This implementation follows the Section 8.2 config description (each
    threshold is an inclusive upper bound via `<=`), since the 5.4 table's
    two-decimal ranges read as a rounded/illustrative display rather than
    an exact boundary spec. In practice this only matters for a score of
    exactly 0.75 (vanishingly unlikely with a continuous float score) —
    flagging it here for the same reason Section 5.6 flags the
    failed_logins ambiguity: so the resolution is visible, not silent.
    """
    thresholds = get_thresholds()
    if threat_score <= thresholds.get("green", 0.25):
        return SecurityLevel.GREEN
    if threat_score <= thresholds.get("yellow", 0.50):
        return SecurityLevel.YELLOW
    if threat_score <= thresholds.get("orange", 0.75):
        return SecurityLevel.ORANGE
    return SecurityLevel.RED


def identify_attack_label(features: ThreatFeatures) -> Optional[str]:
    """
    Rule-based attack labeling (Section 4.5 / 5.6).
    Execution order: DOS -> BRUTE_FORCE -> PORT_SCAN -> None.
    """
    cfg = get_attack_label_config()
    if features.packet_rate > cfg.get("dos_threshold", 1000):
        return AttackLabel.DOS.value
    if features.failed_logins > cfg.get("bruteforce_threshold", 10):
        return AttackLabel.BRUTE_FORCE.value
    if features.destination_diversity > cfg.get("portscan_threshold", 50):
        return AttackLabel.PORT_SCAN.value
    return None


def apply_label_activation_threshold(label: Optional[str], threat_score: float) -> Optional[str]:
    """
    Section 4.5: labels only apply if threat_score > activation_threshold (default 0.5).
    Prevents false labels from triggering attack-specific actions.
    """
    cfg = get_attack_label_config()
    activation_threshold = cfg.get("activation_threshold", 0.50)
    if threat_score <= activation_threshold:
        return None
    return label


def analyze(features: ThreatFeatures, feature_vector_scaled: np.ndarray) -> Tuple[float, SecurityLevel, float, Optional[str]]:
    """
    Full inference pipeline (Section 5.5):
      1. Isolation Forest inference -> threat_score
      2. Confidence calculation
      3. Rule-based attack labeling + activation threshold
      4. Level mapping

    Returns: (threat_score, level, confidence, attack_label)
    """
    raw_score = raw_anomaly_score(feature_vector_scaled)
    threat_score = score_to_threat_score(raw_score)
    confidence = calculate_confidence(raw_score)

    label = identify_attack_label(features)
    label = apply_label_activation_threshold(label, threat_score)

    level = map_score_to_level(threat_score)

    return threat_score, level, confidence, label
