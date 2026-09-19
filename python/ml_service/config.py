"""
Configuration loader for the KAIROS ML Threat Detection Service.
See Technical Specification — Module 3, Section 8.

The ML Service reads the SAME shared config.json used by the Gateway
(nodejs/config/config.json). A local fallback copy may live at
python/config/config.json for standalone development/testing.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from threading import Lock
from typing import Any, Dict

from .constants import DEFAULT_ATTACK_LABEL_THRESHOLDS, DEFAULT_THRESHOLDS

# Resolve config path: env var > shared nodejs config > local fallback
_DEFAULT_SEARCH_PATHS = [
    os.environ.get("CONFIG_PATH", ""),
    str(Path(__file__).resolve().parents[2] / "nodejs" / "config" / "config.json"),
    str(Path(__file__).resolve().parents[1] / "config" / "config.json"),
]


class ConfigLoader:
    """Loads and caches configuration from config.json (Section 8)."""

    _instance = None
    _lock = Lock()

    def __new__(cls):
        with cls._lock:
            if cls._instance is None:
                cls._instance = super().__new__(cls)
                cls._instance._config = None
                cls._instance._path = None
        return cls._instance

    def load(self, path: str | None = None) -> Dict[str, Any]:
        """Load config.json from the first path that exists. Cached after first load."""
        if self._config is not None and path is None:
            return self._config

        search_paths = [path] if path else [p for p in _DEFAULT_SEARCH_PATHS if p]
        for candidate in search_paths:
            candidate_path = Path(candidate)
            if candidate_path.is_file():
                with open(candidate_path, "r", encoding="utf-8") as f:
                    self._config = json.load(f)
                self._path = str(candidate_path)
                return self._config

        # Fall back to safe defaults so the service can still start (Section 10.3)
        self._config = self._defaults()
        self._path = None
        return self._config

    def reload(self) -> Dict[str, Any]:
        self._config = None
        return self.load(self._path)

    @staticmethod
    def _defaults() -> Dict[str, Any]:
        return {
            "ml": {
                "url": "http://127.0.0.1:5000",
                "timeout_ms": 1000,
                "fallback_score": 0.02,
                "degradation_threshold": 3,
            },
            "thresholds": dict(DEFAULT_THRESHOLDS),
            "attack_labels": dict(DEFAULT_ATTACK_LABEL_THRESHOLDS),
            "confidence": {"enabled": False, "min_confidence_for_block": 0.20},
        }

    @property
    def path(self) -> str | None:
        return self._path


_loader = ConfigLoader()


def get_config() -> Dict[str, Any]:
    return _loader.load()


def get_thresholds() -> Dict[str, float]:
    return get_config().get("thresholds", dict(DEFAULT_THRESHOLDS))


def get_attack_label_config() -> Dict[str, float]:
    return get_config().get("attack_labels", dict(DEFAULT_ATTACK_LABEL_THRESHOLDS))


def get_confidence_config() -> Dict[str, Any]:
    return get_config().get("confidence", {"enabled": False, "min_confidence_for_block": 0.20})
