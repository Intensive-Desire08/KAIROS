"""
Model evaluation for the KAIROS Isolation Forest model.
See Technical Specification — Module 3, Section 12.4 (Model Validation Testing).
"""

from typing import Dict

import numpy as np
import pandas as pd

from ml_service.constants import FEATURE_ORDER
from ml_service.model import calculate_confidence, score_to_threat_score


def calculate_fpr(model, scaler, benign_val_df: pd.DataFrame, threshold: float = 0.50) -> float:
    """
    False positive rate on held-out benign validation data.
    Target per Section 4.3: < 5%.
    """
    X = scaler.transform(benign_val_df[FEATURE_ORDER].values)
    raw_scores = model.score_samples(X)
    threat_scores = np.array([score_to_threat_score(r) for r in raw_scores])
    false_positives = (threat_scores > threshold).sum()
    return float(false_positives / len(threat_scores))


def calculate_detection_rate(model, scaler, attack_df: pd.DataFrame, threshold: float = 0.50) -> float:
    """
    Anomaly detection rate on synthetic/labeled attack traffic.
    Target per Section 12.4: > 90%.
    """
    X = scaler.transform(attack_df[FEATURE_ORDER].values)
    raw_scores = model.score_samples(X)
    threat_scores = np.array([score_to_threat_score(r) for r in raw_scores])
    detected = (threat_scores > threshold).sum()
    return float(detected / len(threat_scores))


def evaluate_confidence_distribution(model, scaler, attack_df: pd.DataFrame) -> Dict[str, float]:
    """Sanity-check that confidence is generally high (>0.5) for detected anomalies."""
    X = scaler.transform(attack_df[FEATURE_ORDER].values)
    raw_scores = model.score_samples(X)
    confidences = np.array([calculate_confidence(r) for r in raw_scores])
    return {
        "mean_confidence": float(confidences.mean()),
        "median_confidence": float(np.median(confidences)),
        "pct_above_0.5": float((confidences > 0.5).mean()),
    }


def evaluate_attack_label_precision(true_labels: pd.Series, predicted_labels: pd.Series) -> Dict[str, float]:
    """
    Precision per attack type for the rule-based labeling engine.
    Targets per Section 12.4: DOS > 0.90, BRUTE_FORCE > 0.85, PORT_SCAN > 0.80.
    """
    results = {}
    for label in ["DOS", "BRUTE_FORCE", "PORT_SCAN"]:
        predicted_positive = predicted_labels == label
        if predicted_positive.sum() == 0:
            results[label] = 0.0
            continue
        true_positive = ((predicted_labels == label) & (true_labels == label)).sum()
        results[label] = float(true_positive / predicted_positive.sum())
    return results


def full_evaluation_report(model, scaler, benign_val_df: pd.DataFrame, attack_df: pd.DataFrame) -> Dict:
    report = {
        "false_positive_rate": calculate_fpr(model, scaler, benign_val_df),
        "detection_rate": calculate_detection_rate(model, scaler, attack_df),
        "confidence": evaluate_confidence_distribution(model, scaler, attack_df),
    }
    if "true_label" in attack_df.columns:
        from ml_service.model import identify_attack_label

        try:
            from ml_service.schemas import ThreatFeatures

            def _make_features(row):
                return ThreatFeatures(
                    data_length=int(row["data_length"]),
                    protocol="websocket",
                    packet_rate=float(row["packet_rate"]),
                    avg_packet_size=float(row["avg_packet_size"]),
                    destination_diversity=int(row["destination_diversity"]),
                    failed_logins=int(row["failed_logins"]),
                )
        except ImportError:
            # pydantic not installed in this environment (e.g. training-only
            # setup) — fall back to a minimal duck-typed feature holder,
            # since identify_attack_label() only reads plain attributes.
            class _SimpleFeatures:
                def __init__(self, packet_rate, failed_logins, destination_diversity):
                    self.packet_rate = packet_rate
                    self.failed_logins = failed_logins
                    self.destination_diversity = destination_diversity

            def _make_features(row):
                return _SimpleFeatures(
                    packet_rate=float(row["packet_rate"]),
                    failed_logins=int(row["failed_logins"]),
                    destination_diversity=int(row["destination_diversity"]),
                )

        predicted = [identify_attack_label(_make_features(row)) for _, row in attack_df.iterrows()]
        report["label_precision"] = evaluate_attack_label_precision(
            attack_df["true_label"], pd.Series(predicted)
        )
    report["passes_fpr_target"] = report["false_positive_rate"] < 0.05
    return report
