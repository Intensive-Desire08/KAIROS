"""
KAIROS ML Threat Detection — Interactive CLI Test Bench
Provides a command-line menu interface to test model inference, preset attack scenarios,
custom flow telemetry, ASCII & GUI timeline graphing, score distribution curves, and retraining.
"""

import os
import sys
import time
from pathlib import Path

# Add python directory to sys.path
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np
import pandas as pd

from ml_service.constants import FEATURE_ORDER
from ml_service.features import build_feature_vector, normalize_features
from ml_service.model import (
    _calibration,
    analyze,
    calculate_confidence,
    map_score_to_level,
    model_state,
    raw_anomaly_score,
    score_to_threat_score,
)
from ml_service.schemas import ThreatFeatures, ThreatRequest

def ensure_model_loaded():
    if not model_state.model_loaded:
        model_state.load()

def predict_threat(req: ThreatRequest) -> dict:
    ensure_model_loaded()
    vector = build_feature_vector(req.features)
    scaled = normalize_features(vector, model_state.scaler)
    raw = raw_anomaly_score(scaled)
    threat_score, level, confidence, attack_label = analyze(req.features, scaled)
    return {
        "raw_anomaly_score": raw,
        "threat_score": threat_score,
        "level": level.value if hasattr(level, "value") else str(level),
        "confidence": confidence,
        "attack_label": attack_label,
    }

# Check if matplotlib is available
try:
    import matplotlib
    import matplotlib.pyplot as plt
    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False

# Ensure Windows stdout supports UTF-8 where possible
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass


def print_banner():
    print("""
+=====================================================================+
|              KAIROS ML THREAT DETECTION -- CLI BENCH                |
|          Isolation Forest Anomaly Detection & Telemetry             |
+=====================================================================+
""")


def print_gauge(score: float, level: str, width: int = 24):
    filled = int(round(score * width))
    bar = "#" * filled + "-" * (width - filled)
    pct = score * 100.0
    print(f"Risk Meter: [{bar}] {pct:5.1f}% [{level.upper()}]")


def render_ascii_timeline(scores: list[float], height: int = 10, width: int = 50):
    """Draws an ASCII timeline of threat scores [0.0, 1.0]."""
    print("\n--- ASCII Threat Score Timeline (50 Consecutive Flows) ---")
    grid = [[" " for _ in range(len(scores))] for _ in range(height)]
    
    # 0 is top (score 1.0), height-1 is bottom (score 0.0)
    for col, s in enumerate(scores):
        row = height - 1 - int(round(s * (height - 1)))
        row = max(0, min(height - 1, row))
        grid[row][col] = "^" if s >= 0.50 else "."
        # Fill under curve
        for r in range(row + 1, height):
            if grid[r][col] == " ":
                grid[r][col] = "|" if s >= 0.50 else " "

    for r in range(height):
        tag = ""
        if r == 0:
            tag = " 1.0 [RED]   "
        elif r == int(round(0.25 * (height - 1))):
            tag = " 0.75 [ORNG] "
        elif r == int(round(0.50 * (height - 1))):
            tag = " 0.50 [YLLW] "
        elif r == int(round(0.75 * (height - 1))):
            tag = " 0.25 [GREN] "
        elif r == height - 1:
            tag = " 0.0 [SAFE] "
        else:
            tag = "            "
        line = "".join(grid[r])
        print(f"{tag}|{line}")
    print("            +" + "-" * len(scores))
    print("             Flow 0" + " " * max(1, (len(scores) - 15)) + "Flow " + str(len(scores)))


def render_ascii_histogram(data: np.ndarray, bins: int = 10, title: str = "Distribution"):
    """Draws an ASCII histogram of data array."""
    counts, edges = np.histogram(data, bins=bins)
    max_c = max(counts) if len(counts) > 0 and max(counts) > 0 else 1
    print(f"\n--- ASCII Histogram: {title} ---")
    for i in range(bins):
        low, high = edges[i], edges[i+1]
        bar_len = int(round((counts[i] / max_c) * 30))
        bar = "=" * bar_len
        print(f"  [{low:5.2f} to {high:5.2f}] | {bar:<30} ({counts[i]})")


# ============================================================================
# Feature 1: Model Status
# ============================================================================
def view_model_status():
    print("\n--- [1] Model Architecture & Status ---")
    ensure_model_loaded()
    
    print(f"  Model Loaded    : {'YES' if model_state.model_loaded else 'NO'}")
    if model_state.model_loaded:
        m = model_state.model
        print(f"  Algorithm       : {type(m).__name__}")
        print(f"  Estimators (n)  : {getattr(m, 'n_estimators', 'N/A')}")
        print(f"  Contamination   : {getattr(m, 'contamination', 'N/A')}")
        print(f"  Calibration     : Median={_calibration.get('score_median', 0.0):.4f}, P01={_calibration.get('score_p01', 0.0):.4f}")
        print(f"  Feature Order ({len(FEATURE_ORDER)} dims):")
        for i, f in enumerate(FEATURE_ORDER):
            print(f"    [{i+1}] {f}")
        
        meta_path = Path(__file__).resolve().parent / "models" / "metadata.json"
        if meta_path.exists():
            import json
            with open(meta_path) as fp:
                meta = json.load(fp)
                print(f"  Training Date   : {meta.get('trained_at', 'N/A')}")
                print(f"  FPR Benchmark   : {meta.get('evaluation', {}).get('false_positive_rate', 'N/A')}")


# ============================================================================
# Feature 2: Preset Scenarios
# ============================================================================
def run_preset_scenarios():
    print("\n--- [2] Real-Time Threat Prediction: Preset Scenarios ---")
    scenarios = [
        {
            "name": "Normal Ambient Telemetry (Standard IoT Keepalive)",
            "features": ThreatFeatures(
                data_length=24,
                protocol="mqtt",
                packet_rate=4.5,
                avg_packet_size=128,
                destination_diversity=1,
                failed_logins=0,
            ),
        },
        {
            "name": "Reconnaissance / Stealth Port Scanning",
            "features": ThreatFeatures(
                data_length=64,
                protocol="tcp",
                packet_rate=35.0,
                avg_packet_size=48,
                destination_diversity=65,
                failed_logins=0,
            ),
        },
        {
            "name": "High-Rate SYN Flood / Denial of Service (DoS)",
            "features": ThreatFeatures(
                data_length=1500,
                protocol="tcp",
                packet_rate=1450.0,
                avg_packet_size=60,
                destination_diversity=3,
                failed_logins=0,
            ),
        },
        {
            "name": "Credential Stuffing / SSH Brute Force Attack",
            "features": ThreatFeatures(
                data_length=320,
                protocol="ssh",
                packet_rate=22.0,
                avg_packet_size=256,
                destination_diversity=1,
                failed_logins=28,
            ),
        },
    ]

    for idx, sc in enumerate(scenarios, 1):
        print(f"  [{idx}] {sc['name']}")
    
    choice = input("\nSelect scenario [1-4] > ").strip()
    try:
        sc_idx = int(choice) - 1
        if 0 <= sc_idx < len(scenarios):
            target_sc = scenarios[sc_idx]
            req = ThreatRequest(
                session_id=f"sim_sess_{int(time.time())}",
                source="edge_node_04",
                target="gateway_base_01",
                plaintext="Telemetry Payload",
                features=target_sc["features"]
            )
            resp = predict_threat(req)
            print(f"\nScenario: {target_sc['name']}")
            print(f"  Raw Score       : {resp['raw_anomaly_score']:.4f}")
            print(f"  Threat Score    : {resp['threat_score']:.4f}")
            print(f"  Threat Level    : {resp['level'].upper()}")
            print(f"  Confidence      : {resp['confidence'] * 100.0:.1f}%")
            print(f"  Attack Label    : {resp['attack_label'] or 'None (Benign/Ambient)'}")
            print_gauge(resp['threat_score'], resp['level'])
        else:
            print("Invalid scenario index.")
    except ValueError:
        print("Invalid input.")


# ============================================================================
# Feature 3: Custom Telemetry Input
# ============================================================================
def run_custom_input():
    print("\n--- [3] Custom Telemetry Input ---")
    try:
        data_len = int(input("Data Length (bytes) [default 32]: ").strip() or "32")
        proto = input("Protocol [tcp/udp/mqtt/websocket, default tcp]: ").strip() or "tcp"
        pkt_rate = float(input("Packet Rate (pkts/sec) [default 10.0]: ").strip() or "10.0")
        avg_sz = float(input("Avg Packet Size (bytes) [default 256.0]: ").strip() or "256.0")
        dest_div = int(input("Destination Diversity (unique hosts) [default 1]: ").strip() or "1")
        fails = int(input("Failed Logins in Window [default 0]: ").strip() or "0")

        req = ThreatRequest(
            session_id="custom_sess_01",
            source="custom_device",
            target="kairos_gateway",
            features=ThreatFeatures(
                data_length=data_len,
                protocol=proto,
                packet_rate=pkt_rate,
                avg_packet_size=avg_sz,
                destination_diversity=dest_div,
                failed_logins=fails,
            )
        )
        resp = predict_threat(req)
        print("\n--- Prediction Result ---")
        print(f"  Threat Score : {resp['threat_score']:.4f} / 1.0000")
        print(f"  Threat Level : {resp['level'].upper()}")
        print(f"  Confidence   : {resp['confidence'] * 100.0:.1f}%")
        print(f"  Attack Label : {resp['attack_label'] or 'None (Benign)'}")
        print_gauge(resp['threat_score'], resp['level'])
    except Exception as e:
        print(f"Input error: {e}")


# ============================================================================
# Feature 4: Batch Simulation & Threat Timeline
# ============================================================================
def run_batch_simulation():
    print("\n--- [4] Batch Simulation & Threat Timeline (50 Flows) ---")
    print("Generating simulated sequence: Benign -> Port Scan -> DoS Flood -> Recovery...")

    scores = []
    labels = []
    times = list(range(50))

    for i in range(50):
        if i < 18:
            # Benign
            rate = float(np.random.uniform(2, 12))
            div = 1
            fails = 0
            sz = float(np.random.uniform(100, 300))
        elif 18 <= i < 28:
            # Port scan
            rate = float(np.random.uniform(30, 60))
            div = int(np.random.randint(45, 90))
            fails = 0
            sz = 52.0
        elif 28 <= i < 40:
            # DoS
            rate = float(np.random.uniform(900, 1800))
            div = 2
            fails = 0
            sz = 64.0
        else:
            # Recovery
            rate = float(np.random.uniform(5, 15))
            div = 1
            fails = 0
            sz = float(np.random.uniform(120, 250))

        req = ThreatRequest(
            session_id=f"flow_{i}",
            source="iot_cluster",
            target="base_station",
            features=ThreatFeatures(
                data_length=int(sz),
                protocol="tcp",
                packet_rate=rate,
                avg_packet_size=sz,
                destination_diversity=div,
                failed_logins=fails,
            )
        )
        resp = predict_threat(req)
        scores.append(resp["threat_score"])
        labels.append(resp["attack_label"] or resp["level"])

    # 1. Render in ASCII
    render_ascii_timeline(scores)

    # 2. Offer Matplotlib GUI Window
    if MATPLOTLIB_AVAILABLE:
        gui_choice = input("\nLaunch interactive Matplotlib plot window? [y/N] > ").strip().lower()
        if gui_choice == "y":
            plt.figure(figsize=(10, 5))
            plt.plot(times, scores, color="#2563eb", linewidth=2.5, marker="o", markersize=4, label="Threat Score")
            
            # Shading threat zones
            plt.axhspan(0.00, 0.25, color="#10b981", alpha=0.15, label="Green Zone (Normal)")
            plt.axhspan(0.25, 0.50, color="#f59e0b", alpha=0.15, label="Yellow Zone (Suspicious)")
            plt.axhspan(0.50, 0.75, color="#f97316", alpha=0.15, label="Orange Zone (Elevated)")
            plt.axhspan(0.75, 1.00, color="#ef4444", alpha=0.15, label="Red Zone (Critical)")
            
            # Annotate attack sections
            plt.axvline(18, color="#f59e0b", linestyle="--", alpha=0.7)
            plt.text(18.5, 0.60, "Port Scan Started", color="#b45309", fontsize=9, fontweight="bold")
            
            plt.axvline(28, color="#ef4444", linestyle="--", alpha=0.7)
            plt.text(28.5, 0.95, "DoS Flood Surge", color="#b91c1c", fontsize=9, fontweight="bold")
            
            plt.axvline(40, color="#10b981", linestyle="--", alpha=0.7)
            plt.text(40.5, 0.35, "Traffic Recovered", color="#047857", fontsize=9, fontweight="bold")

            plt.title("KAIROS Gateway — Real-Time Threat Score Timeline", fontsize=12, fontweight="bold")
            plt.xlabel("Flow Index (Chronological)", fontsize=10)
            plt.ylabel("Calibrated Threat Score [0.0 - 1.0]", fontsize=10)
            plt.ylim(-0.02, 1.05)
            plt.grid(True, linestyle=":", alpha=0.6)
            plt.legend(loc="upper left")
            plt.tight_layout()
            print("Opening Matplotlib GUI window... (close window to return to CLI menu)")
            plt.show()
    else:
        print("\n[Notice] matplotlib is not installed. To view GUI windows, run 'pip install matplotlib'.")


# ============================================================================
# Feature 5: Anomaly Score Distribution Curve
# ============================================================================
def run_score_distribution():
    print("\n--- [5] Anomaly Score Distribution Curve ---")
    ensure_model_loaded()
    
    from training.data_loader import generate_synthetic_attacks, generate_synthetic_benign
    
    print("Generating 1,000 benign and 300 attack samples...")
    benign_df = generate_synthetic_benign(1000)
    attack_df = generate_synthetic_attacks(300)
    
    X_benign = model_state.scaler.transform(benign_df[FEATURE_ORDER].values)
    X_attack = model_state.scaler.transform(attack_df[FEATURE_ORDER].values)
    
    benign_raw = model_state.model.score_samples(X_benign)
    attack_raw = model_state.model.score_samples(X_attack)
    
    benign_scores = np.array([score_to_threat_score(s) for s in benign_raw])
    attack_scores = np.array([score_to_threat_score(s) for s in attack_raw])
    
    render_ascii_histogram(benign_scores, bins=8, title="Benign Traffic Scores (Ideal < 0.25)")
    render_ascii_histogram(attack_scores, bins=8, title="Attack Traffic Scores (Ideal > 0.50)")

    if MATPLOTLIB_AVAILABLE:
        gui_choice = input("\nLaunch interactive Matplotlib plot window? [y/N] > ").strip().lower()
        if gui_choice == "y":
            plt.figure(figsize=(9, 5))
            plt.hist(benign_scores, bins=30, alpha=0.65, color="#10b981", label="Benign Baseline Traffic", density=True)
            plt.hist(attack_scores, bins=30, alpha=0.65, color="#ef4444", label="Attack Traffic (DoS/Scan/BruteForce)", density=True)
            plt.axvline(0.50, color="#f59e0b", linestyle="--", linewidth=2, label="Anomaly Threshold (0.50)")
            
            plt.title("Isolation Forest Threat Score Distribution Separation", fontsize=12, fontweight="bold")
            plt.xlabel("Calibrated Threat Score [0.0 - 1.0]", fontsize=10)
            plt.ylabel("Probability Density", fontsize=10)
            plt.grid(True, linestyle=":", alpha=0.5)
            plt.legend()
            plt.tight_layout()
            print("Opening Matplotlib GUI window... (close window to return to CLI menu)")
            plt.show()


# ============================================================================
# Feature 6: Train / Retrain Model
# ============================================================================
def run_training_pipeline():
    print("\n--- [6] Train / Retrain Isolation Forest Model ---")
    confirm = input("Run model training on synthetic benign dataset (50,000 samples)? [y/N] > ").strip().lower()
    if confirm == "y":
        from training.train import train_model, compute_score_calibration, MODEL_PARAMS
        from training.data_loader import generate_synthetic_benign, generate_synthetic_attacks, train_val_split
        from training.evaluate import full_evaluation_report
        import joblib
        import json

        print("[Train] Generating 50,000 synthetic training samples...")
        df = generate_synthetic_benign(50000)
        train_df, val_df = train_val_split(df, val_ratio=0.20)
        
        print(f"[Train] Fitting StandardScaler and IsolationForest (n=100)...")
        model, scaler = train_model(train_df, MODEL_PARAMS)
        
        print("[Train] Calibrating threat scores...")
        calib = compute_score_calibration(model, scaler, train_df)
        
        attacks = generate_synthetic_attacks(3000)
        eval_report = full_evaluation_report(model, scaler, val_df, attacks, threshold=0.50)
        
        print("\nEvaluation Results:")
        print(f"  False Positive Rate: {eval_report['false_positive_rate']*100:.2f}% (Target < 5%)")
        print(f"  Detection Rate     : {eval_report['detection_rate']*100:.2f}%")
        print(f"  Passes FPR Target  : {eval_report['passes_fpr_target']}")
        print(f"  Precision: DoS={eval_report['label_precision']['DOS']}, BruteForce={eval_report['label_precision']['BRUTE_FORCE']}, PortScan={eval_report['label_precision']['PORT_SCAN']}")
        
        models_dir = Path(__file__).resolve().parent / "models"
        joblib.dump(model, models_dir / "isolation_forest.pkl")
        joblib.dump(scaler, models_dir / "scaler.pkl")
        
        meta = {
            "trained_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "parameters": MODEL_PARAMS,
            "calibration": calib,
            "evaluation": eval_report,
        }
        with open(models_dir / "metadata.json", "w") as fp:
            json.dump(meta, fp, indent=2)
        
        print(f"[Train] Saved artifacts to {models_dir}")
        model_state.load()
        print("[Train] Model reloaded successfully!")


# ============================================================================
# Feature 7: Validation Test Suite
# ============================================================================
def run_validation_suite():
    print("\n--- [7] Run Pytest Validation Suite ---")
    import pytest
    tests_dir = str(Path(__file__).resolve().parent / "tests")
    print(f"Executing pytest in {tests_dir}...")
    ret = pytest.main(["-v", tests_dir])
    if ret == 0:
        print("\n[SUCCESS] All unit and integration tests passed!")
    else:
        print(f"\n[FAILURE] Pytest exited with code {ret}")


# ============================================================================
# Feature 8: Launch FastAPI Microservice
# ============================================================================
def launch_api_service():
    print("\n--- [8] Launch Live FastAPI Microservice (:5000) ---")
    print("Command to run in a separate terminal:")
    print("  python -m uvicorn ml_service.app:app --host 127.0.0.1 --port 5000")
    start_now = input("\nStart uvicorn server in foreground now? (Ctrl+C to stop) [y/N] > ").strip().lower()
    if start_now == "y":
        import uvicorn
        uvicorn.run("ml_service.app:app", host="127.0.0.1", port=5000, log_level="info")


# ============================================================================
# Main Loop
# ============================================================================
def main():
    while True:
        print_banner()
        print("  [1] Model Status & Readiness (Architecture, Features, Calibration)")
        print("  [2] Real-Time Threat Prediction (Preset Scenarios: Benign, DoS, Port Scan, Brute Force)")
        print("  [3] Custom Telemetry Input (Input custom network flow values)")
        print("  [4] Batch Simulation & Threat Timeline (50 flows with ASCII & GUI Window plot)")
        print("  [5] Anomaly Score Distribution Curve (ASCII Histogram & GUI Window plot)")
        print("  [6] Train / Retrain Model (Synthetic dev data)")
        print("  [7] Run Full Model Validation Suite (Pytest)")
        print("  [8] Launch Live FastAPI Microservice (:5000)")
        print("  [0] Exit")
        
        choice = input("\nEnter choice [0-8] > ").strip()
        if choice == "1":
            view_model_status()
        elif choice == "2":
            run_preset_scenarios()
        elif choice == "3":
            run_custom_input()
        elif choice == "4":
            run_batch_simulation()
        elif choice == "5":
            run_score_distribution()
        elif choice == "6":
            run_training_pipeline()
        elif choice == "7":
            run_validation_suite()
        elif choice == "8":
            launch_api_service()
        elif choice == "0":
            print("\nExiting KAIROS ML Bench. Goodbye!\n")
            break
        else:
            print("\nInvalid choice. Please select from 0 to 8.")

        input("\nPress Enter to return to main menu...")


if __name__ == "__main__":
    main()
