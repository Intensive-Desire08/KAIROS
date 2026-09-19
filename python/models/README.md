# KAIROS ML Models

This directory holds the trained artifacts loaded by the ML Service at startup
(`ml_service/model.py :: ModelState.load()`):

| File | Description |
|---|---|
| `isolation_forest.pkl` | Trained `sklearn.ensemble.IsolationForest` |
| `scaler.pkl` | Fitted `sklearn.preprocessing.StandardScaler` |
| `metadata.json` | Version, training dataset, hyperparameters, validation FPR, and score calibration |

## Regenerating

```bash
cd python
python training/train.py                      # synthetic benign data (dev/demo)
python training/train.py --data path/to.csv    # real CIC-IDS2017-derived benign CSV
```

## Score calibration

`metadata.json` includes a `score_calibration` block (`score_median`, `score_p01`)
computed from the training set's own Isolation Forest scores. `ml_service/model.py`
uses these to convert `score_samples()` output into the 0.0–1.0 `threat_score` —
see the docstring on `score_to_threat_score()` for why a plain sigmoid on the raw
score (as in the spec's Section 5.5 pseudocode) doesn't work in practice and this
calibrated approach is used instead.

**Do not delete `metadata.json` without retraining** — without it the service falls
back to uncalibrated defaults and threat scores will be meaningless.
