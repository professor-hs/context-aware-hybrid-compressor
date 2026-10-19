"""Safe, Versioned Binary Container Format for Context-Aware Hybrid Compressor.

Binary Container Specification (CACZ v1):
=========================================
File Header (28 bytes, Network/Big-Endian):
- Bytes 0..3:   Magic identifier b'CACZ' (0x43, 0x41, 0x43, 0x5A)
- Bytes 4..5:   Format version (uint16, big-endian) -> Current: 1
- Bytes 6..7:   Flags (uint16, big-endian) -> bit 0: 0=Heuristic, 1=Oracle
- Bytes 8..11:  Default Chunk Size (uint32, big-endian)
- Bytes 12..15: Total Chunk Count (uint32, big-endian)
- Bytes 16..23: Original Uncompressed File Size (uint64, big-endian)
- Bytes 24..27: Header CRC32 Checksum (uint32, computed over bytes 0..23)

Chunk Record Framing (18 bytes prefix + variable payload):
- Bytes 0..3:   Chunk Index (uint32, big-endian, sequential 0..total_chunks - 1)
- Byte 4:       Codec Identifier (uint8) (0=RAW, 1=RLE, 2=HUFFMAN, 3=LZ77, 4=DELTA)
- Byte 5:       Flags / Reserved (uint8, 0x00)
- Bytes 6..9:   Original Chunk Length (uint32, big-endian)
- Bytes 10..13: Encoded Payload Length (uint32, big-endian)
- Bytes 14..17: Uncompressed Data CRC32 (uint32, big-endian)
- Bytes 18..N:  Encoded Payload Bytes (length = Encoded Payload Length)

Security & Integrity Invariants:
- Bit-for-bit exact reconstruction.
- Protection against unbounded memory allocation (enforces bounds on lengths).
- Verification of per-chunk CRC32, overall file length, and chunk sequence integrity.
- Detection of trailing garbage or premature stream termination.
- Atomic file writes using temporary staging to avoid corrupt partial outputs.
- Never uses pickle or insecure deserialization.
"""

import os
import struct
import tempfile
import uuid
import zlib
from dataclasses import dataclass
from typing import BinaryIO, Optional, Union

from core.codecs import (
    CODEC_NAMES,
    CODEC_RAW,
    CorruptedDataError,
    DecompressionError,
    InvalidHeaderError,
    TruncatedDataError,
    get_codec_by_id,
)
from core.chunker import DEFAULT_CHUNK_SIZE, MAX_CHUNK_SIZE, iter_chunks_from_file
from core.selector import ChunkSelectionResult, select_and_encode_chunk

CONTAINER_MAGIC = b"CACZ"
CONTAINER_VERSION = 1
FLAG_MODE_ORACLE = 0x0001
FLAG_MODE_HEURISTIC = 0x0000

FILE_HEADER_STRUCT = struct.Struct(">4sHHIIQI")
FILE_HEADER_SIZE = FILE_HEADER_STRUCT.size  # 28 bytes

CHUNK_RECORD_HEADER_STRUCT = struct.Struct(">IBBIII")
CHUNK_RECORD_HEADER_SIZE = CHUNK_RECORD_HEADER_STRUCT.size  # 18 bytes


@dataclass
class ContainerMetadata:
    """Header metadata describing a CACZ container."""

    version: int
    flags: int
    mode: str
    chunk_size: int
    total_chunks: int
    original_size: int
    compressed_size: int


@dataclass
class ChunkRecordInfo:
    """Inspection metadata for a single chunk record."""

    index: int
    codec_id: int
    codec_name: str
    original_length: int
    payload_length: int
    crc32: int


@dataclass
class ContainerInspection:
    """Detailed inspection report of a CACZ container."""

    metadata: ContainerMetadata
    chunks: list[ChunkRecordInfo]
    codec_counts: dict[str, int]
    codec_bytes: dict[str, int]


def compress_file(
    input_path: Union[str, os.PathLike],
    output_path: Union[str, os.PathLike],
    chunk_size: int = DEFAULT_CHUNK_SIZE,
    mode: str = "heuristic",
    evaluate_oracle: bool = False,
) -> tuple[ContainerMetadata, list[ChunkSelectionResult]]:
    """Compress an input file into a CACZ container.

    Args:
        input_path: Path to input file.
        output_path: Path to write compressed container.
        chunk_size: Fixed chunk size in bytes (1024..1048576).
        mode: Either 'heuristic' or 'oracle'.
        evaluate_oracle: Record oracle comparison metrics if heuristic mode.

    Returns:
        Tuple of (ContainerMetadata, list of ChunkSelectionResult).
    """
    abs_in = os.path.abspath(input_path)
    abs_out = os.path.abspath(output_path)
    if abs_in == abs_out:
        raise ValueError(f"Input and output paths cannot be identical: {abs_in}")

    if not os.path.exists(abs_in):
        raise FileNotFoundError(f"Input file not found: {abs_in}")

    original_size = os.path.getsize(abs_in)

    # First pass or generator collection to determine total chunks
    # Note: to preserve bounded memory, we stream chunks and write to temporary file
    flags = FLAG_MODE_ORACLE if mode == "oracle" else FLAG_MODE_HEURISTIC

    out_dir = os.path.dirname(abs_out) or "."
    temp_fd, temp_path = tempfile.mkstemp(prefix="cacz_comp_", dir=out_dir)
    os.close(temp_fd)

    selection_results: list[ChunkSelectionResult] = []
    total_chunks = 0

    try:
        with open(temp_path, "wb") as out_f:
            # Leave placeholder for 28-byte header
            out_f.seek(FILE_HEADER_SIZE)

            for chunk_idx, chunk_bytes in iter_chunks_from_file(abs_in, chunk_size=chunk_size):
                total_chunks += 1
                orig_len = len(chunk_bytes)
                orig_crc = zlib.crc32(chunk_bytes) & 0xFFFFFFFF

                res = select_and_encode_chunk(
                    chunk_index=chunk_idx,
                    data=chunk_bytes,
                    mode=mode,
                    evaluate_oracle=evaluate_oracle,
                )
                selection_results.append(res)

                payload_len = len(res.selected_payload)

                # Write chunk record header (18 bytes)
                rec_hdr = CHUNK_RECORD_HEADER_STRUCT.pack(
                    chunk_idx,
                    res.selected_codec,
                    0,  # reserved flags
                    orig_len,
                    payload_len,
                    orig_crc,
                )
                out_f.write(rec_hdr)
                out_f.write(res.selected_payload)

            # Seek back to write complete file header
            out_f.seek(0)
            raw_header_part = struct.pack(
                ">4sHHIIQ",
                CONTAINER_MAGIC,
                CONTAINER_VERSION,
                flags,
                chunk_size,
                total_chunks,
                original_size,
            )
            header_crc = zlib.crc32(raw_header_part) & 0xFFFFFFFF
            complete_header = raw_header_part + struct.pack(">I", header_crc)
            out_f.write(complete_header)

        # Atomic rename
        if os.path.exists(abs_out):
            os.replace(temp_path, abs_out)
        else:
            os.rename(temp_path, abs_out)

    except Exception:
        if os.path.exists(temp_path):
            try:
                os.remove(temp_path)
            except OSError:
                pass
        raise

    compressed_size = os.path.getsize(abs_out)
    metadata = ContainerMetadata(
        version=CONTAINER_VERSION,
        flags=flags,
        mode=mode,
        chunk_size=chunk_size,
        total_chunks=total_chunks,
        original_size=original_size,
        compressed_size=compressed_size,
    )
    return metadata, selection_results


def decompress_file(
    input_path: Union[str, os.PathLike],
    output_path: Union[str, os.PathLike],
) -> ContainerMetadata:
    """Decompress a CACZ container to output path with atomic file safety.

    Args:
        input_path: Path to CACZ compressed file.
        output_path: Destination path for uncompressed output.

    Returns:
        ContainerMetadata from decompressed container.

    Raises:
        InvalidHeaderError: If magic or header checksum is invalid.
        CorruptedDataError: If container or payload is corrupted.
        TruncatedDataError: If stream ends prematurely.
    """
    abs_in = os.path.abspath(input_path)
    abs_out = os.path.abspath(output_path)
    if abs_in == abs_out:
        raise ValueError(f"Input and output paths cannot be identical: {abs_in}")

    if not os.path.exists(abs_in):
        raise FileNotFoundError(f"Container file not found: {abs_in}")

    container_size = os.path.getsize(abs_in)
    if container_size < FILE_HEADER_SIZE:
        raise TruncatedDataError(
            f"Container file too small: size {container_size} < header size {FILE_HEADER_SIZE}"
        )

    out_dir = os.path.dirname(abs_out) or "."
    temp_fd, temp_path = tempfile.mkstemp(prefix="cacz_dec_", dir=out_dir)
    os.close(temp_fd)

    try:
        with open(abs_in, "rb") as in_f:
            header_bytes = in_f.read(FILE_HEADER_SIZE)
            if len(header_bytes) < FILE_HEADER_SIZE:
                raise TruncatedDataError("Truncated container header")

            magic, version, flags, chunk_size, total_chunks, original_size, header_crc = (
                FILE_HEADER_STRUCT.unpack(header_bytes)
            )

            if magic != CONTAINER_MAGIC:
                raise InvalidHeaderError(
                    f"Invalid container magic: expected {CONTAINER_MAGIC!r}, got {magic!r}"
                )

            if version != CONTAINER_VERSION:
                raise InvalidHeaderError(
                    f"Unsupported container version: {version} (supported: {CONTAINER_VERSION})"
                )

            # Validate header CRC32
            computed_header_crc = zlib.crc32(header_bytes[:24]) & 0xFFFFFFFF
            if computed_header_crc != header_crc:
                raise CorruptedDataError(
                    f"Header checksum mismatch: expected 0x{header_crc:08X}, computed 0x{computed_header_crc:08X}"
                )

            mode = "oracle" if (flags & FLAG_MODE_ORACLE) else "heuristic"

            total_decompressed_bytes = 0
            chunks_processed = 0

            with open(temp_path, "wb") as out_f:
                for expected_idx in range(total_chunks):
                    rec_bytes = in_f.read(CHUNK_RECORD_HEADER_SIZE)
                    if not rec_bytes:
                        raise TruncatedDataError(
                            f"Premature EOF: expected chunk {expected_idx} of {total_chunks}"
                        )
                    if len(rec_bytes) < CHUNK_RECORD_HEADER_SIZE:
                        raise TruncatedDataError(
                            f"Truncated chunk header for chunk {expected_idx}"
                        )

                    (
                        chunk_idx,
                        codec_id,
                        reserved,
                        orig_len,
                        payload_len,
                        expected_crc,
                    ) = CHUNK_RECORD_HEADER_STRUCT.unpack(rec_bytes)

                    if chunk_idx != expected_idx:
                        raise CorruptedDataError(
                            f"Out of order chunk: expected index {expected_idx}, got {chunk_idx}"
                        )

                    # Bounded memory security check
                    if orig_len > MAX_CHUNK_SIZE * 2:
                        raise CorruptedDataError(
                            f"Dangerous uncompressed chunk length: {orig_len} bytes"
                        )
                    if payload_len > MAX_CHUNK_SIZE * 2:
                        raise CorruptedDataError(
                            f"Dangerous encoded payload length: {payload_len} bytes"
                        )

                    payload = in_f.read(payload_len)
                    if len(payload) < payload_len:
                        raise TruncatedDataError(
                            f"Truncated payload for chunk {expected_idx}: expected {payload_len}B, got {len(payload)}B"
                        )

                    # Decode chunk
                    try:
                        codec_entry = get_codec_by_id(codec_id)
                    except KeyError:
                        raise CorruptedDataError(f"Invalid codec ID in chunk {chunk_idx}: 0x{codec_id:02X}")

                    decompressed_chunk = codec_entry.decode(payload)

                    if len(decompressed_chunk) != orig_len:
                        raise CorruptedDataError(
                            f"Decoded size mismatch in chunk {chunk_idx}: expected {orig_len}B, got {len(decompressed_chunk)}B"
                        )

                    computed_crc = zlib.crc32(decompressed_chunk) & 0xFFFFFFFF
                    if computed_crc != expected_crc:
                        raise CorruptedDataError(
                            f"CRC32 mismatch in chunk {chunk_idx}: expected 0x{expected_crc:08X}, got 0x{computed_crc:08X}"
                        )

                    out_f.write(decompressed_chunk)
                    total_decompressed_bytes += orig_len
                    chunks_processed += 1

            # Validate total bytes and chunks
            if chunks_processed != total_chunks:
                raise CorruptedDataError(
                    f"Chunk count mismatch: expected {total_chunks}, processed {chunks_processed}"
                )

            if total_decompressed_bytes != original_size:
                raise CorruptedDataError(
                    f"Total uncompressed size mismatch: expected {original_size}B, got {total_decompressed_bytes}B"
                )

            # Check for trailing garbage
            trailing = in_f.read(1)
            if trailing:
                raise CorruptedDataError("Unexpected trailing bytes detected at end of container file")

        # Atomic replacement of output
        if os.path.exists(abs_out):
            os.replace(temp_path, abs_out)
        else:
            os.rename(temp_path, abs_out)

    except Exception:
        if os.path.exists(temp_path):
            try:
                os.remove(temp_path)
            except OSError:
                pass
        raise

    metadata = ContainerMetadata(
        version=version,
        flags=flags,
        mode=mode,
        chunk_size=chunk_size,
        total_chunks=total_chunks,
        original_size=original_size,
        compressed_size=container_size,
    )
    return metadata


def inspect_container(file_path: Union[str, os.PathLike]) -> ContainerInspection:
    """Inspect container headers and chunk table without decompressing data.

    Args:
        file_path: Path to CACZ compressed file.

    Returns:
        ContainerInspection report.
    """
    abs_path = os.path.abspath(file_path)
    file_size = os.path.getsize(abs_path)

    with open(abs_path, "rb") as f:
        header_bytes = f.read(FILE_HEADER_SIZE)
        if len(header_bytes) < FILE_HEADER_SIZE:
            raise TruncatedDataError("File smaller than container header")

        magic, version, flags, chunk_size, total_chunks, original_size, header_crc = (
            FILE_HEADER_STRUCT.unpack(header_bytes)
        )

        if magic != CONTAINER_MAGIC:
            raise InvalidHeaderError(f"Invalid magic: {magic!r}")

        computed_crc = zlib.crc32(header_bytes[:24]) & 0xFFFFFFFF
        if computed_crc != header_crc:
            raise CorruptedDataError("Header CRC32 mismatch")

        mode = "oracle" if (flags & FLAG_MODE_ORACLE) else "heuristic"
        metadata = ContainerMetadata(
            version=version,
            flags=flags,
            mode=mode,
            chunk_size=chunk_size,
            total_chunks=total_chunks,
            original_size=original_size,
            compressed_size=file_size,
        )

        chunks: list[ChunkRecordInfo] = []
        codec_counts: dict[str, int] = {name: 0 for name in CODEC_NAMES.values()}
        codec_bytes: dict[str, int] = {name: 0 for name in CODEC_NAMES.values()}

        for _ in range(total_chunks):
            rec_bytes = f.read(CHUNK_RECORD_HEADER_SIZE)
            if len(rec_bytes) < CHUNK_RECORD_HEADER_SIZE:
                raise TruncatedDataError("Truncated chunk record")

            (
                chunk_idx,
                codec_id,
                reserved,
                orig_len,
                payload_len,
                crc32_val,
            ) = CHUNK_RECORD_HEADER_STRUCT.unpack(rec_bytes)

            cname = CODEC_NAMES.get(codec_id, f"unknown(0x{codec_id:02X})")
            chunks.append(
                ChunkRecordInfo(
                    index=chunk_idx,
                    codec_id=codec_id,
                    codec_name=cname,
                    original_length=orig_len,
                    payload_length=payload_len,
                    crc32=crc32_val,
                )
            )
            if cname in codec_counts:
                codec_counts[cname] += 1
                codec_bytes[cname] += payload_len

            # Seek past payload
            f.seek(payload_len, os.SEEK_CUR)

    return ContainerInspection(
        metadata=metadata,
        chunks=chunks,
        codec_counts=codec_counts,
        codec_bytes=codec_bytes,
    )
