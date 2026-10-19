"""Per-Chunk Feature Analysis Engine.

This module computes statistical, structural, and information-theoretic features
for arbitrary binary chunks to guide heuristic codec selection.

Features Computed:
1. Shannon Entropy: Bits per byte in [0.0, 8.0] representing average symbol information.
2. Run-Length Ratio: Proportion of bytes belonging to runs of >= 3 identical consecutive bytes.
3. Repeated-Substring Ratio: Proportion of 4-grams that match previously observed substrings.
4. Byte-Delta Variance: Variance of adjacent modulo-256 byte differences.
5. Delta-Run Ratio: Proportion of byte deltas belonging to runs of >= 3 identical deltas.
6. Unique-Byte Count: Total distinct symbol count in [0, 256].
7. Top-Byte Concentration: Cumulative frequency ratio of the top 4 most frequent symbols.
8. Chunk Length: Size of the chunk in bytes.
"""

import math
from collections import Counter
from dataclasses import dataclass
from typing import NamedTuple


class ChunkFeatures(NamedTuple):
    length: int
    entropy: float
    run_length_ratio: float
    repeated_substring_ratio: float
    byte_delta_variance: float
    delta_run_ratio: float
    unique_bytes: int
    top_byte_concentration: float


def analyze_chunk(data: bytes) -> ChunkFeatures:
    """Analyze a binary chunk and return structured feature measurements.

    Args:
        data: Arbitrary binary chunk bytes.

    Returns:
        ChunkFeatures instance with strictly validated numerical metrics.
    """
    n = len(data)
    if n == 0:
        return ChunkFeatures(
            length=0,
            entropy=0.0,
            run_length_ratio=0.0,
            repeated_substring_ratio=0.0,
            byte_delta_variance=0.0,
            delta_run_ratio=0.0,
            unique_bytes=0,
            top_byte_concentration=0.0,
        )

    # 1. Frequency counts, unique symbols, and Shannon Entropy
    freqs = Counter(data)
    unique_count = len(freqs)
    entropy = 0.0
    for count in freqs.values():
        p = count / n
        entropy -= p * math.log2(p)

    # Top-4 symbol concentration
    most_common = freqs.most_common(4)
    top_freq_sum = sum(cnt for _, cnt in most_common)
    top_byte_concentration = top_freq_sum / n

    # 2. Run-Length Ratio (runs of >= 3 identical bytes)
    run_bytes_count = 0
    i = 0
    while i < n:
        b = data[i]
        run_len = 1
        while (i + run_len < n) and (data[i + run_len] == b):
            run_len += 1
        if run_len >= 3:
            run_bytes_count += run_len
        i += run_len
    run_length_ratio = run_bytes_count / n

    # 3. Repeated-Substring Ratio (using 4-gram sliding window)
    ngram_size = 4
    if n >= ngram_size:
        seen_ngrams: set[bytes] = set()
        matched_ngrams = 0
        total_ngrams = n - ngram_size + 1
        for j in range(total_ngrams):
            gram = data[j : j + ngram_size]
            if gram in seen_ngrams:
                matched_ngrams += 1
            else:
                seen_ngrams.add(gram)
        repeated_substring_ratio = matched_ngrams / total_ngrams
    else:
        repeated_substring_ratio = 0.0

    # 4. Byte-Delta Variance and Delta-Run Ratio
    if n > 1:
        num_deltas = n - 1
        deltas = bytearray(num_deltas)
        prev = data[0]
        delta_sum = 0
        for k in range(1, n):
            curr = data[k]
            d = (curr - prev) & 0xFF
            deltas[k - 1] = d
            delta_sum += d
            prev = curr

        mean_delta = delta_sum / num_deltas
        var_sum = 0.0
        for d in deltas:
            diff = d - mean_delta
            var_sum += diff * diff
        byte_delta_variance = var_sum / num_deltas

        # Delta run ratio (runs of >= 3 identical deltas)
        delta_run_count = 0
        idx = 0
        while idx < num_deltas:
            val = deltas[idx]
            rlen = 1
            while (idx + rlen < num_deltas) and (deltas[idx + rlen] == val):
                rlen += 1
            if rlen >= 3:
                delta_run_count += rlen
            idx += rlen
        delta_run_ratio = delta_run_count / num_deltas
    else:
        byte_delta_variance = 0.0
        delta_run_ratio = 0.0

    return ChunkFeatures(
        length=n,
        entropy=entropy,
        run_length_ratio=run_length_ratio,
        repeated_substring_ratio=repeated_substring_ratio,
        byte_delta_variance=byte_delta_variance,
        delta_run_ratio=delta_run_ratio,
        unique_bytes=unique_count,
        top_byte_concentration=top_byte_concentration,
    )
