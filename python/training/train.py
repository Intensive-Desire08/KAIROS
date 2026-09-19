"""
Model training script for the KAIROS ML Threat Detection Service.
See Technical Specification — Module 3, Section 4.2, 4.3, 4.8, 15.8.

Usage:
    python training/train.py                          # synthetic data (dev)
    python training/train.py --data ./data/benign.csv # real CIC-IDS2017-derived CSV
"""

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import joblib
from sklearn.ensemble import IsolationForest
from sklearn.preprocessing import StandardScaler

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # allow `import ml_service`

from ml_service.constants import FEATURE_ORDER  # noqa: E402
from training.data_loader import (  # noqa: E402
    generate_synthetic_attacks,
    generate_synthetic_benign,
    load_csv,
    train_val_split,
)
from training.evaluate import full_evaluation_report  # noqa: E402
from training.preprocess import preprocess_pipeline  # noqa: E402

MODEL_PARAMS = {
    "n_estimators": 100,
    "contamination": 0.01,
    "max_samples": "auto",
    "random_state": 42,
    "bootstrap": False,
    "n_jobs": -1,
    "warm_start": False,
}

MODELS_DIR = Path(__file__).resolve().parents[1] / "models"


def train_model(train_df, params: dict = MODEL_PARAMS):
    """Fit StandardScaler + IsolationForest on benign-only training data (Section 4.2)."""
    scaler = StandardScaler()
    X_train = scaler.fit_transform(train_df[FEATURE_ORDER].values)

    model = IsolationForest(**params)
    model.fit(X_train)
    return model, scaler


def compute_score_calibration(model, scaler, benign_df) -> dict:
    """
    Compute score calibration reference points from benign training data.
    See ml_service/model.py: score_to_threat_score() for how these are used.
    """
    import numpy as np

    X = scaler.transform(benign_df[FEATURE_ORDER].values)
    raw_scores = model.score_samples(X)
    return {
        "score_median": float(np.median(raw_scores)),
        # Use a more extreme percentile than the raw contamination rate
        # as the calibration floor. This widens the normal->anomalous
        # spread so held-out (out-of-sample) benign traffic doesn't drift
        # past the mid-range threat_score thresholds as easily — in-sample
        # percentiles alone tend to underestimate out-of-sample variance.
        "score_p01": float(np.percentile(raw_scores, 0.05)),
    }


def save_model(model, scaler, metadata: dict, models_dir: Path = MODELS_DIR) -> None:
    """Persist model, scaler, and metadata.json (Section 4.8 / 9.2)."""
    models_dir.mkdir(parents=True, exist_ok=True)
    joblib.dump(model, models_dir / "isolation_forest.pkl")
    joblib.dump(scaler, models_dir / "scaler.pkl")
    with open(models_dir / "metadata.json", "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2)
    print(f"[train] Saved model, scaler, and metadata to {models_dir}")


def main():
    parser = argparse.ArgumentParser(description="Train KAIROS Isolation Forest threat model")
    parser.add_argument("--data", type=str, default=None, help="Path to benign traffic CSV (CIC-IDS2017-derived)")
    parser.add_argument("--label-col", type=str, default="Label", help="Label column name if filtering raw CSV")
    parser.add_argument("--n-synthetic", type=int, default=50_000, help="Synthetic benign sample count (dev mode)")
    parser.add_argument("--out", type=str, default=str(MODELS_DIR), help="Output directory for model artifacts")
    args = parser.parse_args()

    if args.data:
        print(f"[train] Loading dataset from {args.data}")
        raw_df = load_csv(args.data) if _has_only_feature_cols(args.data) else _load_raw(args.data)
        df = preprocess_pipeline(raw_df, label_col=args.label_col)
        dataset_name = Path(args.data).stem
    else:
        print(f"[train] No --data provided; generating {args.n_synthetic} synthetic benign samples (dev mode)")
        df = generate_synthetic_benign(args.n_synthetic)
        dataset_name = "synthetic-benign-dev"

    train_df, val_df = train_val_split(df, val_fraction=0.20)
    print(f"[train] Train size: {len(train_df)}, Validation size: {len(val_df)}")

    model, scaler = train_model(train_df)
    print("[train] Isolation Forest trained.")

    score_calibration = compute_score_calibration(model, scaler, train_df)
    print(f"[train] Score calibration: {score_calibration}")

    # Apply calibration in-process so the evaluation below reflects the
    # same threat_score conversion the live service will use.
    from ml_service.model import load_calibration

    load_calibration({"score_calibration": score_calibration})

    # Validate against held-out benign + synthetic attacks (Section 4.3, 12.4)
    attack_df = generate_synthetic_attacks(3000)
    report = full_evaluation_report(model, scaler, val_df, attack_df)
    print("[train] Evaluation report:")
    print(json.dumps(report, indent=2))

    if not report["passes_fpr_target"]:
        print(
            f"[train] WARNING: false_positive_rate={report['false_positive_rate']:.4f} "
            "exceeds the 5% target (Section 4.3). Consider tuning `contamination` "
            "or reviewing training data quality before deploying this model."
        )

    metadata = {
        "version": "1.0.0",
        "trained_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "dataset": dataset_name,
        "features": FEATURE_ORDER,
        "parameters": MODEL_PARAMS,
        "validation": {
            "false_positive_rate": report["false_positive_rate"],
            "test_size": 0.20,
        },
        "score_calibration": score_calibration,
        "training_data": {
            "samples": len(train_df),
            "features": len(FEATURE_ORDER),
        },
    }

    save_model(model, scaler, metadata, models_dir=Path(args.out))


def _has_only_feature_cols(path: str) -> bool:
    import pandas as pd

    header = pd.read_csv(path, nrows=0).columns.tolist()
    return set(FEATURE_ORDER).issubset(set(header)) and "Label" not in header


def _load_raw(path: str):
    import pandas as pd

    return pd.read_csv(path)


if __name__ == "__main__":
    main()
