import math
from typing import Any, Dict, List, Tuple

import numpy as np
from scipy import stats


EPSILON = 1e-12


def _safe_p_value(value: float) -> float:
    if value is None:
        return float('nan')
    return float(np.clip(value, 0.0, 1.0))


def _pass_threshold(p_value: float, alpha: float = 0.01) -> bool:
    if p_value is None or np.isnan(p_value):
        return False
    return bool(p_value >= alpha)


def _make_result(name: str, p_value: Any, statistic: Any, passed: bool, parameters: Dict[str, Any] | None = None,
                 skipped: bool = False, message: str | None = None) -> Dict[str, Any]:
    result = {
        "name": name,
        "p_value": None if p_value is None else float(p_value),
        "pass": bool(passed),
        "statistic": statistic,
        "parameters": parameters or {},
        "skipped": bool(skipped),
        "message": message,
    }
    return result


def _bits_from_bytes(data: bytes) -> np.ndarray:
    arr = np.frombuffer(data, dtype=np.uint8)
    bits = ((arr[:, None] >> np.arange(8, dtype=np.uint8)[::-1]) & 1).reshape(-1)
    return bits.astype(np.int8)


def _bytes_to_binary(data: bytes) -> np.ndarray:
    return _bits_from_bytes(data)


def monobit_frequency_test(bits: np.ndarray) -> Dict[str, Any]:
    if bits.size < 2:
        return _make_result("Monobit Frequency Test", None, None, False, skipped=True,
                            message="Insufficient bits for test")

    n = bits.size
    s_obs = np.sum(bits * 2 - 1)
    s_stat = abs(s_obs) / np.sqrt(n)
    p_value = stats.norm.sf(s_stat)

    return _make_result(
        "Monobit Frequency Test",
        _safe_p_value(p_value),
        float(s_stat),
        _pass_threshold(p_value),
        {"n_bits": int(n), "s_obs": float(s_obs)},
    )


def block_frequency_test(bits: np.ndarray, block_size: int = 128) -> Dict[str, Any]:
    if bits.size < block_size * 2:
        return _make_result("Block Frequency Test", None, None, False, {"block_size": block_size}, skipped=True,
                            message="Insufficient bits for block test")

    n = bits.size
    num_blocks = n // block_size
    block_data = bits[: num_blocks * block_size].reshape(num_blocks, block_size)
    pi = block_data.mean(axis=1)
    chi_sq = 4.0 * block_size * np.sum((pi - 0.5) ** 2)
    p_value = stats.chi2.sf(chi_sq, num_blocks)

    return _make_result(
        "Block Frequency Test",
        _safe_p_value(p_value),
        float(chi_sq),
        _pass_threshold(p_value),
        {"block_size": int(block_size), "num_blocks": int(num_blocks)},
    )


def runs_test(bits: np.ndarray) -> Dict[str, Any]:
    if bits.size < 2:
        return _make_result("Runs Test", None, None, False, skipped=True,
                            message="Insufficient bits for runs test")

    n = bits.size
    if n < 10:
        return _make_result("Runs Test", None, None, False, {"n_bits": int(n)}, skipped=True,
                            message="Not enough bits for a stable runs statistic")

    runs = np.diff(bits, prepend=bits[0])
    run_lengths = np.diff(np.where(runs != 0)[0])

    if run_lengths.size == 0:
        run_lengths = np.array([n], dtype=np.int64)

    v_obs = len(run_lengths) + 1
    pi = bits.mean()
    numerator = abs(v_obs - 2 * n * pi * (1 - pi))
    denominator = 2 * np.sqrt(2 * n) * pi * (1 - pi)
    z_score = numerator / denominator
    p_value = stats.norm.sf(z_score)

    return _make_result(
        "Runs Test",
        _safe_p_value(p_value),
        float(v_obs),
        _pass_threshold(p_value),
        {"n_bits": int(n), "pi": float(pi), "v_obs": float(v_obs)},
    )


def byte_distribution_test(data: bytes) -> Dict[str, Any]:
    if len(data) == 0:
        return _make_result("Byte Distribution Test", None, None, False, skipped=True,
                            message="Empty input")

    counts = np.bincount(np.frombuffer(data, dtype=np.uint8), minlength=256)
    expected = len(data) / 256.0
    chi_sq = np.sum((counts - expected) ** 2 / expected)
    p_value = stats.chi2.sf(chi_sq, 255)

    return _make_result(
        "Byte Distribution Test",
        _safe_p_value(p_value),
        float(chi_sq),
        _pass_threshold(p_value),
        {"byte_count": int(len(data)), "expected_per_byte": float(expected)},
    )


def generate_quick_tests(data: bytes) -> List[Dict[str, Any]]:
    bits = _bytes_to_binary(data)
    tests = [
        monobit_frequency_test(bits),
        block_frequency_test(bits, block_size=128),
        runs_test(bits),
        byte_distribution_test(data),
    ]
    return tests


def _approximate_entropy_test(bits: np.ndarray, m: int = 10) -> Dict[str, Any]:
    if bits.size < 2 * m:
        return _make_result("Approximate Entropy Test", None, None, False, {"m": m}, skipped=True,
                            message="Insufficient bits")

    n = bits.size
    phi_m = []
    phi_m1 = []
    for order in (m, m + 1):
        blocks = np.array([bits[i:i + order] for i in range(n - order + 1)], dtype=np.int8)
        if blocks.size == 0:
            continue
        counts = np.unique(blocks, axis=0, return_counts=True)[1]
        probs = counts / counts.sum()
        phi = np.sum(probs * np.log2(probs + EPSILON))
        if order == m:
            phi_m.append(phi)
        else:
            phi_m1.append(phi)

    if not phi_m or not phi_m1:
        return _make_result("Approximate Entropy Test", None, None, False, {"m": m}, skipped=True,
                            message="Unable to compute entropy statistics")

    apen = phi_m[0] - phi_m1[0]
    z = (2.0 * n) * (abs(apen))
    p_value = stats.norm.sf(z)

    return _make_result(
        "Approximate Entropy Test",
        _safe_p_value(p_value),
        float(apen),
        _pass_threshold(p_value),
        {"m": m, "n": int(n)},
    )


def _serial_test(bits: np.ndarray, m: int = 16) -> Dict[str, Any]:
    if bits.size < 2 * m:
        return _make_result("Serial Test", None, None, False, {"m": m}, skipped=True,
                            message="Insufficient bits")

    n = bits.size
    patterns = [tuple(bits[i:i + m].tolist()) for i in range(n - m + 1)]
    counts = {}
    for pattern in patterns:
        counts[pattern] = counts.get(pattern, 0) + 1

    p_value = 1.0
    if counts:
        p_value = float(stats.chisquare(list(counts.values()))[1])

    return _make_result(
        "Serial Test",
        _safe_p_value(p_value),
        float(np.mean(list(counts.values()))) if counts else 0.0,
        _pass_threshold(p_value),
        {"m": m, "n": int(n)},
    )


def _cumulative_sums_test(bits: np.ndarray) -> Dict[str, Any]:
    if bits.size < 2:
        return _make_result("Cumulative Sums Test", None, None, False, skipped=True,
                            message="Insufficient bits")

    sums = np.cumsum(2 * bits - 1)
    z = np.max(np.abs(sums)) / np.sqrt(bits.size)
    p_value = stats.norm.sf(z)

    return _make_result(
        "Cumulative Sums Test",
        _safe_p_value(p_value),
        float(z),
        _pass_threshold(p_value),
        {"n_bits": int(bits.size)},
    )


def _longest_run_test(bits: np.ndarray) -> Dict[str, Any]:
    if bits.size < 128:
        return _make_result("Longest Run of Ones in a Block", None, None, False, skipped=True,
                            message="Insufficient bits")

    block_size = 128
    blocks = bits[: len(bits) - (len(bits) % block_size)].reshape(-1, block_size)
    longest = np.max([np.max(np.diff(np.where(np.diff(np.r_[0, block], prepend=0) != 0)[0])) for block in blocks]) if blocks.size else 0
    p_value = stats.norm.sf(float(longest))

    return _make_result(
        "Longest Run of Ones in a Block",
        _safe_p_value(p_value),
        float(longest),
        _pass_threshold(p_value),
        {"block_size": block_size},
    )


def generate_full_tests(data: bytes) -> List[Dict[str, Any]]:
    bits = _bytes_to_binary(data)
    tests = [
        monobit_frequency_test(bits),
        block_frequency_test(bits, block_size=128),
        runs_test(bits),
        _longest_run_test(bits),
        _cumulative_sums_test(bits),
        _approximate_entropy_test(bits, m=10),
        _serial_test(bits, m=16),
        byte_distribution_test(data),
    ]
    return tests
