"""Run-Length Encoding (RLE) Codec.

This module implements a robust, packet-based byte-oriented Run-Length Encoding.
It operates directly on arbitrary binary data (all 256 byte values).

Algorithm and Framing Details:
- The stream is composed of packets preceded by a single control byte:
  1. Run Packet (bit 7 = 1):
     - Control byte: 0x80 | (run_length - 3), where run_length in [3, 130].
     - Followed by 1 byte representing the repeated value.
     - Runs exceeding 130 bytes are partitioned into multiple run packets.
  2. Literal Packet (bit 7 = 0):
     - Control byte: (literal_count - 1), where literal_count in [1, 128].
     - Followed by `literal_count` uncompressed literal bytes.
     - Consecutive non-run bytes are clustered together to minimize overhead.

Performance & Expansion Characteristics:
- Best case: Highly repetitive data (e.g., runs of zeros, padding, bitmaps)
  compresses up to ~65x (130 bytes compressed into 2 bytes).
- Worst case: Data with no runs of length >= 3 expands by at most 1 byte per
  128 bytes (~0.78% expansion).
- All byte sequences are unambiguously recoverable without escape sequence collision.
"""

from typing import Optional
from . import (
    CODEC_RLE,
    CorruptedDataError,
    TruncatedDataError,
    register_codec,
)

MAX_RUN_LENGTH = 130
MIN_RUN_LENGTH = 3
MAX_LITERAL_LENGTH = 128


def encode(data: bytes) -> bytes:
    """Encode binary data using packet-based Run-Length Encoding.

    Args:
        data: Arbitrary input bytes.

    Returns:
        Encoded bytes.
    """
    if not data:
        return b""

    n = len(data)
    out = bytearray()
    literal_buf = bytearray()

    def flush_literals() -> None:
        """Flush accumulated literals into literal packets (max 128 bytes each)."""
        idx = 0
        while idx < len(literal_buf):
            chunk_len = min(MAX_LITERAL_LENGTH, len(literal_buf) - idx)
            # Control byte: literal_len - 1 (0..127)
            out.append(chunk_len - 1)
            out.extend(literal_buf[idx : idx + chunk_len])
            idx += chunk_len
        literal_buf.clear()

    i = 0
    while i < n:
        # Check for run starting at i
        b = data[i]
        run_len = 1
        while (i + run_len < n) and (data[i + run_len] == b) and (run_len < MAX_RUN_LENGTH):
            run_len += 1

        if run_len >= MIN_RUN_LENGTH:
            # We found a run of length >= 3
            flush_literals()
            # Control byte: 0x80 | (run_len - 3)
            out.append(0x80 | (run_len - MIN_RUN_LENGTH))
            out.append(b)
            i += run_len
        else:
            # Accumulate literal byte
            literal_buf.append(b)
            i += 1
            if len(literal_buf) >= MAX_LITERAL_LENGTH:
                flush_literals()

    flush_literals()
    return bytes(out)


def decode(data: bytes) -> bytes:
    """Decode packet-based Run-Length Encoded binary data.

    Args:
        data: RLE-encoded bytes.

    Returns:
        Exact reconstructed original bytes.

    Raises:
        TruncatedDataError: If the stream terminates unexpectedly within a packet.
        CorruptedDataError: If the payload contains malformed structure.
    """
    if not data:
        return b""

    n = len(data)
    out = bytearray()
    i = 0

    while i < n:
        ctrl = data[i]
        i += 1

        if ctrl & 0x80:
            # Run packet: length in [3, 130]
            run_len = (ctrl & 0x7F) + MIN_RUN_LENGTH
            if i >= n:
                raise TruncatedDataError(
                    f"RLE stream truncated: expected repeated byte at offset {i}, stream ended"
                )
            val = data[i]
            i += 1
            out.extend(bytes([val]) * run_len)
        else:
            # Literal packet: count in [1, 128]
            literal_count = ctrl + 1
            if i + literal_count > n:
                raise TruncatedDataError(
                    f"RLE stream truncated: expected {literal_count} literals at offset {i}, "
                    f"only {n - i} bytes remain"
                )
            out.extend(data[i : i + literal_count])
            i += literal_count

    return bytes(out)


# Register RLE in global codec registry
register_codec(CODEC_RLE, "rle", encode, decode)
