"""
Entry point for the KAIROS ML Threat Detection Service.

Run with:
    python main.py
or directly with uvicorn:
    python -m uvicorn ml_service.app:app --host 127.0.0.1 --port 5000
"""

import os

import uvicorn

if __name__ == "__main__":
    host = os.environ.get("ML_HOST", "127.0.0.1")
    port = int(os.environ.get("ML_PORT", "5000"))
    uvicorn.run("ml_service.app:app", host=host, port=port, log_level="info")
