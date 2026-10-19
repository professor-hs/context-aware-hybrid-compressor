"""Robustness, security, and corruption-handling tests for CACZ container.

Validates that malformed, corrupted, truncated, or malicious container payloads
are rejected safely with explicit exceptions and without unbounded memory allocations
or leaving behind corrupt partial output files.
"""

import os
import struct
import tempfile
import unittest

from core import (
    compress_file,
    decompress_file,
)
from core.codecs import (
    CorruptedDataError,
    DecompressionError,
    InvalidHeaderError,
    TruncatedDataError,
)


class TestContainerCorruption(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.dir_path = self.temp_dir.name

        # Create a valid baseline compressed container
        self.source_data = b"Testing robust corruption detection and error recovery." * 20
        self.src_file = os.path.join(self.dir_path, "orig.dat")
        with open(self.src_file, "wb") as f:
            f.write(self.source_data)

        self.valid_container = os.path.join(self.dir_path, "valid.cacz")
        compress_file(self.src_file, self.valid_container, chunk_size=1024)

        with open(self.valid_container, "rb") as f:
            self.valid_bytes = bytearray(f.read())

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def _write_and_decompress(self, corrupt_bytes: bytes) -> None:
        c_path = os.path.join(self.dir_path, "corrupt.cacz")
        out_path = os.path.join(self.dir_path, "output.dat")
        with open(c_path, "wb") as f:
            f.write(corrupt_bytes)
        try:
            decompress_file(c_path, out_path)
        finally:
            # Atomic safety check: corrupted decompression must NOT produce output
            if os.path.exists(out_path):
                os.remove(out_path)

    def test_corrupted_magic_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        corrupt[0:4] = b"NOPE"
        with self.assertRaises(InvalidHeaderError):
            self._write_and_decompress(bytes(corrupt))

    def test_unsupported_version_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        # Version is at offset 4..5
        corrupt[4:6] = struct.pack(">H", 99)
        with self.assertRaises(InvalidHeaderError):
            self._write_and_decompress(bytes(corrupt))

    def test_corrupted_header_checksum_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        # Flip bits in default chunk size (offset 8..11) while keeping header CRC unchanged
        corrupt[8] ^= 0xFF
        with self.assertRaises(CorruptedDataError):
            self._write_and_decompress(bytes(corrupt))

    def test_truncated_header_raises(self) -> None:
        # File shorter than 28 bytes
        with self.assertRaises(TruncatedDataError):
            self._write_and_decompress(self.valid_bytes[:20])

    def test_truncated_chunk_record_raises(self) -> None:
        # Cut stream right after file header (missing chunk header)
        with self.assertRaises(TruncatedDataError):
            self._write_and_decompress(self.valid_bytes[:35])

    def test_truncated_payload_raises(self) -> None:
        # Cut stream inside chunk payload
        with self.assertRaises(TruncatedDataError):
            self._write_and_decompress(self.valid_bytes[:-10])

    def test_invalid_codec_id_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        # First chunk starts at offset 28. Codec ID is at offset 28 + 4 = 32
        corrupt[32] = 0xFE  # Invalid codec ID
        with self.assertRaises(CorruptedDataError):
            self._write_and_decompress(bytes(corrupt))

    def test_chunk_payload_bitflip_crc_mismatch(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        # Flip bits in the middle of payload
        corrupt[-15] ^= 0x55
        with self.assertRaises(DecompressionError):
            self._write_and_decompress(bytes(corrupt))

    def test_out_of_order_chunk_index_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        # Chunk index is at offset 28..31
        corrupt[28:32] = struct.pack(">I", 999)
        with self.assertRaises(CorruptedDataError):
            self._write_and_decompress(bytes(corrupt))

    def test_unbounded_memory_length_exploit_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        # Original chunk length is at offset 28 + 6 = 34
        corrupt[34:38] = struct.pack(">I", 500 * 1024 * 1024)  # 500 MB claim
        with self.assertRaises(CorruptedDataError):
            self._write_and_decompress(bytes(corrupt))

    def test_trailing_garbage_raises(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        corrupt.extend(b"EXTRA_GARBAGE_BYTES")
        with self.assertRaises(CorruptedDataError):
            self._write_and_decompress(bytes(corrupt))

    def test_atomic_safety_no_partial_file_on_error(self) -> None:
        corrupt = bytearray(self.valid_bytes)
        corrupt[-15] ^= 0x55
        c_path = os.path.join(self.dir_path, "corrupt_atomic.cacz")
        out_path = os.path.join(self.dir_path, "should_not_exist.dat")
        with open(c_path, "wb") as f:
            f.write(corrupt)

        with self.assertRaises(DecompressionError):
            decompress_file(c_path, out_path)

        # Output file must NOT exist after failed decompression
        self.assertFalse(
            os.path.exists(out_path),
            "Corrupt decompression left behind partial output file!",
        )


if __name__ == "__main__":
    unittest.main()
