"""Byte-Level Delta Encoding Codec.

This module implements reversible modulo-256 byte-delta transformation
coupled with run-length packing for compressible difference representations.

Theoretical Background and Reality of Delta Encoding:
- The discrete delta transform computes:
    t[0] = d[0]
    t[i] = (d[i] - d[i - 1]) mod 256  for i >= 1
- By itself, this transform is an information-preserving bijection: it maps an
  N-byte sequence to an N-byte sequence of differences. It DOES NOT reduce size on its own.
- In fact, for random or chaotic data, delta transformation can increase symbol entropy.
- However, when data exhibits linear trends, monotonic counters (e.g. timestamps,
  record IDs, sequence numbers), or smooth signals, the transformed sequence t[i]
  collapses into long runs of identical constants (e.g., runs of +1, +0, or fixed strides).
- To realize actual compression, this codec couples the modulo-256 delta transform
  with run-length packing (RLE). Chunks with structured delta patterns achieve dramatic
  compression (e.g. 25x reduction on monotonic sequences), while unpredictable streams
  are flagged for raw fallback by the selector.

Interface:
    encode(data: bytes) -> bytes
    decode(data: bytes) -> bytes
"""

from typing import Optional
from . import (
    CODEC_DELTA,
    CorruptedDataError,
    TruncatedDataError,
    register_codec,
)
from .rle import encode as rle_encode, decode as rle_decode

DELTA_MAGIC_V1 = 0x01


def delta_transform(data: bytes) -> bytes:
    """Apply forward modulo-256 delta transformation.

    Args:
        data: Input bytes.

    Returns:
        Transformed bytes of identical length where each byte (after the first)
        is the difference from its predecessor modulo 256.
    """
    if not data:
        return b""
    n = len(data)
    out = bytearray(n)
    out[0] = data[0]
    prev = data[0]
    for i in range(1, n):
        curr = data[i]
        out[i] = (curr - prev) & 0xFF
        prev = curr
    return bytes(out)


def inverse_delta_transform(transformed: bytes) -> bytes:
    """Apply inverse modulo-256 delta transformation.

    Args:
        transformed: Sequence of delta-transformed bytes.

    Returns:
        Original reconstructed bytes.
    """
    if not transformed:
        return b""
    n = len(transformed)
    out = bytearray(n)
    curr = transformed[0]
    out[0] = curr
    for i in range(1, n):
        curr = (curr + transformed[i]) & 0xFF
        out[i] = curr
    return bytes(out)


def encode(data: bytes) -> bytes:
    """Compress binary data using modulo-256 delta transform followed by run packing.

    Args:
        data: Arbitrary input bytes.

    Returns:
        Encoded bytes.
    """
    if not data:
        return b""

    # Step 1: Forward modulo-256 transform
    transformed = delta_transform(data)

    # Step 2: Pack transformed difference stream using RLE
    packed = rle_encode(transformed)

    # Prepend 1-byte format identifier
    return bytes([DELTA_MAGIC_V1]) + packed


def decode(data: bytes) -> bytes:
    """Decompress delta-encoded bytes.

    Args:
        data: Encoded byte stream.

    Returns:
        Exact reconstructed original bytes.

    Raises:
        TruncatedDataError: If stream ends prematurely.
        CorruptedDataError: If format header or difference payload is invalid.
    """
    if not data:
        return b""

    if len(data) < 2:
        raise TruncatedDataError("Delta payload truncated: missing difference payload")

    format_id = data[0]
    if format_id != DELTA_MAGIC_V1:
        raise CorruptedDataError(f"Unsupported delta codec format identifier: 0x{format_id:02X}")

    packed_payload = data[1:]
    transformed = rle_decode(packed_payload)
    return inverse_delta_transform(transformed)


# Register Delta in global codec registry
register_codec(CODEC_DELTA, "delta", encode, decode)
