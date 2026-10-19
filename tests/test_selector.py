"""Unit tests for feature analyzer and heuristic/oracle selector."""

import math
import os
import unittest

from core.analyzer import analyze_chunk
from core.codecs import (
    CODEC_DELTA,
    CODEC_HUFFMAN,
    CODEC_LZ77,
    CODEC_RAW,
    CODEC_RLE,
    CODEC_NAMES,
)
from core.selector import (
    predict_codec_heuristic,
    select_and_encode_chunk,
)


class TestAnalyzerAndSelector(unittest.TestCase):
    def test_analyzer_empty_chunk(self) -> None:
        features = analyze_chunk(b"")
        self.assertEqual(features.length, 0)
        self.assertEqual(features.entropy, 0.0)
        self.assertEqual(features.run_length_ratio, 0.0)
        self.assertEqual(features.repeated_substring_ratio, 0.0)
        self.assertEqual(features.byte_delta_variance, 0.0)
        self.assertEqual(features.delta_run_ratio, 0.0)
        self.assertEqual(features.unique_bytes, 0)
        self.assertEqual(features.top_byte_concentration, 0.0)

    def test_analyzer_identical_bytes(self) -> None:
        data = b"A" * 1000
        features = analyze_chunk(data)
        self.assertEqual(features.length, 1000)
        self.assertAlmostEqual(features.entropy, 0.0, places=5)
        self.assertAlmostEqual(features.run_length_ratio, 1.0, places=5)
        self.assertEqual(features.unique_bytes, 1)
        self.assertAlmostEqual(features.top_byte_concentration, 1.0, places=5)

    def test_analyzer_uniform_entropy(self) -> None:
        data = bytes(range(256)) * 4  # Exactly equal frequency for all 256 symbols
        features = analyze_chunk(data)
        # Shannon entropy for 256 equally likely symbols is log2(256) = 8.0 bits
        self.assertAlmostEqual(features.entropy, 8.0, places=4)
        self.assertEqual(features.unique_bytes, 256)

    def test_heuristic_predicts_rle_on_runs(self) -> None:
        data = b"X" * 500 + b"Y" * 500
        features = analyze_chunk(data)
        predicted = predict_codec_heuristic(features)
        self.assertEqual(predicted, CODEC_RLE)

    def test_heuristic_predicts_delta_on_monotonic(self) -> None:
        data = bytes(i % 256 for i in range(1000))
        features = analyze_chunk(data)
        predicted = predict_codec_heuristic(features)
        self.assertEqual(predicted, CODEC_DELTA)

    def test_heuristic_predicts_lz77_on_repeated_text(self) -> None:
        phrase = b"This is a repeated phrase that indicates high substring redundancy. "
        data = phrase * 20
        features = analyze_chunk(data)
        predicted = predict_codec_heuristic(features)
        self.assertEqual(predicted, CODEC_LZ77)

    def test_heuristic_predicts_raw_on_tiny_chunk(self) -> None:
        data = b"Tiny"
        features = analyze_chunk(data)
        predicted = predict_codec_heuristic(features)
        self.assertEqual(predicted, CODEC_RAW)

    def test_heuristic_predicts_raw_on_high_entropy_random(self) -> None:
        data = os.urandom(4096)
        features = analyze_chunk(data)
        predicted = predict_codec_heuristic(features)
        self.assertEqual(predicted, CODEC_RAW)

    def test_oracle_selects_strictly_minimal_size(self) -> None:
        test_cases = [
            b"A" * 1000,
            bytes(i % 256 for i in range(1000)),
            b"Repeated structured phrases for compression testing! " * 25,
            os.urandom(500),
        ]
        for data in test_cases:
            res = select_and_encode_chunk(
                chunk_index=0,
                data=data,
                mode="oracle",
            )
            # The selected size must be <= all candidate sizes
            for cname, csize in res.candidate_sizes.items():
                self.assertLessEqual(
                    res.selected_size,
                    csize,
                    f"Oracle failed: selected {res.selected_size} > candidate {cname} {csize}",
                )

    def test_raw_fallback_prevents_expansion(self) -> None:
        # High entropy random payload that expands under compression
        data = os.urandom(1024)
        res = select_and_encode_chunk(
            chunk_index=0,
            data=data,
            mode="heuristic",
        )
        # Payload size must never exceed original uncompressed size
        self.assertLessEqual(res.selected_size, len(data))


if __name__ == "__main__":
    unittest.main()
