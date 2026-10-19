"""Automated Benchmarking Suite for Context-Aware Hybrid Compressor.

Compares Context-Aware Hybrid Compressor (Heuristic and Oracle modes) against
standard library baselines (gzip, bz2, lzma) across 5 standardized datasets.

Features:
- Rigorous byte-for-byte round-trip integrity verification for every test run.
- Multi-iteration median timing for stable throughput and latency metrics.
- Fine-grained per-chunk telemetry (entropy, features, codec selection, oracle match).
- Exports structured data to CSV and JSON.
- Generates 5 high-resolution publication-quality visualization charts.
"""

import bz2
import csv
import gzip
import json
import lzma
import os
import platform
import statistics
import sys
import tempfile
import time
from typing import Any, Optional

import matplotlib
matplotlib.use("Agg")  # Non-interactive backend
import matplotlib.pyplot as plt
import numpy as np

from core import (
    DEFAULT_CHUNK_SIZE,
    compress_file,
    decompress_file,
    inspect_container,
)
from core.codecs import CODEC_NAMES
from core.selector import ChunkSelectionResult


def run_single_method_baseline(
    name: str,
    data: bytes,
    compress_fn: Any,
    decompress_fn: Any,
    repeats: int = 3,
) -> dict[str, Any]:
    """Measure compression and decompression performance for an in-memory baseline."""
    orig_size = len(data)

    # Measure compression times
    comp_times = []
    compressed_bytes = b""
    for _ in range(repeats):
        t0 = time.perf_counter()
        compressed_bytes = compress_fn(data)
        comp_times.append(time.perf_counter() - t0)

    median_comp_time = statistics.median(comp_times)
    comp_size = len(compressed_bytes)

    # Measure decompression times
    dec_times = []
    decompressed_bytes = b""
    for _ in range(repeats):
        t0 = time.perf_counter()
        decompressed_bytes = decompress_fn(compressed_bytes)
        dec_times.append(time.perf_counter() - t0)

    median_dec_time = statistics.median(dec_times)

    # Exact byte-for-byte verification
    verified = (decompressed_bytes == data)
    if not verified:
        raise ValueError(f"Baseline {name} failed round-trip verification!")

    ratio = comp_size / orig_size if orig_size > 0 else 1.0
    space_saved = (1.0 - ratio) * 100.0
    comp_throughput = (orig_size / (1024 * 1024)) / median_comp_time if median_comp_time > 0 else 0.0
    dec_throughput = (orig_size / (1024 * 1024)) / median_dec_time if median_dec_time > 0 else 0.0

    return {
        "method": name,
        "original_size": orig_size,
        "compressed_size": comp_size,
        "storage_ratio": round(ratio, 4),
        "space_saved_percent": round(space_saved, 2),
        "compression_time_sec": round(median_comp_time, 5),
        "decompression_time_sec": round(median_dec_time, 5),
        "compression_throughput_mibs": round(comp_throughput, 2),
        "decompression_throughput_mibs": round(dec_throughput, 2),
        "roundtrip_verified": verified,
    }


def run_custom_compressor_benchmark(
    name: str,
    input_path: str,
    chunk_size: int,
    mode: str,
    repeats: int = 3,
) -> tuple[dict[str, Any], list[ChunkSelectionResult]]:
    """Measure performance and chunk telemetry for custom CACZ compressor."""
    with open(input_path, "rb") as f:
        original_data = f.read()
    orig_size = len(original_data)

    temp_dir = tempfile.gettempdir()
    comp_path = os.path.join(temp_dir, f"bench_{name}_{os.path.basename(input_path)}.cacz")
    dec_path = os.path.join(temp_dir, f"bench_{name}_{os.path.basename(input_path)}.orig")

    comp_times = []
    selection_results: list[ChunkSelectionResult] = []

    # Run compression iterations
    for it in range(repeats):
        t0 = time.perf_counter()
        meta, results = compress_file(
            input_path=input_path,
            output_path=comp_path,
            chunk_size=chunk_size,
            mode=mode,
            evaluate_oracle=(mode == "heuristic"),
        )
        comp_times.append(time.perf_counter() - t0)
        if it == 0:
            selection_results = results

    median_comp_time = statistics.median(comp_times)
    comp_size = os.path.getsize(comp_path)

    # Run decompression iterations
    dec_times = []
    for _ in range(repeats):
        t0 = time.perf_counter()
        decompress_file(comp_path, dec_path)
        dec_times.append(time.perf_counter() - t0)

    median_dec_time = statistics.median(dec_times)

    with open(dec_path, "rb") as f:
        reconstructed = f.read()

    verified = (reconstructed == original_data)
    if not verified:
        raise ValueError(f"Custom compressor {name} failed round-trip verification!")

    # Cleanup temp files
    for p in [comp_path, dec_path]:
        if os.path.exists(p):
            os.remove(p)

    ratio = comp_size / orig_size if orig_size > 0 else 1.0
    space_saved = (1.0 - ratio) * 100.0
    comp_throughput = (orig_size / (1024 * 1024)) / median_comp_time if median_comp_time > 0 else 0.0
    dec_throughput = (orig_size / (1024 * 1024)) / median_dec_time if median_dec_time > 0 else 0.0

    # Codec distribution counts
    codec_counts = {cname: 0 for cname in CODEC_NAMES.values()}
    for r in selection_results:
        codec_counts[r.selected_name] += 1

    matched_oracle_count = sum(1 for r in selection_results if r.matched_oracle is True)
    oracle_match_percent = (matched_oracle_count / len(selection_results) * 100.0) if selection_results else 100.0

    res_dict = {
        "method": name,
        "original_size": orig_size,
        "compressed_size": comp_size,
        "storage_ratio": round(ratio, 4),
        "space_saved_percent": round(space_saved, 2),
        "compression_time_sec": round(median_comp_time, 5),
        "decompression_time_sec": round(median_dec_time, 5),
        "compression_throughput_mibs": round(comp_throughput, 2),
        "decompression_throughput_mibs": round(dec_throughput, 2),
        "roundtrip_verified": verified,
        "chunk_count": len(selection_results),
        "codec_distribution": codec_counts,
        "oracle_match_percent": round(oracle_match_percent, 2),
    }

    return res_dict, selection_results


def generate_benchmark_charts(
    all_results: list[dict[str, Any]],
    chunk_telemetry: dict[str, list[ChunkSelectionResult]],
    output_dir: str,
) -> list[str]:
    """Generate the 5 required benchmark analysis charts."""
    charts_dir = os.path.join(output_dir, "charts")
    os.makedirs(charts_dir, exist_ok=True)
    generated_chart_paths = []

    plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
    plt.rcParams.update({"font.size": 10, "figure.autolayout": True})

    files = list(dict.fromkeys(r["file"] for r in all_results))
    methods = ["Custom-Heuristic", "Custom-Oracle", "gzip", "bz2", "lzma"]

    # -------------------------------------------------------------
    # Chart A: Compression Ratio Comparison (Grouped Bar Chart)
    # -------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    x = np.arange(len(files))
    width = 0.15

    colors = ["#2b5c8f", "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728"]
    for i, method in enumerate(methods):
        ratios = []
        for f in files:
            matching = [r for r in all_results if r["file"] == f and r["method"] == method]
            ratios.append(matching[0]["storage_ratio"] if matching else 0.0)
        ax.bar(x + (i - 2) * width, ratios, width, label=method, color=colors[i])

    ax.set_ylabel("Storage Ratio (Compressed / Original, lower is better)", fontweight="bold")
    ax.set_title("Chart A: Compression Ratio Comparison Across Datasets", fontsize=12, fontweight="bold")
    ax.set_xticks(x)
    ax.set_xticklabels([f.replace(".py", "\n.py").replace(".csv", "\n.csv") for f in files], rotation=0)
    ax.axhline(1.0, color="gray", linestyle="--", alpha=0.7, label="Uncompressed Baseline (1.0)")
    ax.legend(loc="upper right", frameon=True)
    ax.set_ylim(bottom=0)

    chart_a_path = os.path.join(charts_dir, "compression_ratios.png")
    fig.savefig(chart_a_path)
    plt.close(fig)
    generated_chart_paths.append(chart_a_path)

    # -------------------------------------------------------------
    # Chart B: Codec Selection Distribution (Stacked Bar Chart)
    # -------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    codec_names_list = ["raw", "rle", "delta", "huffman", "lz77"]
    codec_colors = {
        "raw": "#7f7f7f",
        "rle": "#1f77b4",
        "delta": "#9467bd",
        "huffman": "#2ca02c",
        "lz77": "#ff7f0e",
    }

    bottom = np.zeros(len(files))
    for cname in codec_names_list:
        counts = []
        for f in files:
            matching = [r for r in all_results if r["file"] == f and r["method"] == "Custom-Heuristic"]
            dist = matching[0].get("codec_distribution", {}) if matching else {}
            counts.append(dist.get(cname, 0))
        counts_arr = np.array(counts)
        ax.bar(files, counts_arr, bottom=bottom, label=cname.upper(), color=codec_colors[cname], width=0.55)
        bottom += counts_arr

    ax.set_ylabel("Selected Chunk Count", fontweight="bold")
    ax.set_title("Chart B: Heuristic Codec Selection Distribution by Dataset", fontsize=12, fontweight="bold")
    ax.set_xticks(range(len(files)))
    ax.set_xticklabels(files, rotation=15, ha="right")
    ax.legend(title="Codec", loc="upper right", frameon=True)

    chart_b_path = os.path.join(charts_dir, "codec_distribution.png")
    fig.savefig(chart_b_path)
    plt.close(fig)
    generated_chart_paths.append(chart_b_path)

    # -------------------------------------------------------------
    # Chart C: Entropy vs Selected Codec (Scatter Plot)
    # -------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    codec_y_map = {"raw": 0, "rle": 1, "delta": 2, "huffman": 3, "lz77": 4}
    jitter_rng = np.random.default_rng(42)

    for fname, chunks in chunk_telemetry.items():
        entropies = [c.features.entropy for c in chunks]
        y_vals = [codec_y_map.get(c.selected_name, 0) + jitter_rng.uniform(-0.15, 0.15) for c in chunks]
        c_colors = [codec_colors.get(c.selected_name, "#000000") for c in chunks]
        ax.scatter(entropies, y_vals, label=fname, alpha=0.75, edgecolors="none", s=50)

    ax.set_yticks(list(codec_y_map.values()))
    ax.set_yticklabels([k.upper() for k in codec_y_map.keys()], fontweight="bold")
    ax.set_xlabel("Chunk Shannon Entropy (bits / byte)", fontweight="bold")
    ax.set_title("Chart C: Chunk Entropy vs Selected Compression Codec", fontsize=12, fontweight="bold")
    ax.set_xlim(-0.2, 8.2)
    ax.legend(title="Dataset", loc="center left", bbox_to_anchor=(1.02, 0.5), frameon=True)

    chart_c_path = os.path.join(charts_dir, "entropy_vs_codec.png")
    fig.savefig(chart_c_path, bbox_inches="tight")
    plt.close(fig)
    generated_chart_paths.append(chart_c_path)

    # -------------------------------------------------------------
    # Chart D: Performance Comparison (Throughput MiB/s)
    # -------------------------------------------------------------
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5), dpi=300)

    # Compute average throughput per method across all files
    comp_throughputs = []
    dec_throughputs = []
    for m in methods:
        comp_vals = [r["compression_throughput_mibs"] for r in all_results if r["method"] == m]
        dec_vals = [r["decompression_throughput_mibs"] for r in all_results if r["method"] == m]
        comp_throughputs.append(statistics.mean(comp_vals) if comp_vals else 0.0)
        dec_throughputs.append(statistics.mean(dec_vals) if dec_vals else 0.0)

    y_pos = np.arange(len(methods))
    ax1.barh(y_pos, comp_throughputs, color="#1f77b4", align="center")
    ax1.set_yticks(y_pos)
    ax1.set_yticklabels(methods, fontweight="bold")
    ax1.invert_yaxis()
    ax1.set_xlabel("Compression Speed (MiB/s, higher is better)", fontweight="bold")
    ax1.set_title("Compression Throughput", fontweight="bold")

    ax2.barh(y_pos, dec_throughputs, color="#2ca02c", align="center")
    ax2.set_yticks(y_pos)
    ax2.set_yticklabels([])
    ax2.invert_yaxis()
    ax2.set_xlabel("Decompression Speed (MiB/s, higher is better)", fontweight="bold")
    ax2.set_title("Decompression Throughput", fontweight="bold")

    fig.suptitle("Chart D: Throughput Performance Comparison (Mean across Corpus)", fontsize=13, fontweight="bold")

    chart_d_path = os.path.join(charts_dir, "performance_comparison.png")
    fig.savefig(chart_d_path)
    plt.close(fig)
    generated_chart_paths.append(chart_d_path)

    # -------------------------------------------------------------
    # Chart E: Heuristic vs Oracle Selector Comparison
    # -------------------------------------------------------------
    fig, (ax_e1, ax_e2) = plt.subplots(1, 2, figsize=(12, 5), dpi=300)

    # Subplot E1: Compressed size comparison
    h_sizes = []
    o_sizes = []
    match_percentages = []
    for f in files:
        h_res = [r for r in all_results if r["file"] == f and r["method"] == "Custom-Heuristic"][0]
        o_res = [r for r in all_results if r["file"] == f and r["method"] == "Custom-Oracle"][0]
        h_sizes.append(h_res["compressed_size"] / 1024.0)
        o_sizes.append(o_res["compressed_size"] / 1024.0)
        match_percentages.append(h_res["oracle_match_percent"])

    x_idx = np.arange(len(files))
    w = 0.35
    ax_e1.bar(x_idx - w / 2, h_sizes, w, label="Heuristic Mode", color="#2b5c8f")
    ax_e1.bar(x_idx + w / 2, o_sizes, w, label="Oracle Mode", color="#1f77b4")
    ax_e1.set_ylabel("Compressed Size (KiB, lower is better)", fontweight="bold")
    ax_e1.set_xticks(x_idx)
    ax_e1.set_xticklabels([f.replace(".py", "\n.py").replace(".csv", "\n.csv") for f in files], rotation=0)
    ax_e1.set_title("Compressed Container Size", fontweight="bold")
    ax_e1.legend()

    # Subplot E2: Selector Prediction Accuracy
    ax_e2.bar(files, match_percentages, color="#2ca02c", width=0.55)
    ax_e2.set_ylabel("Oracle Match Rate (%)", fontweight="bold")
    ax_e2.set_ylim(0, 105)
    ax_e2.set_xticks(range(len(files)))
    ax_e2.set_xticklabels(files, rotation=15, ha="right")
    ax_e2.set_title("Heuristic Codec Prediction Accuracy", fontweight="bold")
    for idx, v in enumerate(match_percentages):
        ax_e2.text(idx, v + 2, f"{v:.1f}%", ha="center", fontweight="bold", fontsize=9)

    fig.suptitle("Chart E: Selector Quality — Heuristic vs Oracle Analysis", fontsize=13, fontweight="bold")

    chart_e_path = os.path.join(charts_dir, "heuristic_vs_oracle.png")
    fig.savefig(chart_e_path)
    plt.close(fig)
    generated_chart_paths.append(chart_e_path)

    return generated_chart_paths


def run_benchmarks(
    test_dir: str = "benchmark/test_files",
    chunk_size: int = DEFAULT_CHUNK_SIZE,
    output_dir: str = "benchmark/results",
) -> int:
    """Execute complete benchmark suite, save CSV/JSON, and generate charts."""
    if not os.path.exists(test_dir):
        print(f"Test directory '{test_dir}' not found. Generating datasets...")
        from benchmark.generate_test_files import generate_all_datasets
        generate_all_datasets(test_dir)

    test_files = sorted(
        [os.path.join(test_dir, f) for f in os.listdir(test_dir) if os.path.isfile(os.path.join(test_dir, f))]
    )

    if not test_files:
        print(f"Error: No benchmark files found in {test_dir}", file=sys.stderr)
        return 1

    os.makedirs(output_dir, exist_ok=True)
    all_benchmark_rows: list[dict[str, Any]] = []
    chunk_telemetry: dict[str, list[ChunkSelectionResult]] = {}

    print(f"\n=================================================================")
    print(f" Context-Aware Hybrid Compressor — Automated Benchmarking Suite")
    print(f" Python: {platform.python_version()} | OS: {platform.system()} {platform.release()} ({platform.machine()})")
    print(f" Chunk Size: {chunk_size} B | Test Files: {len(test_files)}")
    print(f"=================================================================\n")

    for file_path in test_files:
        filename = os.path.basename(file_path)
        with open(file_path, "rb") as f:
            data = f.read()

        print(f"--> Benchmarking: {filename} ({len(data):,} bytes)")

        # 1. Custom Compressor — Heuristic Mode
        h_res, h_chunks = run_custom_compressor_benchmark(
            name="Custom-Heuristic",
            input_path=file_path,
            chunk_size=chunk_size,
            mode="heuristic",
            repeats=3,
        )
        h_res["file"] = filename
        all_benchmark_rows.append(h_res)
        chunk_telemetry[filename] = h_chunks
        print(f"    Custom-Heuristic : {h_res['compressed_size']:>8,} B | Ratio: {h_res['storage_ratio']:.4f} | "
              f"Comp: {h_res['compression_throughput_mibs']:6.2f} MiB/s | Dec: {h_res['decompression_throughput_mibs']:6.2f} MiB/s")

        # 2. Custom Compressor — Oracle Mode
        o_res, _ = run_custom_compressor_benchmark(
            name="Custom-Oracle",
            input_path=file_path,
            chunk_size=chunk_size,
            mode="oracle",
            repeats=1,  # Oracle does exhaustive search, 1 repetition is sufficient
        )
        o_res["file"] = filename
        all_benchmark_rows.append(o_res)
        print(f"    Custom-Oracle    : {o_res['compressed_size']:>8,} B | Ratio: {o_res['storage_ratio']:.4f} | "
              f"Comp: {o_res['compression_throughput_mibs']:6.2f} MiB/s | Dec: {o_res['decompression_throughput_mibs']:6.2f} MiB/s")

        # 3. Baseline gzip
        gz_res = run_single_method_baseline("gzip", data, gzip.compress, gzip.decompress, repeats=3)
        gz_res["file"] = filename
        all_benchmark_rows.append(gz_res)
        print(f"    gzip (standard)  : {gz_res['compressed_size']:>8,} B | Ratio: {gz_res['storage_ratio']:.4f} | "
              f"Comp: {gz_res['compression_throughput_mibs']:6.2f} MiB/s | Dec: {gz_res['decompression_throughput_mibs']:6.2f} MiB/s")

        # 4. Baseline bz2
        bz_res = run_single_method_baseline("bz2", data, bz2.compress, bz2.decompress, repeats=3)
        bz_res["file"] = filename
        all_benchmark_rows.append(bz_res)
        print(f"    bz2 (standard)   : {bz_res['compressed_size']:>8,} B | Ratio: {bz_res['storage_ratio']:.4f} | "
              f"Comp: {bz_res['compression_throughput_mibs']:6.2f} MiB/s | Dec: {bz_res['decompression_throughput_mibs']:6.2f} MiB/s")

        # 5. Baseline lzma
        lz_res = run_single_method_baseline("lzma", data, lzma.compress, lzma.decompress, repeats=3)
        lz_res["file"] = filename
        all_benchmark_rows.append(lz_res)
        print(f"    lzma (standard)  : {lz_res['compressed_size']:>8,} B | Ratio: {lz_res['storage_ratio']:.4f} | "
              f"Comp: {lz_res['compression_throughput_mibs']:6.2f} MiB/s | Dec: {lz_res['decompression_throughput_mibs']:6.2f} MiB/s")
        print()

    # Save CSV
    csv_path = os.path.join(output_dir, "benchmark_results.csv")
    csv_columns = [
        "file", "method", "original_size", "compressed_size", "storage_ratio",
        "space_saved_percent", "compression_time_sec", "decompression_time_sec",
        "compression_throughput_mibs", "decompression_throughput_mibs",
        "roundtrip_verified", "oracle_match_percent"
    ]
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=csv_columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(all_benchmark_rows)
    print(f"==> Benchmark results CSV saved to: {csv_path}")

    # Save JSON summary
    json_path = os.path.join(output_dir, "benchmark_summary.json")
    summary_data = {
        "metadata": {
            "python_version": platform.python_version(),
            "os": platform.system(),
            "os_release": platform.release(),
            "architecture": platform.machine(),
            "chunk_size": chunk_size,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        },
        "results": all_benchmark_rows,
    }
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2)
    print(f"==> Benchmark summary JSON saved to: {json_path}")

    # Generate charts
    print("==> Generating Matplotlib benchmark analysis charts...")
    chart_paths = generate_benchmark_charts(all_benchmark_rows, chunk_telemetry, output_dir)
    for cp in chart_paths:
        print(f"    Chart saved: {cp}")

    print("\n==> Benchmark execution and visualization completed successfully!\n")
    return 0


if __name__ == "__main__":
    t_dir = sys.argv[1] if len(sys.argv) > 1 else "benchmark/test_files"
    o_dir = sys.argv[2] if len(sys.argv) > 2 else "benchmark/results"
    sys.exit(run_benchmarks(test_dir=t_dir, output_dir=o_dir))
