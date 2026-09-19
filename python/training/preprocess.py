"""
Data preprocessing utilities for KAIROS ML Service training.
See Technical Specification — Module 3, Section 4.3 (Preprocessing).
"""

import pandas as pd

from ml_service.constants import FEATURE_ORDER


def remove_duplicates(df: pd.DataFrame) -> pd.DataFrame:
    before = len(df)
    df = df.drop_duplicates().reset_index(drop=True)
    removed = before - len(df)
    if removed:
        print(f"[preprocess] Removed {removed} duplicate rows")
    return df


def handle_missing_values(df: pd.DataFrame) -> pd.DataFrame:
    before = len(df)
    df = df.dropna(subset=FEATURE_ORDER).reset_index(drop=True)
    removed = before - len(df)
    if removed:
        print(f"[preprocess] Dropped {removed} rows with missing values")
    return df


def filter_benign_only(df: pd.DataFrame, label_col: str = "Label") -> pd.DataFrame:
    """
    For raw CIC-IDS2017 CSVs that include a label column, filter to
    benign traffic only, per Section 4.3 ("Training on benign only
    ensures the model learns normal behavior exclusively").
    """
    if label_col not in df.columns:
        return df  # already benign-only / synthetic data with no label column
    mask = df[label_col].astype(str).str.upper().str.contains("BENIGN")
    return df[mask].drop(columns=[label_col]).reset_index(drop=True)


def clip_to_valid_ranges(df: pd.DataFrame) -> pd.DataFrame:
    """Clip features to the valid ranges documented in Section 4.4."""
    clipped = df.copy()
    clipped["data_length"] = clipped["data_length"].clip(0, 65535)
    clipped["protocol_encoded"] = clipped["protocol_encoded"].clip(0, 3)
    clipped["packet_rate"] = clipped["packet_rate"].clip(lower=0)
    clipped["avg_packet_size"] = clipped["avg_packet_size"].clip(0, 65535)
    clipped["destination_diversity"] = clipped["destination_diversity"].clip(0, 255)
    clipped["failed_logins"] = clipped["failed_logins"].clip(lower=0)
    clipped["session_duration"] = clipped["session_duration"].clip(0, 3600)
    clipped["total_packets"] = clipped["total_packets"].clip(lower=0)
    return clipped[FEATURE_ORDER]


def preprocess_pipeline(df: pd.DataFrame, label_col: str = "Label") -> pd.DataFrame:
    df = filter_benign_only(df, label_col=label_col)
    df = remove_duplicates(df)
    df = handle_missing_values(df)
    df = clip_to_valid_ranges(df)
    return df
