"""Core package for Context-Aware Hybrid Compressor.

Exports streaming chunker, feature analyzer, heuristic/oracle selector,
and binary container engine.
"""

from core.analyzer import ChunkFeatures, analyze_chunk
from core.chunker import (
    DEFAULT_CHUNK_SIZE,
    MAX_CHUNK_SIZE,
    MIN_CHUNK_SIZE,
    iter_chunks_from_file,
    iter_chunks_from_stream,
    validate_chunk_size,
)
from core.container import (
    ContainerInspection,
    ContainerMetadata,
    compress_file,
    decompress_file,
    inspect_container,
)
from core.selector import (
    ChunkSelectionResult,
    predict_codec_heuristic,
    select_and_encode_chunk,
)

__all__ = [
    "ChunkFeatures",
    "analyze_chunk",
    "DEFAULT_CHUNK_SIZE",
    "MIN_CHUNK_SIZE",
    "MAX_CHUNK_SIZE",
    "iter_chunks_from_file",
    "iter_chunks_from_stream",
    "validate_chunk_size",
    "ContainerMetadata",
    "ContainerInspection",
    "compress_file",
    "decompress_file",
    "inspect_container",
    "ChunkSelectionResult",
    "predict_codec_heuristic",
    "select_and_encode_chunk",
]
