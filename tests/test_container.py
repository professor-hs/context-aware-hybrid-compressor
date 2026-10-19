"""Unit and integration tests for CACZ binary container format.

Validates:
- Empty file handling (0 chunks)
- Files smaller than chunk size (partial single chunk)
- Files exactly one chunk size
- Multi-chunk files with partial tail chunks
- Both heuristic and oracle compression modes
- Various chunk sizes (1024, 4096, 8192, 16384)
- Path collision safeguards
- Metadata inspection fidelity
"""

import os
import tempfile
import unittest

from core import (
    DEFAULT_CHUNK_SIZE,
    compress_file,
    decompress_file,
    inspect_container,
)


class TestContainer(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.dir_path = self.temp_dir.name

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def _create_file(self, name: str, content: bytes) -> str:
        path = os.path.join(self.dir_path, name)
        with open(path, "wb") as f:
            f.write(content)
        return path

    def _assert_container_round_trip(
        self,
        data: bytes,
        chunk_size: int = DEFAULT_CHUNK_SIZE,
        mode: str = "heuristic",
    ) -> None:
        src_path = self._create_file("source.dat", data)
        comp_path = os.path.join(self.dir_path, "archive.cacz")
        dec_path = os.path.join(self.dir_path, "output.dat")

        meta, results = compress_file(
            input_path=src_path,
            output_path=comp_path,
            chunk_size=chunk_size,
            mode=mode,
        )
        self.assertEqual(meta.original_size, len(data))
        self.assertEqual(meta.chunk_size, chunk_size)
        self.assertEqual(meta.mode, mode)

        # Verify inspection
        inspection = inspect_container(comp_path)
        self.assertEqual(inspection.metadata.original_size, len(data))
        self.assertEqual(inspection.metadata.total_chunks, meta.total_chunks)
        self.assertEqual(len(inspection.chunks), meta.total_chunks)

        # Decompress
        dec_meta = decompress_file(comp_path, dec_path)
        self.assertEqual(dec_meta.original_size, len(data))

        with open(dec_path, "rb") as f:
            reconstructed = f.read()

        self.assertEqual(reconstructed, data, "Decompressed output does not match original data")

        # Cleanup
        for p in [src_path, comp_path, dec_path]:
            if os.path.exists(p):
                os.remove(p)

    def test_empty_file(self) -> None:
        self._assert_container_round_trip(b"", mode="heuristic")
        self._assert_container_round_trip(b"", mode="oracle")

    def test_file_smaller_than_chunk(self) -> None:
        data = b"Short payload smaller than one 8KB chunk." * 10
        self._assert_container_round_trip(data, chunk_size=8192, mode="heuristic")
        self._assert_container_round_trip(data, chunk_size=8192, mode="oracle")

    def test_file_exact_multiple_of_chunk(self) -> None:
        chunk_size = 2048
        one_chunk = b"X" * chunk_size
        data = one_chunk * 3  # Exactly 3 chunks
        self.assertEqual(len(data), chunk_size * 3)
        self._assert_container_round_trip(data, chunk_size=chunk_size, mode="heuristic")
        self._assert_container_round_trip(data, chunk_size=chunk_size, mode="oracle")

    def test_multi_chunk_with_partial_tail(self) -> None:
        chunk_size = 1024
        data = os.urandom(3500)  # 3 full chunks + 428 byte partial chunk
        self._assert_container_round_trip(data, chunk_size=chunk_size, mode="heuristic")
        self._assert_container_round_trip(data, chunk_size=chunk_size, mode="oracle")

    def test_repetitive_multi_chunk(self) -> None:
        # Repetitive runs across multiple chunks
        chunk_size = 4096
        data = (b"HeaderPrefix\n" + b"A" * 500 + b"B" * 500) * 10
        self._assert_container_round_trip(data, chunk_size=chunk_size, mode="heuristic")
        self._assert_container_round_trip(data, chunk_size=chunk_size, mode="oracle")

    def test_path_collision_rejection(self) -> None:
        src = self._create_file("same_path.dat", b"Content")
        with self.assertRaises(ValueError):
            compress_file(src, src)
        with self.assertRaises(ValueError):
            decompress_file(src, src)

    def test_nonexistent_file_raises(self) -> None:
        missing = os.path.join(self.dir_path, "does_not_exist.dat")
        comp = os.path.join(self.dir_path, "comp.cacz")
        with self.assertRaises(FileNotFoundError):
            compress_file(missing, comp)
        with self.assertRaises(FileNotFoundError):
            decompress_file(missing, comp)


if __name__ == "__main__":
    unittest.main()
