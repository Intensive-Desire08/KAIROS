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
    s_obs = float(np.sum(bits * 2 - 1))
    s_stat = abs(s_obs) / math.sqrt(n)
    # NIST SP 800-22 Section 2.1.4: P-value = erfc( |S_obs| / sqrt(2*n) )
    p_value = math.erfc(abs(s_obs) / math.sqrt(2.0 * n))

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

    pi = float(bits.mean())
    # NIST SP 800-22 Section 2.3: Pre-test check |pi - 0.5| >= 2 / sqrt(n)
    tau = 2.0 / math.sqrt(n)
    if abs(pi - 0.5) >= tau:
        return _make_result("Runs Test", 0.0, 0.0, False,
                            {"n_bits": int(n), "pi": pi, "v_obs": 0.0},
                            message="Frequency test failed prerequisite for runs test")

    # Vectorized run count: 1 + transitions between adjacent bits
    v_obs = float(1 + np.sum(bits[1:] != bits[:-1]))
    numerator = abs(v_obs - 2.0 * n * pi * (1.0 - pi))
    denominator = 2.0 * math.sqrt(2.0 * n) * pi * (1.0 - pi)

    if denominator <= 0:
        return _make_result("Runs Test", 0.0, float(v_obs), False,
                            {"n_bits": int(n), "pi": pi, "v_obs": v_obs})

    # NIST SP 800-22 Section 2.3.4: P-value = erfc( numerator / denominator )
    p_value = math.erfc(numerator / denominator)

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
        longest_run_of_ones_test(bits),
        byte_distribution_test(data),
    ]
    return tests


def _phi_m(bits: np.ndarray, m: int) -> float:
    n = bits.size
    extended = np.concatenate((bits, bits[: m - 1]))
    powers = 1 << np.arange(m - 1, -1, -1, dtype=np.int64)
    patterns = np.convolve(extended.astype(np.int64), powers, mode="valid")
    counts = np.bincount(patterns, minlength=1 << m)
    counts = counts[counts > 0]
    probs = counts / float(n)
    return float(np.sum(probs * np.log(probs)))


def _approximate_entropy_test(bits: np.ndarray, m: int = 5) -> Dict[str, Any]:
    n = bits.size
    # Bound m to prevent invalid degrees of freedom: m <= floor(log2(n)) - 2
    max_m = max(2, int(math.floor(math.log2(max(n, 8)))) - 3)
    effective_m = min(m, max_m)

    if n < 2 ** (effective_m + 1):
        return _make_result("Approximate Entropy Test", None, None, False, {"m": effective_m}, skipped=True,
                            message="Insufficient bits for approximate entropy")

    phi_m = _phi_m(bits, effective_m)
    phi_m1 = _phi_m(bits, effective_m + 1)
    apen = phi_m - phi_m1

    chi_sq = float(2.0 * n * (math.log(2.0) - apen))
    p_value = float(stats.chi2.sf(chi_sq, 1 << (effective_m - 1)))

    return _make_result(
        "Approximate Entropy Test",
        _safe_p_value(p_value),
        float(chi_sq),
        _pass_threshold(p_value),
        {"m": effective_m, "n": int(n), "apen": float(apen)},
    )


def _serial_test(bits: np.ndarray, m: int = 4) -> Dict[str, Any]:
    n = bits.size
    max_m = max(2, int(math.floor(math.log2(max(n, 8)))) - 3)
    effective_m = min(m, max_m)

    if n < 2 ** (effective_m + 1):
        return _make_result("Serial Test", None, None, False, {"m": effective_m}, skipped=True,
                            message="Insufficient bits for serial test")

    extended = np.concatenate((bits, bits[: effective_m - 1]))
    powers = 1 << np.arange(effective_m - 1, -1, -1, dtype=np.int64)
    patterns = np.convolve(extended.astype(np.int64), powers, mode="valid")
    counts = np.bincount(patterns, minlength=1 << effective_m)

    expected = float(n) / float(1 << effective_m)
    chi_sq = float(np.sum((counts - expected) ** 2 / expected))
    p_value = float(stats.chi2.sf(chi_sq, (1 << effective_m) - 1))

    return _make_result(
        "Serial Test",
        _safe_p_value(p_value),
        float(chi_sq),
        _pass_threshold(p_value),
        {"m": effective_m, "n": int(n)},
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


def _max_run_of_ones(arr: np.ndarray) -> int:
    max_run = 0
    cur_run = 0
    for b in arr:
        if b == 1:
            cur_run += 1
            if cur_run > max_run:
                max_run = cur_run
        else:
            cur_run = 0
    return max_run


def longest_run_of_ones_test(bits: np.ndarray) -> Dict[str, Any]:
    n = bits.size
    if n < 128:
        return _make_result("Longest Run of Ones in a Block", None, None, False, skipped=True,
                            message="Insufficient bits (need >= 128)")

    highest_1s_run = _max_run_of_ones(bits)

    if n < 6272:
        # NIST SP 800-22 Section 2.4: M = 8, K = 3 (classes: <=1, 2, 3, >=4)
        M = 8
        K = 3
        pi = np.array([0.2148, 0.3672, 0.2305, 0.1875])
        N = n // M
        blocks = bits[: N * M].reshape(N, M)
        v = np.zeros(4, dtype=np.int64)
        for block in blocks:
            max_b = _max_run_of_ones(block)
            if max_b <= 1:
                v[0] += 1
            elif max_b == 2:
                v[1] += 1
            elif max_b == 3:
                v[2] += 1
            else:
                v[3] += 1
    else:
        # NIST SP 800-22 Section 2.4: M = 128, K = 5 (classes: <=4, 5, 6, 7, 8, >=9)
        M = 128
        K = 5
        pi = np.array([0.1174, 0.2430, 0.2493, 0.1752, 0.1027, 0.1124])
        N = n // M
        blocks = bits[: N * M].reshape(N, M)
        v = np.zeros(6, dtype=np.int64)
        for block in blocks:
            max_b = _max_run_of_ones(block)
            if max_b <= 4:
                v[0] += 1
            elif max_b == 5:
                v[1] += 1
            elif max_b == 6:
                v[2] += 1
            elif max_b == 7:
                v[3] += 1
            elif max_b == 8:
                v[4] += 1
            else:
                v[5] += 1

    expected = N * pi
    chi_sq = float(np.sum(((v - expected) ** 2) / expected))
    p_value = float(stats.chi2.sf(chi_sq, K))

    return _make_result(
        "Longest Run of Ones in a Block",
        _safe_p_value(p_value),
        float(highest_1s_run),
        _pass_threshold(p_value),
        {
            "block_size": int(M),
            "num_blocks": int(N),
            "highest_1s_run": int(highest_1s_run),
            "chi_sq": round(chi_sq, 4),
        },
    )


def _longest_run_test(bits: np.ndarray) -> Dict[str, Any]:
    return longest_run_of_ones_test(bits)


def generate_full_tests(data: bytes) -> List[Dict[str, Any]]:
    bits = _bytes_to_binary(data)
    tests = [
        monobit_frequency_test(bits),
        block_frequency_test(bits, block_size=128),
        runs_test(bits),
        longest_run_of_ones_test(bits),
        _cumulative_sums_test(bits),
        _approximate_entropy_test(bits, m=5),
        _serial_test(bits, m=4),
        byte_distribution_test(data),
    ]
    return tests


def run_exhaustive_tests(samples: List[bytes], alpha: float = 0.01) -> Dict[str, Any]:
    """
    NIST SP 800-22 Section 4.2 Multi-Run Methodology:
    1. Runs the 5 core tests across N independent samples.
    2. Section 4.2.1: Checks proportion of passes against the 3-sigma confidence interval.
       Threshold = (1 - alpha) - 3 * sqrt( alpha * (1 - alpha) / N ).
    3. Section 4.2.2: Evaluates P-value distribution uniformity across 10 bins using
       Chi-Square Goodness-of-Fit and Kolmogorov-Smirnov tests.
    """
    n_runs = len(samples)
    if n_runs < 1:
        return {"status": "error", "error": "No samples provided for exhaustive analysis"}

    test_keys = ["monobit", "block_frequency", "runs", "longest_run", "byte_distribution"]
    test_names = [
        "Monobit Frequency Test",
        "Block Frequency Test",
        "Runs Test",
        "Longest Run of Ones in a Block",
        "Byte Distribution Test",
    ]

    collected_p_values: Dict[str, List[float]] = {k: [] for k in test_keys}
    sample_sizes = [len(s) for s in samples]

    for sample in samples:
        bits = _bytes_to_binary(sample)
        res = [
            monobit_frequency_test(bits),
            block_frequency_test(bits, block_size=128),
            runs_test(bits),
            longest_run_of_ones_test(bits),
            byte_distribution_test(sample),
        ]
        for key, r in zip(test_keys, res):
            p = r.get("p_value")
            collected_p_values[key].append(0.0 if p is None or np.isnan(p) else float(p))

    # NIST SP 800-22 Section 4.2.1: Confidence Interval for the Proportion of Sequences
    # Acceptable proportion threshold: (1 - alpha) - 3 * sqrt( alpha * (1 - alpha) / N )
    delta = 3.0 * math.sqrt((alpha * (1.0 - alpha)) / float(n_runs))
    min_pass_proportion = max(0.0, (1.0 - alpha) - delta)
    min_passes_required = int(math.ceil(min_pass_proportion * n_runs))

    test_summaries = {}
    all_p_values: List[float] = []
    all_tests_passed = True

    for key, name in zip(test_keys, test_names):
        p_vals = collected_p_values[key]
        all_p_values.extend(p_vals)

        # Step 2: Pass proportion
        passes = sum(1 for p in p_vals if p >= alpha)
        pass_rate = passes / float(n_runs)
        proportion_passed = bool(pass_rate >= min_pass_proportion)

        # Step 3: Uniformity test across 10 bins of width 0.1
        hist_counts, _ = np.histogram(p_vals, bins=10, range=(0.0, 1.0))
        hist_counts_list = hist_counts.tolist()
        expected_per_bin = float(n_runs) / 10.0

        chi_sq_uniformity = float(sum((c - expected_per_bin) ** 2 / expected_per_bin for c in hist_counts_list))
        # Degrees of freedom = 9
        uniformity_p_val = float(stats.chi2.sf(chi_sq_uniformity, 9))
        # NIST SP 800-22 Section 4.2.2: p_T >= 0.0001
        uniformity_passed = bool(uniformity_p_val >= 0.0001)

        # Kolmogorov-Smirnov test against uniform distribution
        ks_res = stats.kstest(p_vals, "uniform")
        ks_stat = float(ks_res.statistic)
        ks_p_val = float(ks_res.pvalue)

        # Qualitative diagnosis
        if uniformity_passed:
            diagnosis = "Uniform (Consistent with ideal random source)"
        elif hist_counts_list[0] > 2.0 * expected_per_bin:
            diagnosis = "Skewed toward 0 (Excessive failures / non-random bias)"
        elif hist_counts_list[-1] > 2.0 * expected_per_bin:
            diagnosis = "Skewed toward 1 (Suspiciously perfect / degenerate source or test)"
        else:
            diagnosis = "Clustered (Localized non-uniform structure)"

        test_passed = bool(proportion_passed and uniformity_passed)
        if not test_passed:
            all_tests_passed = False

        test_summaries[key] = {
            "name": name,
            "runs": n_runs,
            "passes": passes,
            "pass_rate": round(pass_rate, 4),
            "pass_rate_percent": round(pass_rate * 100.0, 2),
            "min_pass_threshold_percent": round(min_pass_proportion * 100.0, 2),
            "min_passes_required": min_passes_required,
            "proportion_passed": proportion_passed,
            "histogram_bins_10": hist_counts_list,
            "expected_per_bin": round(expected_per_bin, 2),
            "uniformity_chi_sq": round(chi_sq_uniformity, 4),
            "uniformity_p_value": _safe_p_value(uniformity_p_val),
            "uniformity_passed": uniformity_passed,
            "ks_statistic": round(ks_stat, 4),
            "ks_p_value": _safe_p_value(ks_p_val),
            "diagnosis": diagnosis,
            "passed": test_passed,
            "sample_p_values": [round(p, 4) for p in p_vals[:20]],
        }

    total_evaluations = n_runs * len(test_keys)
    total_passes = sum(t["passes"] for t in test_summaries.values())
    overall_pass_rate = total_passes / float(total_evaluations) if total_evaluations else 0.0

    overall_hist, _ = np.histogram(all_p_values, bins=10, range=(0.0, 1.0))
    overall_hist_list = overall_hist.tolist()
    overall_expected = float(total_evaluations) / 10.0
    overall_chi_sq = float(sum((c - overall_expected) ** 2 / overall_expected for c in overall_hist_list))
    overall_uniformity_p = float(stats.chi2.sf(overall_chi_sq, 9))

    return {
        "status": "ok",
        "mode": "exhaustive",
        "runs": n_runs,
        "sample_bytes": sample_sizes[0] if sample_sizes else 4096,
        "total_bytes_tested": sum(sample_sizes),
        "alpha": alpha,
        "min_pass_threshold_percent": round(min_pass_proportion * 100.0, 2),
        "min_passes_required": min_passes_required,
        "overall_verdict": "PASSED" if all_tests_passed else "FAILURES_DETECTED",
        "overall_passed": all_tests_passed,
        "overall_pass_rate_percent": round(overall_pass_rate * 100.0, 2),
        "overall_histogram_bins_10": overall_hist_list,
        "overall_expected_per_bin": round(overall_expected, 2),
        "overall_uniformity_p_value": _safe_p_value(overall_uniformity_p),
        "tests": test_summaries,
    }
