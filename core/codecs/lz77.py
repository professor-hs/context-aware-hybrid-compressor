"""Sliding-Window LZ77 / LZSS Codec.

This module implements a sliding-window dictionary compressor operating on
arbitrary binary payloads.

Algorithmic Design:
- Uses an LZSS flag-byte framing to differentiate literals from match references.
- Window size: 4096 bytes.
- Min match length: 3 bytes (minimum needed to yield size reduction).
- Max match length: 258 bytes (supports long matches, matching DEFLATE limits).
- Fast sliding-window search using a bounded-chain 3-byte hash table (O(1) candidate lookup).
- Correctly handles overlapping matches (where match_length > offset), allowing
  efficient representation of repetitive runs and repeated substrings.

Stream Framing:
- Byte 0..3: Original uncompressed size (uint32, big-endian).
- Followed by 8-item command blocks.
  Each block starts with 1 flag byte (bits 0..7 from LSB to MSB):
  - bit = 0: Literal byte (1 byte follows).
  - bit = 1: Match reference:
    - 2 bytes base reference:
      * offset: 12 bits (1..4096, stored as offset - 1 in 0..4095).
      * length code: 4 bits (0..14 for lengths 3..17; 15 indicates extended length).
    - If length code == 15:
      * 1 byte follows: extra_length (0..240, where total length = 18 + extra_length, up to 258).
"""

import struct
from typing import Optional
from . import (
    CODEC_LZ77,
    CorruptedDataError,
    TruncatedDataError,
    register_codec,
)

WINDOW_SIZE = 4096
MIN_MATCH = 3
MAX_MATCH = 258
MAX_CHAIN = 16  # Bounded search depth for consistent low latency


def encode(data: bytes) -> bytes:
    """Compress binary data using sliding-window LZ77 / LZSS.

    Args:
        data: Arbitrary input bytes.

    Returns:
        Encoded bytes including header and compressed token stream.
    """
    if not data:
        return b""

    n = len(data)
    out = bytearray(struct.pack(">I", n))

    # Fast hash table: 3-byte prefix -> list of recent offsets
    head: dict[bytes, list[int]] = {}

    # Token storage before packing into flag blocks:
    # A token is either (0, literal_byte) or (1, offset, length)
    tokens: list[tuple] = []

    def flush_tokens() -> None:
        """Pack accumulated tokens into 8-item flag blocks."""
        nonlocal tokens
        idx = 0
        num_tokens = len(tokens)
        while idx < num_tokens:
            group_size = min(8, num_tokens - idx)
            flags = 0
            payload = bytearray()

            for bit_pos in range(group_size):
                tok = tokens[idx + bit_pos]
                if tok[0] == 1:
                    # Match reference
                    flags |= 1 << bit_pos
                    offset, length = tok[1], tok[2]
                    offset_val = offset - 1
                    if length < 18:
                        len_code = length - 3
                        extra = None
                    else:
                        len_code = 15
                        extra = length - 18

                    b0 = (offset_val >> 4) & 0xFF
                    b1 = ((offset_val & 0x0F) << 4) | (len_code & 0x0F)
                    payload.append(b0)
                    payload.append(b1)
                    if extra is not None:
                        payload.append(extra)
                else:
                    # Literal
                    payload.append(tok[1])

            out.append(flags)
            out.extend(payload)
            idx += group_size
        tokens = []

    i = 0
    while i < n:
        best_offset = 0
        best_len = 0

        if i + MIN_MATCH <= n:
            prefix = data[i : i + MIN_MATCH]
            candidates = head.get(prefix)
            if candidates is not None:
                chain_count = 0
                # Search backwards from the most recent candidates
                for pos in reversed(candidates):
                    offset = i - pos
                    if offset > WINDOW_SIZE:
                        break
                    chain_count += 1
                    if chain_count > MAX_CHAIN:
                        break

                    # Quick filter on current best match length
                    if best_len >= MIN_MATCH:
                        check_idx = pos + best_len
                        target_idx = i + best_len
                        if target_idx >= n or check_idx >= i or data[check_idx] != data[target_idx]:
                            continue

                    # Determine common prefix length (supports overlapping matches)
                    match_len = 0
                    max_possible = min(MAX_MATCH, n - i)
                    while match_len < max_possible and data[pos + match_len] == data[i + match_len]:
                        match_len += 1

                    if match_len > best_len:
                        best_len = match_len
                        best_offset = offset
                        if match_len == MAX_MATCH:
                            break

        if best_len >= MIN_MATCH:
            tokens.append((1, best_offset, best_len))
            # Insert trigrams for the skipped positions (bounded to maintain speed)
            end = min(i + best_len, n - MIN_MATCH + 1)
            for j in range(i, min(i + 4, end)):
                p = data[j : j + MIN_MATCH]
                head.setdefault(p, []).append(j)
            i += best_len
        else:
            tokens.append((0, data[i]))
            if i + MIN_MATCH <= n:
                prefix = data[i : i + MIN_MATCH]
                head.setdefault(prefix, []).append(i)
            i += 1

        if len(tokens) >= 512:
            flush_tokens()

    flush_tokens()
    return bytes(out)


def decode(data: bytes) -> bytes:
    """Decompress LZ77 / LZSS encoded bytes.

    Args:
        data: Compressed byte stream.

    Returns:
        Exact reconstructed original bytes.

    Raises:
        TruncatedDataError: If stream ends prematurely.
        CorruptedDataError: If reference offset or length is invalid.
    """
    if not data:
        return b""

    if len(data) < 4:
        raise TruncatedDataError(f"LZ77 stream truncated: expected 4-byte header, got {len(data)}")

    original_length = struct.unpack(">I", data[0:4])[0]
    if original_length == 0:
        return b""

    out = bytearray()
    i = 4
    n = len(data)

    while len(out) < original_length:
        if i >= n:
            raise TruncatedDataError(
                f"LZ77 stream truncated: produced {len(out)} of {original_length} bytes"
            )

        flags = data[i]
        i += 1

        for bit_pos in range(8):
            if len(out) >= original_length:
                break

            is_match = (flags >> bit_pos) & 1

            if is_match:
                if i + 2 > n:
                    raise TruncatedDataError("LZ77 stream truncated: incomplete match reference")

                b0 = data[i]
                b1 = data[i + 1]
                i += 2

                offset_val = (b0 << 4) | (b1 >> 4)
                offset = offset_val + 1
                len_code = b1 & 0x0F

                if len_code < 15:
                    length = len_code + 3
                else:
                    if i >= n:
                        raise TruncatedDataError("LZ77 stream truncated: missing extended length byte")
                    extra = data[i]
                    i += 1
                    length = 18 + extra

                if offset > len(out) or offset <= 0:
                    raise CorruptedDataError(
                        f"Invalid LZ77 backward reference: offset={offset} exceeds output length {len(out)}"
                    )

                ref_start = len(out) - offset
                # Overlapping match reproduction: copy byte-by-byte
                for k in range(length):
                    out.append(out[ref_start + k])
            else:
                if i >= n:
                    raise TruncatedDataError("LZ77 stream truncated: missing literal byte")
                out.append(data[i])
                i += 1

    return bytes(out)


# Register LZ77 in global codec registry
register_codec(CODEC_LZ77, "lz77", encode, decode)
