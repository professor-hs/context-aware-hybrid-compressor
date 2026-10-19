"""Unit tests for individual compression codecs (RLE, Huffman, LZ77, Delta).

Tests each codec for:
- Empty input
- Single byte input
- All identical bytes
- Long repeated runs
- Deterministic pseudo-random bytes
- All 256 byte values present
- Repeated substrings and phrases
- Natural text data
- Structured numeric sequences
- Boundary lengths (0, 1, 2, 3, 127, 128, 129, 255, 256, 1024, 4096, 8192)
- Rejection of malformed or truncated payloads
"""

import os
import random
import unittest
from typing import Callable

from core.codecs import (
    CorruptedDataError,
    DecompressionError,
    TruncatedDataError,
    delta,
    huffman,
    lz77,
    rle,
)


class BaseCodecTestCase:
    """Reusable test suite applicable to all codecs."""

    encode_fn: Callable[[bytes], bytes]
    decode_fn: Callable[[bytes], bytes]
    codec_name: str

    def assertRoundTrip(self, data: bytes) -> None:
        """Assert decode(encode(data)) == data bit-for-bit."""
        encoded = self.encode_fn(data)
        decoded = self.decode_fn(encoded)
        self.assertEqual(
            decoded,
            data,
            f"Codec {self.codec_name} failed round-trip for len {len(data)} (got {len(decoded)})",
        )

    def test_empty_input(self) -> None:
        self.assertRoundTrip(b"")

    def test_single_byte(self) -> None:
        for val in [0x00, 0x41, 0x7F, 0x80, 0xFF]:
            self.assertRoundTrip(bytes([val]))

    def test_all_identical_bytes(self) -> None:
        for count in [2, 3, 4, 10, 127, 128, 130, 256, 1000]:
            self.assertRoundTrip(b"A" * count)
            self.assertRoundTrip(b"\x00" * count)
            self.assertRoundTrip(b"\xFF" * count)

    def test_all_256_byte_values(self) -> None:
        all_bytes = bytes(range(256))
        self.assertRoundTrip(all_bytes)
        # Repeated permutation of all 256 bytes
        rng = random.Random(1337)
        perm = list(range(256))
        rng.shuffle(perm)
        self.assertRoundTrip(bytes(perm) * 4)

    def test_random_bytes(self) -> None:
        rng = random.Random(42)
        for size in [10, 64, 256, 1024, 4096]:
            rand_bytes = rng.randbytes(size)
            self.assertRoundTrip(rand_bytes)

    def test_text_and_repeated_substrings(self) -> None:
        text = b"The quick brown fox jumps over the lazy dog. " * 25
        self.assertRoundTrip(text)
        pattern = b"abc_123_xyz_456_" * 40
        self.assertRoundTrip(pattern)

    def test_numeric_sequences(self) -> None:
        seq = bytes(i % 256 for i in range(2000))
        self.assertRoundTrip(seq)

    def test_boundary_lengths(self) -> None:
        for length in [1, 2, 3, 4, 7, 15, 127, 128, 129, 255, 256, 1024, 4096]:
            data = bytes((k * 31 + 17) % 256 for k in range(length))
            self.assertRoundTrip(data)

    def test_truncated_payload_raises(self) -> None:
        data = b"Testing truncation safety on payload " * 10
        encoded = self.encode_fn(data)
        if len(encoded) > 2:
            # Truncate halfway or chop off last byte
            for cut in [1, len(encoded) // 2, len(encoded) - 1]:
                with self.assertRaises(DecompressionError):
                    self.decode_fn(encoded[:cut])

    def test_corrupted_payload_safety(self) -> None:
        data = b"Arbitrary structured payload for testing corruption detection." * 5
        encoded = bytearray(self.encode_fn(data))
        if len(encoded) > 4:
            # Flip random bits in the payload
            encoded[len(encoded) // 2] ^= 0xFF
            try:
                res = self.decode_fn(bytes(encoded))
                # If it doesn't raise, the decoded result must not equal original data
                self.assertNotEqual(res, data)
            except DecompressionError:
                pass  # Expected safe rejection


class TestRLECodec(unittest.TestCase, BaseCodecTestCase):
    encode_fn = staticmethod(rle.encode)
    decode_fn = staticmethod(rle.decode)
    codec_name = "RLE"

    def test_rle_specific_compression(self) -> None:
        data = b"X" * 1000
        encoded = rle.encode(data)
        # Should be dramatically compressed (e.g. ~16 bytes for 1000 bytes)
        self.assertLess(len(encoded), 30)

    def test_rle_alternating_literals(self) -> None:
        data = b"ABABABABABABABABABABABABABAB"
        self.assertRoundTrip(data)


class TestHuffmanCodec(unittest.TestCase, BaseCodecTestCase):
    encode_fn = staticmethod(huffman.encode)
    decode_fn = staticmethod(huffman.decode)
    codec_name = "Huffman"

    def test_single_symbol_payload(self) -> None:
        data = b"Z" * 500
        self.assertRoundTrip(data)

    def test_skewed_frequency_compression(self) -> None:
        data = (b"E" * 800) + (b"T" * 150) + (b"A" * 40) + (b"Z" * 10)
        encoded = huffman.encode(data)
        self.assertLess(len(encoded), len(data))

    def test_deterministic_reproducibility(self) -> None:
        data = b"Reproducibility test across multiple runs for identical trees." * 10
        enc1 = huffman.encode(data)
        enc2 = huffman.encode(data)
        self.assertEqual(enc1, enc2)


class TestLZ77Codec(unittest.TestCase, BaseCodecTestCase):
    encode_fn = staticmethod(lz77.encode)
    decode_fn = staticmethod(lz77.decode)
    codec_name = "LZ77"

    def test_overlapping_match_expansion(self) -> None:
        # Repeating pattern where length > offset
        data = b"A" * 200
        self.assertRoundTrip(data)
        encoded = lz77.encode(data)
        self.assertLess(len(encoded), 25)

    def test_repeated_phrases(self) -> None:
        phrase = b"This is a long repeated phrase that sliding window compressor should find easily! "
        data = phrase * 20
        self.assertRoundTrip(data)
        encoded = lz77.encode(data)
        self.assertLess(len(encoded), len(data) // 4)

    def test_large_window_reference(self) -> None:
        prefix = b"UniquePrefix_" * 10
        padding = os.urandom(2000)
        data = prefix + padding + prefix
        self.assertRoundTrip(data)


class TestDeltaCodec(unittest.TestCase, BaseCodecTestCase):
    encode_fn = staticmethod(delta.encode)
    decode_fn = staticmethod(delta.decode)
    codec_name = "Delta"

    def test_monotonic_counter_compression(self) -> None:
        # Sequential values: 0, 1, 2, ..., 255 repeated 4 times
        data = bytes(i % 256 for i in range(1024))
        self.assertRoundTrip(data)
        encoded = delta.encode(data)
        # Transformed deltas are all 1s -> should be heavily compressed
        self.assertLess(len(encoded), 50)

    def test_constant_slope_compression(self) -> None:
        # Values with stride +5 mod 256
        data = bytes((i * 5) % 256 for i in range(500))
        self.assertRoundTrip(data)
        encoded = delta.encode(data)
        self.assertLess(len(encoded), 30)

    def test_delta_round_trip_arbitrary(self) -> None:
        data = bytes([10, 50, 200, 255, 0, 1, 2, 254, 128])
        self.assertRoundTrip(data)


if __name__ == "__main__":
    unittest.main()
