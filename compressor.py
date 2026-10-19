#!/usr/bin/env python3
"""Context-Aware Hybrid Compressor CLI.

Production-grade command-line interface for the Context-Aware Hybrid Compressor (CACZ).
Provides commands for compression, decompression, container inspection, and benchmarking.
"""

import argparse
import os
import sys
import time

from core import (
    DEFAULT_CHUNK_SIZE,
    MAX_CHUNK_SIZE,
    MIN_CHUNK_SIZE,
    compress_file,
    decompress_file,
    inspect_container,
)
from core.codecs import (
    CorruptedDataError,
    DecompressionError,
    InvalidHeaderError,
    TruncatedDataError,
)


def format_bytes(size: float) -> str:
    """Format byte counts into human-readable strings with binary prefixes."""
    units = ["B", "KiB", "MiB", "GiB"]
    val = float(size)
    for u in units:
        if abs(val) < 1024.0 or u == units[-1]:
            return f"{val:.2f} {u}" if u != "B" else f"{int(val)} B"
        val /= 1024.0
    return f"{val:.2f} TiB"


def cmd_compress(args: argparse.Namespace) -> int:
    """Execute file compression."""
    input_path = args.input
    output_path = args.output
    chunk_size = args.chunk_size
    mode = args.mode

    if not os.path.exists(input_path):
        print(f"Error: Input file does not exist: {input_path}", file=sys.stderr)
        return 1

    if os.path.abspath(input_path) == os.path.abspath(output_path):
        print("Error: Input and output paths cannot be identical.", file=sys.stderr)
        return 1

    print(f"==> Compressing: {input_path}")
    print(f"    Mode: {mode.upper()} | Chunk Size: {chunk_size} bytes")

    t0 = time.perf_counter()
    try:
        metadata, results = compress_file(
            input_path=input_path,
            output_path=output_path,
            chunk_size=chunk_size,
            mode=mode,
        )
    except PermissionError as e:
        print(f"Permission error: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"Compression failed: {e}", file=sys.stderr)
        return 2
    elapsed = time.perf_counter() - t0

    orig = metadata.original_size
    comp = metadata.compressed_size
    ratio = (comp / orig) if orig > 0 else 1.0
    space_saved = (1.0 - ratio) * 100.0
    speed_mib = (orig / (1024 * 1024)) / elapsed if elapsed > 0 else 0.0

    print(f"==> Success!")
    print(f"    Original Size   : {format_bytes(orig)} ({orig:,} bytes)")
    print(f"    Compressed Size : {format_bytes(comp)} ({comp:,} bytes)")
    print(f"    Storage Ratio   : {ratio:.4f} (lower is better)")
    print(f"    Space Saved     : {space_saved:+.2f}%")
    print(f"    Total Chunks    : {metadata.total_chunks}")
    print(f"    Elapsed Time    : {elapsed:.3f} s ({speed_mib:.2f} MiB/s)")

    if args.verbose and results:
        print("\n--- Per-Chunk Breakdown ---")
        for r in results:
            print(
                f"  Chunk {r.chunk_index:3d}: Codec={r.selected_name:7s} | "
                f"Orig={r.original_size:5d} B | Enc={r.selected_size:5d} B | "
                f"Entropy={r.features.entropy:.2f}"
            )

    return 0


def cmd_decompress(args: argparse.Namespace) -> int:
    """Execute file decompression."""
    input_path = args.input
    output_path = args.output

    if not os.path.exists(input_path):
        print(f"Error: Compressed file does not exist: {input_path}", file=sys.stderr)
        return 1

    if os.path.abspath(input_path) == os.path.abspath(output_path):
        print("Error: Input and output paths cannot be identical.", file=sys.stderr)
        return 1

    print(f"==> Decompressing: {input_path}")
    t0 = time.perf_counter()
    try:
        metadata = decompress_file(input_path, output_path)
    except (InvalidHeaderError, CorruptedDataError, TruncatedDataError) as e:
        print(f"Corrupted container error: {e}", file=sys.stderr)
        return 2
    except PermissionError as e:
        print(f"Permission error: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"Decompression failed: {e}", file=sys.stderr)
        return 2
    elapsed = time.perf_counter() - t0

    orig = metadata.original_size
    speed_mib = (orig / (1024 * 1024)) / elapsed if elapsed > 0 else 0.0

    print(f"==> Decompression verified!")
    print(f"    Container Version : v{metadata.version}")
    print(f"    Recorded Mode     : {metadata.mode.upper()}")
    print(f"    Reconstructed     : {output_path}")
    print(f"    Output Size       : {format_bytes(orig)} ({orig:,} bytes)")
    print(f"    Total Chunks      : {metadata.total_chunks}")
    print(f"    Elapsed Time      : {elapsed:.3f} s ({speed_mib:.2f} MiB/s)")
    return 0


def cmd_inspect(args: argparse.Namespace) -> int:
    """Inspect binary container metadata without decompression."""
    file_path = args.file
    if not os.path.exists(file_path):
        print(f"Error: File does not exist: {file_path}", file=sys.stderr)
        return 1

    try:
        report = inspect_container(file_path)
    except (InvalidHeaderError, CorruptedDataError, TruncatedDataError) as e:
        print(f"Corrupted container: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"Failed to inspect container: {e}", file=sys.stderr)
        return 2

    meta = report.metadata
    orig = meta.original_size
    comp = meta.compressed_size
    ratio = (comp / orig) if orig > 0 else 1.0
    space_saved = (1.0 - ratio) * 100.0

    print(f"==> CACZ Container Inspection: {file_path}")
    print(f"    Format Version  : v{meta.version}")
    print(f"    Compression Mode: {meta.mode.upper()}")
    print(f"    Chunk Size      : {meta.chunk_size} bytes")
    print(f"    Total Chunks    : {meta.total_chunks}")
    print(f"    Original Size   : {format_bytes(orig)} ({orig:,} bytes)")
    print(f"    Container Size  : {format_bytes(comp)} ({comp:,} bytes)")
    print(f"    Storage Ratio   : {ratio:.4f}")
    print(f"    Space Saved     : {space_saved:+.2f}%")

    print("\n--- Codec Selection Distribution ---")
    header_str = f"  {'Codec':<10} | {'Chunks':>8} | {'Proportion':>10} | {'Payload Bytes':>14}"
    print(header_str)
    print("  " + "-" * (len(header_str) - 2))

    total_chunks = max(1, meta.total_chunks)
    for cname, count in report.codec_counts.items():
        if count > 0:
            prop = (count / total_chunks) * 100.0
            pbytes = report.codec_bytes[cname]
            print(f"  {cname:<10} | {count:>8d} | {prop:>9.1f}% | {format_bytes(pbytes):>14}")

    if args.verbose and report.chunks:
        print("\n--- Chunk Table ---")
        print(f"  {'Index':>5} | {'Codec':<8} | {'Orig Size':>9} | {'Payload':>9} | {'CRC32':>10}")
        print("  " + "-" * 50)
        for c in report.chunks:
            print(
                f"  {c.index:5d} | {c.codec_name:<8} | {c.original_length:9d} | "
                f"{c.payload_length:9d} | 0x{c.crc32:08X}"
            )

    return 0


def cmd_benchmark(args: argparse.Namespace) -> int:
    """Run benchmark suite on directory."""
    try:
        from benchmark.run_benchmark import run_benchmarks
    except ImportError:
        print("Error: Could not import benchmark module.", file=sys.stderr)
        return 1

    return run_benchmarks(
        test_dir=args.directory,
        chunk_size=args.chunk_size,
        output_dir=args.output_dir,
    )


def build_parser() -> argparse.ArgumentParser:
    """Construct command-line argument parser."""
    parser = argparse.ArgumentParser(
        prog="compressor.py",
        description="Context-Aware Hybrid Compressor (CACZ) - Multi-Algorithm Hybrid Archiver",
    )
    subparsers = parser.add_subparsers(dest="command", help="Available subcommands")

    # Subcommand: compress
    p_comp = subparsers.add_parser("compress", help="Compress a file into a CACZ container")
    p_comp.add_argument("input", help="Path to input file")
    p_comp.add_argument("output", help="Path to output CACZ file")
    p_comp.add_argument(
        "--chunk-size",
        type=int,
        default=DEFAULT_CHUNK_SIZE,
        help=f"Chunk size in bytes [{MIN_CHUNK_SIZE}..{MAX_CHUNK_SIZE}], default: {DEFAULT_CHUNK_SIZE}",
    )
    p_comp.add_argument(
        "--mode",
        choices=["heuristic", "oracle"],
        default="heuristic",
        help="Codec selection mode (default: heuristic)",
    )
    p_comp.add_argument("-v", "--verbose", action="store_true", help="Print per-chunk selection details")
    p_comp.set_defaults(func=cmd_compress)

    # Subcommand: decompress
    p_dec = subparsers.add_parser("decompress", help="Decompress a CACZ container")
    p_dec.add_argument("input", help="Path to CACZ compressed file")
    p_dec.add_argument("output", help="Path to reconstructed output file")
    p_dec.add_argument("-v", "--verbose", action="store_true", help="Verbose verification details")
    p_dec.set_defaults(func=cmd_decompress)

    # Subcommand: inspect
    p_insp = subparsers.add_parser("inspect", help="Inspect container metadata and chunk distribution")
    p_insp.add_argument("file", help="Path to CACZ compressed file")
    p_insp.add_argument("-v", "--verbose", action="store_true", help="Print full chunk table")
    p_insp.set_defaults(func=cmd_inspect)

    # Subcommand: benchmark
    p_bench = subparsers.add_parser("benchmark", help="Run comparative benchmark suite against baselines")
    p_bench.add_argument("directory", nargs="?", default="benchmark/test_files", help="Path to test files directory")
    p_bench.add_argument("--chunk-size", type=int, default=DEFAULT_CHUNK_SIZE, help="Chunk size for benchmarks")
    p_bench.add_argument("--output-dir", default="benchmark/results", help="Directory to save CSV, JSON, and charts")
    p_bench.set_defaults(func=cmd_benchmark)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if not hasattr(args, "func"):
        parser.print_help()
        return 1
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
