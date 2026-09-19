"""
Dataset loading for KAIROS ML Service training.
See Technical Specification — Module 3, Section 4.3.

Primary dataset: CIC-IDS2017 (benign traffic only).
Since that dataset must be downloaded separately (see README in
python/data/cic_ids_2017/), this loader also provides a synthetic benign
traffic generator so the pipeline is runnable out-of-the-box for
development/testing. Swap in the real CSV via --data to train on
CIC-IDS2017 for production.
"""

from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd

from ml_service.constants import FEATURE_ORDER

RNG_SEED = 42


def load_csv(path: str) -> pd.DataFrame:
    """Load a preprocessed benign-only traffic CSV with FEATURE_ORDER columns."""
    df = pd.read_csv(path)
    missing = set(FEATURE_ORDER) - set(df.columns)
    if missing:
        raise ValueError(f"Dataset is missing required columns: {missing}")
    return df[FEATURE_ORDER].copy()


def generate_synthetic_benign(n_samples: int = 50_000, seed: int = RNG_SEED) -> pd.DataFrame:
    """
    Generate synthetic "benign" network traffic consistent with the
    feature schema (Section 4.4), for development/testing when
    CIC-IDS2017 is not available locally.

    Distributions are chosen to resemble normal, low-volume session
    traffic: modest packet rates, low failed logins, low destination
    diversity, and a mix of protocols.
    """
    rng = np.random.default_rng(seed)

    data_length = rng.normal(loc=300, scale=120, size=n_samples).clip(1, 2000)
    protocol_encoded = rng.choice([0, 1, 2, 3], size=n_samples, p=[0.55, 0.30, 0.10, 0.05])
    packet_rate = rng.gamma(shape=2.0, scale=8.0, size=n_samples).clip(0, 300)
    avg_packet_size = rng.normal(loc=256, scale=80, size=n_samples).clip(1, 1500)
    destination_diversity = rng.poisson(lam=1.5, size=n_samples).clip(0, 15)
    failed_logins = rng.poisson(lam=0.1, size=n_samples).clip(0, 5)
    session_duration = rng.exponential(scale=180, size=n_samples).clip(0, 3600)
    total_packets = (packet_rate * session_duration / 10).clip(0, 100_000)

    df = pd.DataFrame(
        {
            "data_length": data_length,
            "protocol_encoded": protocol_encoded,
            "packet_rate": packet_rate,
            "avg_packet_size": avg_packet_size,
            "destination_diversity": destination_diversity,
            "failed_logins": failed_logins,
            "session_duration": session_duration,
            "total_packets": total_packets,
        }
    )
    return df[FEATURE_ORDER]


def generate_synthetic_attacks(n_samples: int = 3000, seed: int = RNG_SEED + 1) -> pd.DataFrame:
    """
    Generate synthetic attack traffic (DOS / BRUTE_FORCE / PORT_SCAN) for
    validation of the rule-based labeling logic and for the Isolation
    Forest's detection-rate sanity checks. Not used for unsupervised
    training (Section 4.3: training is benign-only).
    """
    rng = np.random.default_rng(seed)
    n_each = n_samples // 3

    # DOS: extreme packet_rate
    dos = generate_synthetic_benign(n_each, seed=seed)
    dos["packet_rate"] = rng.uniform(1200, 5000, size=n_each)
    dos["total_packets"] = dos["packet_rate"] * dos["session_duration"] / 10

    # BRUTE_FORCE: high failed_logins
    brute = generate_synthetic_benign(n_each, seed=seed + 1)
    brute["failed_logins"] = rng.integers(11, 50, size=n_each)

    # PORT_SCAN: high destination_diversity
    scan = generate_synthetic_benign(n_each, seed=seed + 2)
    scan["destination_diversity"] = rng.integers(51, 200, size=n_each)

    labels = (["DOS"] * n_each) + (["BRUTE_FORCE"] * n_each) + (["PORT_SCAN"] * n_each)
    df = pd.concat([dos, brute, scan], ignore_index=True)
    df["true_label"] = labels
    return df


def train_val_split(df: pd.DataFrame, val_fraction: float = 0.20, seed: int = RNG_SEED):
    """80/20 split per Section 4.3."""
    shuffled = df.sample(frac=1.0, random_state=seed).reset_index(drop=True)
    split_idx = int(len(shuffled) * (1 - val_fraction))
    return shuffled.iloc[:split_idx], shuffled.iloc[split_idx:]
