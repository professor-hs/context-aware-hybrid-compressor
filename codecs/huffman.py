"""Lossless Huffman Coding Codec.

This module implements deterministic, canonical-quality Huffman coding for
arbitrary binary payloads (all 256 byte values).

Key Engineering Features:
- Exact frequency counting and deterministic priority-queue tree construction
  with tie-breaking on symbol values to ensure reproducible output across platforms.
- Pre-order serialized tree representation embedded in the bitstream header.
- Graceful handling of empty payloads and single-symbol degenerate trees.
- Bit-level packing and unpacking with safe handling of incomplete final bytes.
- Rigorous bounds checking and validation to detect truncation and malformed bitstreams.

Header Format:
- If input is empty: 0 bytes (empty output).
- Bytes 0..3: Original uncompressed size (uint32, big-endian).
- Byte 4: Unique symbol count (0 = 1 symbol, ..., 255 = 256 symbols).
- Followed by tree serialization (pre-order bits) and packed symbol bits.
"""

import heapq
import struct
from collections import Counter
from typing import Optional, Union
from . import (
    CODEC_HUFFMAN,
    CorruptedDataError,
    TruncatedDataError,
    register_codec,
)


class _HuffmanNode:
    """Internal node or leaf in the Huffman tree."""

    __slots__ = ("freq", "min_symbol", "symbol", "left", "right")

    def __init__(
        self,
        freq: int,
        min_symbol: int,
        symbol: Optional[int] = None,
        left: Optional["_HuffmanNode"] = None,
        right: Optional["_HuffmanNode"] = None,
    ):
        self.freq = freq
        self.min_symbol = min_symbol
        self.symbol = symbol  # None for internal nodes, 0..255 for leaves
        self.left = left
        self.right = right

    @property
    def is_leaf(self) -> bool:
        return self.symbol is not None


class _BitWriter:
    """Helper for packing variable-length bit sequences into bytes."""

    __slots__ = ("buffer", "current_byte", "bit_count")

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.current_byte = 0
        self.bit_count = 0

    def write_bit(self, bit: int) -> None:
        self.current_byte = (self.current_byte << 1) | (bit & 1)
        self.bit_count += 1
        if self.bit_count == 8:
            self.buffer.append(self.current_byte)
            self.current_byte = 0
            self.bit_count = 0

    def write_bits(self, value: int, num_bits: int) -> None:
        for i in range(num_bits - 1, -1, -1):
            self.write_bit((value >> i) & 1)

    def write_byte(self, byte_val: int) -> None:
        self.write_bits(byte_val, 8)

    def flush(self) -> bytes:
        if self.bit_count > 0:
            self.buffer.append(self.current_byte << (8 - self.bit_count))
            self.current_byte = 0
            self.bit_count = 0
        return bytes(self.buffer)


class _BitReader:
    """Helper for reading variable-length bit sequences from bytes."""

    __slots__ = ("data", "total_bytes", "byte_pos", "bit_pos")

    def __init__(self, data: bytes, byte_offset: int = 0) -> None:
        self.data = data
        self.total_bytes = len(data)
        self.byte_pos = byte_offset
        self.bit_pos = 0  # 0 to 7, where 0 is MSB

    def read_bit(self) -> int:
        if self.byte_pos >= self.total_bytes:
            raise TruncatedDataError("Unexpected EOF while reading bitstream")
        val = (self.data[self.byte_pos] >> (7 - self.bit_pos)) & 1
        self.bit_pos += 1
        if self.bit_pos == 8:
            self.bit_pos = 0
            self.byte_pos += 1
        return val

    def read_byte(self) -> int:
        val = 0
        for _ in range(8):
            val = (val << 1) | self.read_bit()
        return val


def _build_huffman_tree(frequencies: Counter[int]) -> _HuffmanNode:
    """Construct deterministic Huffman tree using min-heap with deterministic tie-breakers."""
    heap: list[tuple[int, int, int, _HuffmanNode]] = []
    tie_breaker = 0

    for symbol, freq in frequencies.items():
        node = _HuffmanNode(freq=freq, min_symbol=symbol, symbol=symbol)
        heapq.heappush(heap, (freq, symbol, tie_breaker, node))
        tie_breaker += 1

    while len(heap) > 1:
        freq1, min1, _, left = heapq.heappop(heap)
        freq2, min2, _, right = heapq.heappop(heap)

        # Enforce canonical ordering between children
        if min1 > min2:
            left, right = right, left
            min1, min2 = min2, min1

        parent = _HuffmanNode(
            freq=freq1 + freq2,
            min_symbol=min1,
            symbol=None,
            left=left,
            right=right,
        )
        tie_breaker += 1
        heapq.heappush(heap, (parent.freq, parent.min_symbol, tie_breaker, parent))

    return heap[0][3]


def _generate_codes(
    node: _HuffmanNode,
    prefix_val: int = 0,
    prefix_len: int = 0,
    codes: Optional[dict[int, tuple[int, int]]] = None,
) -> dict[int, tuple[int, int]]:
    """Traverse tree to build dictionary of symbol -> (code_value, bit_length)."""
    if codes is None:
        codes = {}

    if node.is_leaf:
        assert node.symbol is not None
        codes[node.symbol] = (prefix_val, max(1, prefix_len))
    else:
        assert node.left is not None and node.right is not None
        _generate_codes(node.left, (prefix_val << 1) | 0, prefix_len + 1, codes)
        _generate_codes(node.right, (prefix_val << 1) | 1, prefix_len + 1, codes)

    return codes


def _serialize_tree(node: _HuffmanNode, writer: _BitWriter) -> None:
    """Pre-order bitwise serialization of the Huffman tree.

    1 bit: 1 = Leaf (followed by 8 bits of symbol).
    1 bit: 0 = Internal node (followed by left child then right child).
    """
    if node.is_leaf:
        writer.write_bit(1)
        assert node.symbol is not None
        writer.write_byte(node.symbol)
    else:
        writer.write_bit(0)
        assert node.left is not None and node.right is not None
        _serialize_tree(node.left, writer)
        _serialize_tree(node.right, writer)


def _deserialize_tree(reader: _BitReader) -> _HuffmanNode:
    """Reconstruct Huffman tree from pre-order serialized bits."""
    bit = reader.read_bit()
    if bit == 1:
        symbol = reader.read_byte()
        return _HuffmanNode(freq=0, min_symbol=symbol, symbol=symbol)
    else:
        left = _deserialize_tree(reader)
        right = _deserialize_tree(reader)
        return _HuffmanNode(
            freq=0,
            min_symbol=min(left.min_symbol, right.min_symbol),
            symbol=None,
            left=left,
            right=right,
        )


def encode(data: bytes) -> bytes:
    """Encode binary data using deterministic lossless Huffman coding.

    Args:
        data: Arbitrary input bytes.

    Returns:
        Encoded bytes including header metadata and packed payload.
    """
    if not data:
        return b""

    original_length = len(data)
    freqs = Counter(data)
    num_symbols = len(freqs)

    # 4 bytes original length
    header = bytearray(struct.pack(">I", original_length))

    # Single unique symbol optimization
    if num_symbols == 1:
        symbol = next(iter(freqs))
        header.append(0)  # num_symbols - 1 = 0 (means 1 unique symbol)
        header.append(symbol)
        return bytes(header)

    # 1 byte for num_symbols - 1 (1..255 representing 2..256)
    header.append(num_symbols - 1)

    tree = _build_huffman_tree(freqs)
    codes = _generate_codes(tree)

    bit_writer = _BitWriter()
    _serialize_tree(tree, bit_writer)

    for b in data:
        code_val, code_len = codes[b]
        bit_writer.write_bits(code_val, code_len)

    payload = bit_writer.flush()
    header.extend(payload)
    return bytes(header)


def decode(data: bytes) -> bytes:
    """Decode Huffman-encoded binary payload.

    Args:
        data: Huffman-encoded bytes.

    Returns:
        Exact reconstructed original bytes.

    Raises:
        TruncatedDataError: If the stream terminates prematurely.
        CorruptedDataError: If tree serialization or payload bits are invalid.
    """
    if not data:
        return b""

    if len(data) < 5:
        raise TruncatedDataError(f"Huffman stream too short: expected at least 5 bytes header, got {len(data)}")

    original_length = struct.unpack(">I", data[0:4])[0]
    if original_length == 0:
        return b""

    num_symbols = data[4] + 1

    # Single symbol case
    if num_symbols == 1:
        if len(data) < 6:
            raise TruncatedDataError("Single-symbol Huffman stream truncated: missing symbol byte")
        symbol = data[5]
        return bytes([symbol]) * original_length

    # Multi-symbol case: read serialized tree then walk bitstream
    reader = _BitReader(data, byte_offset=5)
    try:
        tree = _deserialize_tree(reader)
    except TruncatedDataError:
        raise
    except Exception as e:
        raise CorruptedDataError(f"Malformed Huffman tree in payload: {e}") from e

    out = bytearray(original_length)
    curr = tree
    emitted = 0

    while emitted < original_length:
        bit = reader.read_bit()
        curr = curr.right if bit else curr.left
        if curr is None:
            raise CorruptedDataError("Corrupted Huffman bitstream traversed into nonexistent node")
        if curr.is_leaf:
            assert curr.symbol is not None
            out[emitted] = curr.symbol
            emitted += 1
            curr = tree

    return bytes(out)


# Register Huffman in global codec registry
register_codec(CODEC_HUFFMAN, "huffman", encode, decode)
