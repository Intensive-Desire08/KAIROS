import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ml_service import model as model_module
from ml_service.constants import SecurityLevel


class DummyFeatures:
    def __init__(self, packet_rate=10.0, failed_logins=0, destination_diversity=1):
        self.packet_rate = packet_rate
        self.failed_logins = failed_logins
        self.destination_diversity = destination_diversity


@pytest.fixture(autouse=True)
def reset_calibration():
    """Ensure a known calibration for deterministic threat-score tests."""
    model_module.load_calibration({"score_calibration": {"score_median": -0.4, "score_p01": -0.8}})
    yield
    model_module.load_calibration({"score_calibration": {"score_median": 0.0, "score_p01": -0.05}})


def test_score_to_threat_score_at_median_is_zero():
    assert model_module.score_to_threat_score(-0.4) == pytest.approx(0.0, abs=1e-6)


def test_score_to_threat_score_at_floor_is_one():
    assert model_module.score_to_threat_score(-0.8) == pytest.approx(1.0, abs=1e-6)


def test_score_to_threat_score_beyond_floor_clips_to_one():
    assert model_module.score_to_threat_score(-1.5) == 1.0


def test_score_to_threat_score_above_median_clips_to_zero():
    assert model_module.score_to_threat_score(0.2) == 0.0


def test_calculate_confidence_at_median_is_low():
    assert model_module.calculate_confidence(-0.4) == pytest.approx(0.0, abs=1e-6)


def test_calculate_confidence_far_from_median_is_high():
    assert model_module.calculate_confidence(-0.8) == pytest.approx(1.0, abs=1e-6)


# --- Level mapping (Section 5.4) ---

def test_map_score_to_level_green():
    assert model_module.map_score_to_level(0.10) == SecurityLevel.GREEN


def test_map_score_to_level_yellow():
    assert model_module.map_score_to_level(0.40) == SecurityLevel.YELLOW


def test_map_score_to_level_orange():
    assert model_module.map_score_to_level(0.60) == SecurityLevel.ORANGE


def test_map_score_to_level_red():
    assert model_module.map_score_to_level(0.90) == SecurityLevel.RED


# --- Attack labeling (Section 4.5 / 5.6) ---

def test_identify_attack_label_dos():
    features = DummyFeatures(packet_rate=1500)
    assert model_module.identify_attack_label(features) == "DOS"


def test_identify_attack_label_bruteforce():
    features = DummyFeatures(packet_rate=10, failed_logins=15)
    assert model_module.identify_attack_label(features) == "BRUTE_FORCE"


def test_identify_attack_label_portscan():
    features = DummyFeatures(packet_rate=10, failed_logins=0, destination_diversity=60)
    assert model_module.identify_attack_label(features) == "PORT_SCAN"


def test_identify_attack_label_none():
    features = DummyFeatures(packet_rate=10, failed_logins=0, destination_diversity=1)
    assert model_module.identify_attack_label(features) is None


def test_dos_takes_priority_over_bruteforce_and_portscan():
    """Execution order per Section 4.5: DOS checked first."""
    features = DummyFeatures(packet_rate=1500, failed_logins=20, destination_diversity=100)
    assert model_module.identify_attack_label(features) == "DOS"


def test_label_activation_threshold_suppresses_label_below_0_5():
    """Section 4.5: labels only apply if threat_score > 0.5."""
    result = model_module.apply_label_activation_threshold("DOS", threat_score=0.3)
    assert result is None


def test_label_activation_threshold_allows_label_above_0_5():
    result = model_module.apply_label_activation_threshold("DOS", threat_score=0.6)
    assert result == "DOS"
