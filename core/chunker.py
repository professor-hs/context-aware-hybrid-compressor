"""Streaming File Chunker for Context-Aware Hybrid Compressor.

This module provides fixed-size chunking over streaming file-like objects or paths.
It ensures bounded memory usage (O(chunk_size)) regardless of total file size.

Key Invariants:
- Chunk size is bounded between MIN_CHUNK_SIZE (1 KiB) and MAX_CHUNK_SIZE (1 MiB).
- Empty files yield 0 chunks without error.
- Files smaller than chunk size yield exactly 1 partial chunk.
- Files exact multiples of chunk size yield N full chunks (no trailing empty chunk).
- Preserves exact byte ordering and bit integrity.
"""

import os
from typing import BinaryIO, Iterator, Union

DEFAULT_CHUNK_SIZE: int = 8192
MIN_CHUNK_SIZE: int = 1024
MAX_CHUNK_SIZE: int = 1024 * 1024  # 1 MiB


def validate_chunk_size(chunk_size: int) -> int:
    """Validate that chunk_size is an integer within acceptable bounds.

    Args:
        chunk_size: Proposed chunk size in bytes.

    Returns:
        Validated chunk size.

    Raises:
        ValueError: If chunk_size is outside [MIN_CHUNK_SIZE, MAX_CHUNK_SIZE].
    """
    if not isinstance(chunk_size, int):
        raise TypeError(f"chunk_size must be an integer, got {type(chunk_size).__name__}")
    if chunk_size < MIN_CHUNK_SIZE or chunk_size > MAX_CHUNK_SIZE:
        raise ValueError(
            f"chunk_size {chunk_size} out of valid range [{MIN_CHUNK_SIZE}, {MAX_CHUNK_SIZE}] bytes"
        )
    return chunk_size


def iter_chunks_from_stream(
    stream: BinaryIO,
    chunk_size: int = DEFAULT_CHUNK_SIZE,
) -> Iterator[tuple[int, bytes]]:
    """Yield (chunk_index, chunk_bytes) sequentially from an open binary stream.

    Args:
        stream: Readable binary stream.
        chunk_size: Size in bytes of each chunk.

    Yields:
        Tuples of (0-based chunk_index, chunk_bytes).
    """
    valid_size = validate_chunk_size(chunk_size)
    chunk_idx = 0
    while True:
        chunk = stream.read(valid_size)
        if not chunk:
            break
        yield (chunk_idx, chunk)
        chunk_idx += 1


def iter_chunks_from_file(
    file_path: Union[str, os.PathLike],
    chunk_size: int = DEFAULT_CHUNK_SIZE,
) -> Iterator[tuple[int, bytes]]:
    """Stream chunks from a file path with bounded memory.

    Args:
        file_path: Path to the input file.
        chunk_size: Size in bytes of each chunk.

    Yields:
        Tuples of (0-based chunk_index, chunk_bytes).
    """
    with open(file_path, "rb") as f:
        yield from iter_chunks_from_stream(f, chunk_size=chunk_size)
