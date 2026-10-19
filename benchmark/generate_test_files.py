"""Synthetic Benchmark Dataset Generator.

Generates five standardized, deterministic benchmark datasets:
1. mixed_application.log - Semi-structured log records with repetitive templates and variable IDs.
2. numeric_records.csv   - Tabular numeric and categorical records with monotonic counters.
3. source_code.py        - Python source code with repeated keywords, indentation, and docstrings.
4. english_text.txt      - Natural-language English text corpus following Zipfian word frequencies.
5. already_compressed.zip- High-entropy compressed archive demonstrating raw fallback behavior.

All generators use fixed random seeds for strict bit-level reproducibility.
"""

import io
import os
import random
import sys
import zipfile

FIXED_SEED = 42


def generate_mixed_application_log(target_bytes: int = 160 * 1024) -> bytes:
    """Generate mixed application logs with periodic templates and bursty errors."""
    rng = random.Random(FIXED_SEED + 1)
    levels = ["INFO", "DEBUG", "WARN", "ERROR", "TRACE"]
    modules = ["auth.service", "db.pool", "http.router", "payment.gateway", "cache.redis", "worker.celery"]
    messages = [
        "Connection established to cluster node {node}",
        "User {user_id} authenticated successfully from IP 192.168.1.{ip}",
        "Query execution completed in {duration:.2f} ms for table 'orders'",
        "Cache hit ratio: {ratio:.1f}% on key 'session:{user_id}'",
        "Heartbeat probe received from monitor agent",
        "Failed to acquire lock on resource 'txn:{tx_id}', retrying...",
        "Transaction {tx_id} committed with status code 200",
        "Payload serialized with size {payload_size} bytes",
    ]

    buf = io.StringIO()
    sec = 1700000000
    while buf.tell() < target_bytes:
        sec += rng.randint(0, 3)
        lvl = rng.choices(levels, weights=[50, 30, 10, 5, 5])[0]
        mod = rng.choice(modules)
        msg_template = rng.choice(messages)
        msg = msg_template.format(
            node=rng.randint(1, 16),
            user_id=rng.randint(1000, 9999),
            ip=rng.randint(2, 254),
            duration=rng.uniform(0.1, 150.0),
            ratio=rng.uniform(70.0, 99.9),
            tx_id=f"tx_{rng.randint(100000, 999999)}",
            payload_size=rng.randint(64, 4096),
        )
        line = f"2026-03-15T12:00:{sec % 60:02d}.{rng.randint(100, 999):03d}Z [{lvl:5s}] {mod:15s}: {msg}\n"
        buf.write(line)

    return buf.getvalue()[:target_bytes].encode("utf-8")


def generate_numeric_records_csv(target_bytes: int = 160 * 1024) -> bytes:
    """Generate structured tabular CSV records with sequential IDs, dates, and metrics."""
    rng = random.Random(FIXED_SEED + 2)
    categories = ["Electronics", "Books", "HomeGoods", "Apparel", "Automotive", "Industrial"]
    regions = ["NA-EAST", "NA-WEST", "EU-CENTRAL", "AP-SOUTH", "SA-EAST"]
    payment_methods = ["CREDIT_CARD", "DEBIT_CARD", "ACH_TRANSFER", "CRYPTO", "PAYPAL"]

    buf = io.StringIO()
    buf.write("record_id,timestamp,category,region,quantity,unit_price,total_amount,payment_method,tax_rate\n")

    record_id = 100000
    while buf.tell() < target_bytes:
        record_id += 1
        cat = rng.choice(categories)
        reg = rng.choice(regions)
        pm = rng.choice(payment_methods)
        qty = rng.randint(1, 20)
        unit_price = round(rng.uniform(5.0, 500.0), 2)
        total = round(qty * unit_price, 2)
        tax = round(unit_price * 0.0825, 2)
        line = f"{record_id},2026-01-01T{rng.randint(0,23):02d}:{rng.randint(0,59):02d}:{rng.randint(0,59):02d},{cat},{reg},{qty},{unit_price:.2f},{total:.2f},{pm},{tax:.2f}\n"
        buf.write(line)

    return buf.getvalue()[:target_bytes].encode("utf-8")


def generate_source_code_py(target_bytes: int = 160 * 1024) -> bytes:
    """Generate realistic, synthetic Python source code with classes, docstrings, and loops."""
    rng = random.Random(FIXED_SEED + 3)
    header = (
        '"""Auto-generated synthetic benchmark module simulating enterprise Python architecture."""\n\n'
        "import os\nimport sys\nimport time\nfrom typing import Any, Optional, Union\n\n"
    )
    buf = io.StringIO()
    buf.write(header)

    class_idx = 0
    while buf.tell() < target_bytes:
        class_idx += 1
        cname = f"DataPipelineController{class_idx}"
        buf.write(f"class {cname}:\n")
        buf.write(f'    """Service controller handling pipeline stage {class_idx}."""\n\n')
        buf.write("    def __init__(self, config: dict[str, Any], timeout: float = 30.0) -> None:\n")
        buf.write("        self.config = config\n")
        buf.write("        self.timeout = timeout\n")
        buf.write("        self._state: dict[str, Any] = {}\n")
        buf.write("        self._initialized = True\n\n")

        for m_idx in range(4):
            mname = f"process_batch_{m_idx}"
            buf.write(f"    def {mname}(self, records: list[dict[str, Any]]) -> list[dict[str, Any]]:\n")
            buf.write(f'        """Execute batch transformation step {m_idx} on input records."""\n')
            buf.write("        results = []\n")
            buf.write("        for item in records:\n")
            buf.write("            transformed = {}\n")
            buf.write("            for key, val in item.items():\n")
            buf.write("                if isinstance(val, (int, float)):\n")
            buf.write(f"                    transformed[key] = val * {rng.uniform(1.01, 1.5):.3f} + {rng.randint(1, 10)}\n")
            buf.write("                elif isinstance(val, str):\n")
            buf.write("                    transformed[key] = val.strip().lower()\n")
            buf.write("                else:\n")
            buf.write("                    transformed[key] = None\n")
            buf.write("            results.append(transformed)\n")
            buf.write("        return results\n\n")

    return buf.getvalue()[:target_bytes].encode("utf-8")


def generate_english_text_txt(target_bytes: int = 160 * 1024) -> bytes:
    """Generate English-like text corpus with natural Zipfian word frequency distributions."""
    rng = random.Random(FIXED_SEED + 4)

    high_freq_words = [
        "the", "of", "and", "a", "to", "in", "is", "you", "that", "it",
        "he", "was", "for", "on", "are", "as", "with", "his", "they", "at",
        "be", "this", "have", "from", "or", "one", "had", "by", "word", "but",
    ]
    med_freq_words = [
        "system", "algorithm", "data", "compression", "performance", "memory",
        "throughput", "process", "analysis", "information", "structure", "function",
        "computer", "network", "channel", "entropy", "symbol", "frequency", "variable",
        "context", "hybrid", "selector", "container", "checksum", "evaluation",
    ]
    low_freq_words = [
        "deterministic", "asymptotic", "reproducibility", "uncompressed", "modality",
        "heuristic", "oracle", "discrepancy", "divergence", "redundancy", "canonical",
        "incompressible", "arbitrary", "statistical", "optimization", "serialization",
    ]

    weights = (
        [100] * len(high_freq_words)
        + [15] * len(med_freq_words)
        + [2] * len(low_freq_words)
    )
    all_words = high_freq_words + med_freq_words + low_freq_words

    buf = io.StringIO()
    while buf.tell() < target_bytes:
        sentence_len = rng.randint(8, 20)
        sentence_words = rng.choices(all_words, weights=weights, k=sentence_len)
        sentence_words[0] = sentence_words[0].capitalize()
        sentence = " ".join(sentence_words) + ". "
        buf.write(sentence)
        if rng.random() < 0.15:
            buf.write("\n\n")

    return buf.getvalue()[:target_bytes].encode("utf-8")


def generate_already_compressed_zip(target_bytes: int = 160 * 1024) -> bytes:
    """Generate a valid ZIP archive packed with DEFLATE-compressed synthetic files."""
    rng = random.Random(FIXED_SEED + 5)
    zip_buffer = io.BytesIO()

    with zipfile.ZipFile(zip_buffer, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        # File 1: pseudo-random noise
        noise = rng.randbytes(100 * 1024)
        zf.writestr("noise.bin", noise)

        # File 2: repetitious text that gets heavily compressed inside the zip
        text_corpus = ("The quick brown fox jumps over the lazy dog. Repetition reduces entropy.\n" * 1500).encode("utf-8")
        zf.writestr("corpus.txt", text_corpus)

        # File 3: numeric table
        num_data = bytes([i % 256 for i in range(50000)])
        zf.writestr("sequential.dat", num_data)

    data = zip_buffer.getvalue()
    # Pad or truncate cleanly to target_bytes if needed
    if len(data) > target_bytes:
        # Re-pack smaller if needed
        return data[:target_bytes]
    return data


DATASET_GENERATORS = {
    "mixed_application.log": generate_mixed_application_log,
    "numeric_records.csv": generate_numeric_records_csv,
    "source_code.py": generate_source_code_py,
    "english_text.txt": generate_english_text_txt,
    "already_compressed.zip": generate_already_compressed_zip,
}


def generate_all_datasets(output_dir: str = "benchmark/test_files") -> dict[str, str]:
    """Generate all standard benchmark files into output_dir.

    Args:
        output_dir: Directory where files will be written.

    Returns:
        Mapping of filename to absolute file path.
    """
    os.makedirs(output_dir, exist_ok=True)
    created_files: dict[str, str] = {}

    print(f"==> Generating benchmark datasets in: {output_dir}")
    for filename, generator in DATASET_GENERATORS.items():
        dest_path = os.path.join(output_dir, filename)
        content = generator()
        with open(dest_path, "wb") as f:
            f.write(content)
        size_kb = len(content) / 1024.0
        print(f"    Created: {filename:<25} ({size_kb:6.1f} KiB, {len(content):,} bytes)")
        created_files[filename] = os.path.abspath(dest_path)

    print("==> All 5 benchmark datasets successfully generated!\n")
    return created_files


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "benchmark/test_files"
    generate_all_datasets(out_dir)
