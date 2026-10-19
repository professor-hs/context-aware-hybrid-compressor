"""Codec package for Context-Aware Hybrid Compressor.

This package defines the stable codec identifiers, exception hierarchy,
and registry for all compression algorithms implemented in the project.
"""

from typing import Callable, NamedTuple


class CompressionError(Exception):
    """Base exception for all compression and decompression errors."""
    pass


class DecompressionError(CompressionError):
    """Raised when decompression fails due to invalid, corrupt, or truncated data."""
    pass


class CorruptedDataError(DecompressionError):
    """Raised when data integrity checks fail or payload is corrupted."""
    pass


class TruncatedDataError(DecompressionError):
    """Raised when input stream ends prematurely before expected payload completes."""
    pass


class InvalidHeaderError(CompressionError):
    """Raised when container or codec header contains invalid magic or values."""
    pass


# Stable numeric codec identifiers (uint8)
CODEC_RAW: int = 0x00
CODEC_RLE: int = 0x01
CODEC_HUFFMAN: int = 0x02
CODEC_LZ77: int = 0x03
CODEC_DELTA: int = 0x04

CODEC_NAMES: dict[int, str] = {
    CODEC_RAW: "raw",
    CODEC_RLE: "rle",
    CODEC_HUFFMAN: "huffman",
    CODEC_LZ77: "lz77",
    CODEC_DELTA: "delta",
}

NAME_TO_CODEC: dict[str, int] = {name: cid for cid, name in CODEC_NAMES.items()}


class CodecEntry(NamedTuple):
    id: int
    name: str
    encode: Callable[[bytes], bytes]
    decode: Callable[[bytes], bytes]


# Registry populated lazily or upon submodule import
_REGISTRY: dict[int, CodecEntry] = {}


def register_codec(codec_id: int, name: str, encode_fn: Callable[[bytes], bytes], decode_fn: Callable[[bytes], bytes]) -> None:
    """Register a codec with its ID, name, and encode/decode entrypoints."""
    _REGISTRY[codec_id] = CodecEntry(id=codec_id, name=name, encode=encode_fn, decode=decode_fn)


def get_codec_by_id(codec_id: int) -> CodecEntry:
    """Retrieve codec entry by numeric ID. Raises KeyError if unknown."""
    if codec_id not in _REGISTRY:
        raise KeyError(f"Unknown codec ID: 0x{codec_id:02X}")
    return _REGISTRY[codec_id]


def get_codec_by_name(name: str) -> CodecEntry:
    """Retrieve codec entry by string name. Raises KeyError if unknown."""
    lower_name = name.lower()
    if lower_name not in NAME_TO_CODEC:
        raise KeyError(f"Unknown codec name: {name}")
    return _REGISTRY[NAME_TO_CODEC[lower_name]]


# Raw fallback codec
def raw_encode(data: bytes) -> bytes:
    """Identity pass-through for uncompressed fallback."""
    return data


def raw_decode(data: bytes) -> bytes:
    """Identity pass-through for uncompressed fallback."""
    return data


register_codec(CODEC_RAW, "raw", raw_encode, raw_decode)

# Auto-register all sub-codecs
from . import rle as _rle  # noqa: E402
from . import huffman as _huffman  # noqa: E402
from . import lz77 as _lz77  # noqa: E402
from . import delta as _delta  # noqa: E402
