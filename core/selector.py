"""Context-Aware Codec Selection Engine.

This module implements two selection strategies:
1. Heuristic Mode: Uses statistical and structural chunk features to predict
   the most suitable compression codec in O(1) time without speculative encoding.
2. Oracle Mode: Speculatively encodes the chunk with all available codecs and
   selects the strictly minimal byte representation.

Both modes include an automatic uncompressed RAW fallback to ensure that chunks
are never expanded when compression algorithms fail to achieve positive savings.
"""

import time
from dataclasses import dataclass
from typing import NamedTuple, Optional
from core.codecs import (
    CODEC_DELTA,
    CODEC_HUFFMAN,
    CODEC_LZ77,
    CODEC_RAW,
    CODEC_RLE,
    CODEC_NAMES,
    raw_encode,
)
from core.codecs import delta, huffman, lz77, rle
from core.analyzer import ChunkFeatures, analyze_chunk


@dataclass
class ChunkSelectionResult:
    """Detailed metadata about codec selection for a single chunk."""

    chunk_index: int
    original_size: int
    selected_codec: int
    selected_name: str
    selected_payload: bytes
    selected_size: int
    features: ChunkFeatures
    mode: str
    selection_time: float
    candidate_sizes: dict[str, int]
    oracle_codec: Optional[int] = None
    matched_oracle: Optional[bool] = None


def predict_codec_heuristic(features: ChunkFeatures) -> int:
    """Predict the best codec using deterministic calibrated feature thresholds.

    Calibrated Selection Policy:
    1. Very small chunks (< 32 B) -> RAW (framing overhead exceeds potential savings).
    2. High entropy (>= 7.85) with low repeated substrings (< 0.15) -> RAW (incompressible).
    3. High repeated-substring ratio (>= 0.35) and repeated_substring_ratio > run_length_ratio -> LZ77.
    4. Dominant run-length ratio (>= 0.40) -> RLE.
    5. High delta-run ratio (>= 0.30) with low run-length ratio (< 0.25) -> DELTA.
    6. Repeated substrings (>= 0.20) and entropy < 7.6 -> LZ77.
    7. Low entropy (< 6.8) or skewed frequency distribution (top-4 >= 0.45 or unique <= 64) -> HUFFMAN.
    8. Moderate repeated patterns (>= 0.10) -> LZ77.
    9. Default -> HUFFMAN.
    """
    if features.length < 32:
        return CODEC_RAW

    if features.entropy >= 7.85 and features.repeated_substring_ratio < 0.15:
        return CODEC_RAW

    # Monotonic sequences or stepped numeric counters (dominant delta runs)
    if features.delta_run_ratio >= 0.50 and features.run_length_ratio < 0.25:
        return CODEC_DELTA

    # If repeated substrings dominate (e.g., source code, structured templates), LZ77 is superior
    if features.repeated_substring_ratio >= 0.35 and features.repeated_substring_ratio > features.run_length_ratio:
        return CODEC_LZ77

    # Dominant repeated runs of identical bytes
    if features.run_length_ratio >= 0.40:
        return CODEC_RLE

    # Secondary delta check for moderate delta runs
    if features.delta_run_ratio >= 0.30 and features.run_length_ratio < 0.25:
        return CODEC_DELTA

    if features.repeated_substring_ratio >= 0.20 and features.entropy < 7.6:
        return CODEC_LZ77

    if features.entropy < 6.8 or features.top_byte_concentration >= 0.45 or features.unique_bytes <= 64:
        return CODEC_HUFFMAN

    if features.repeated_substring_ratio >= 0.10:
        return CODEC_LZ77

    return CODEC_HUFFMAN


def _encode_with_codec(codec_id: int, data: bytes) -> bytes:
    """Execute encoding for a specific codec ID."""
    if codec_id == CODEC_RAW:
        return raw_encode(data)
    elif codec_id == CODEC_RLE:
        return rle.encode(data)
    elif codec_id == CODEC_HUFFMAN:
        return huffman.encode(data)
    elif codec_id == CODEC_LZ77:
        return lz77.encode(data)
    elif codec_id == CODEC_DELTA:
        return delta.encode(data)
    else:
        raise ValueError(f"Unknown codec ID: {codec_id}")


def select_and_encode_chunk(
    chunk_index: int,
    data: bytes,
    mode: str = "heuristic",
    evaluate_oracle: bool = False,
) -> ChunkSelectionResult:
    """Select appropriate codec and encode chunk according to specified mode.

    Args:
        chunk_index: 0-based sequence index.
        data: Raw uncompressed chunk bytes.
        mode: Either 'heuristic' or 'oracle'.
        evaluate_oracle: If True and mode is 'heuristic', also run the oracle
                         evaluation to record oracle match status and overhead.

    Returns:
        ChunkSelectionResult containing selected payload, metadata, and statistics.
    """
    t0 = time.perf_counter()
    features = analyze_chunk(data)
    n = len(data)
    candidate_sizes: dict[str, int] = {}

    if mode == "oracle" or evaluate_oracle:
        # Evaluate all candidates
        encoded_candidates: dict[int, bytes] = {}
        for cid, cname in CODEC_NAMES.items():
            enc = _encode_with_codec(cid, data)
            encoded_candidates[cid] = enc
            candidate_sizes[cname] = len(enc)

        # Deterministic tie-breaker priority order: RAW, RLE, DELTA, HUFFMAN, LZ77
        priority_order = [CODEC_RAW, CODEC_RLE, CODEC_DELTA, CODEC_HUFFMAN, CODEC_LZ77]
        best_cid = min(
            priority_order,
            key=lambda cid: candidate_sizes[CODEC_NAMES[cid]],
        )
        oracle_cid = best_cid
    else:
        oracle_cid = None

    if mode == "oracle":
        selected_cid = oracle_cid  # type: ignore
        selected_payload = encoded_candidates[selected_cid]  # type: ignore
        matched_oracle = True
    else:
        # Heuristic prediction
        predicted_cid = predict_codec_heuristic(features)
        predicted_payload = _encode_with_codec(predicted_cid, data)
        candidate_sizes[CODEC_NAMES[predicted_cid]] = len(predicted_payload)

        # Raw fallback if compression resulted in expansion
        if len(predicted_payload) >= n:
            selected_cid = CODEC_RAW
            selected_payload = data
            candidate_sizes["raw"] = n
        else:
            selected_cid = predicted_cid
            selected_payload = predicted_payload

        if oracle_cid is not None:
            matched_oracle = (selected_cid == oracle_cid)
        else:
            matched_oracle = None

    elapsed = time.perf_counter() - t0

    return ChunkSelectionResult(
        chunk_index=chunk_index,
        original_size=n,
        selected_codec=selected_cid,
        selected_name=CODEC_NAMES[selected_cid],
        selected_payload=selected_payload,
        selected_size=len(selected_payload),
        features=features,
        mode=mode,
        selection_time=elapsed,
        candidate_sizes=candidate_sizes,
        oracle_codec=oracle_cid,
        matched_oracle=matched_oracle,
    )
