#!/usr/bin/env python3
import argparse
import json
import os
import sys
from datetime import datetime, timezone
from typing import Any, Dict, List

from nist_tests import generate_full_tests, generate_quick_tests


DEFAULT_MODE = "quick"
DEFAULT_BYTES = 4096


def _read_stdin_bytes() -> bytes:
    try:
        raw = sys.stdin.buffer.read()
    except Exception:
        raw = b""
    return raw


def _coerce_bytes(raw: bytes, requested: int | None = None) -> bytes:
    if not raw:
        return b""

    if requested is not None and requested > 0:
        data = raw[:requested]
        return data
    return raw


def _build_response(mode: str, data: bytes, test_results: List[Dict[str, Any]], error: str | None = None) -> Dict[str, Any]:
    payload = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "mode": mode,
        "total_bytes": len(data),
        "status": "error" if error else "ok",
        "error": error,
        "test_results": test_results,
    }
    return payload


def run_analysis(data: bytes, mode: str = DEFAULT_MODE) -> Dict[str, Any]:
    if not data:
        return _build_response(mode, data, [], error="No entropy data received on stdin")

    try:
        if mode == "full":
            results = generate_full_tests(data)
        else:
            results = generate_quick_tests(data)

        return _build_response(mode, data, results)
    except Exception as exc:  # pragma: no cover
        return _build_response(mode, data, [], error=str(exc))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="PseudoQuantum Entropy Analyzer")
    parser.add_argument("--mode", choices=["quick", "full"], default=DEFAULT_MODE, help="Select quick or full statistical analysis mode")
    parser.add_argument("--bytes", type=int, default=None, help="Optional maximum number of bytes to process from stdin")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    raw = _read_stdin_bytes()
    data = _coerce_bytes(raw, args.bytes)
    result = run_analysis(data, mode=args.mode)

    try:
        sys.stdout.write(json.dumps(result, indent=2, sort_keys=True))
        sys.stdout.write("\n")
        sys.stdout.flush()
    except BrokenPipeError:
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
