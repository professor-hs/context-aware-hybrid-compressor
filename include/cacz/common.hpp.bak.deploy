#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <stdexcept>
#include <vector>
#include <span>
#include <array>
#include <bit>

namespace cacz {

// --- Error Hierarchies ---
class CompressionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DecompressionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class InvalidHeaderError : public DecompressionError {
public:
    using DecompressionError::DecompressionError;
};

class CorruptedDataError : public DecompressionError {
public:
    using DecompressionError::DecompressionError;
};

class TruncatedDataError : public DecompressionError {
public:
    using DecompressionError::DecompressionError;
};

class UnsupportedCodecError : public DecompressionError {
public:
    using DecompressionError::DecompressionError;
};

// --- Container Format Constants ---
inline constexpr uint8_t MAGIC[4] = {'C', 'A', 'C', 'Z'};
inline constexpr uint16_t FORMAT_VERSION_1 = 1;
inline constexpr uint16_t FORMAT_VERSION_2 = 2;

inline constexpr uint16_t FLAG_MODE_HEURISTIC = 0x0000;
inline constexpr uint16_t FLAG_MODE_ORACLE    = 0x0001;
inline constexpr uint16_t FLAG_MODE_AI        = 0x0002;

inline constexpr size_t FILE_HEADER_SIZE_V1 = 28;
inline constexpr size_t CHUNK_RECORD_HEADER_SIZE = 18;

inline constexpr size_t DEFAULT_CHUNK_SIZE = 8192;
inline constexpr size_t MIN_CHUNK_SIZE     = 1024;
inline constexpr size_t MAX_CHUNK_SIZE     = 1048576; // 1 MB

// --- Codec Identifiers ---
enum class CodecId : uint8_t {
    Raw     = 0x00,
    Rle     = 0x01,
    Huffman = 0x02,
    Lz77    = 0x03,
    Delta   = 0x04,
    Deflate = 0x05,
    Zstd    = 0x06
};

inline const char* codec_id_to_name(CodecId id) noexcept {
    switch (id) {
        case CodecId::Raw:     return "raw";
        case CodecId::Rle:     return "rle";
        case CodecId::Huffman: return "huffman";
        case CodecId::Lz77:    return "lz77";
        case CodecId::Delta:   return "delta";
        case CodecId::Deflate: return "deflate";
        case CodecId::Zstd:    return "zstd";
        default:               return "unknown";
    }
}

inline CodecId codec_name_to_id(std::string_view name) {
    if (name == "raw")     return CodecId::Raw;
    if (name == "rle")     return CodecId::Rle;
    if (name == "huffman") return CodecId::Huffman;
    if (name == "lz77")    return CodecId::Lz77;
    if (name == "delta")   return CodecId::Delta;
    if (name == "deflate") return CodecId::Deflate;
    if (name == "zstd")    return CodecId::Zstd;
    throw UnsupportedCodecError("Unknown codec name: " + std::string(name));
}

// --- Endian Helpers (Big-Endian / Network Order) ---
inline uint16_t read_be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

inline uint32_t read_be32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

inline uint64_t read_be64(const uint8_t* p) noexcept {
    return (static_cast<uint64_t>(p[0]) << 56) |
           (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) |
           (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) |
           (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8)  |
           (static_cast<uint64_t>(p[7]));
}

inline void write_be16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void write_be32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

inline void write_be64(uint8_t* p, uint64_t v) noexcept {
    p[0] = static_cast<uint8_t>((v >> 56) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 48) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 40) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 32) & 0xFF);
    p[4] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[5] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[6] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[7] = static_cast<uint8_t>(v & 0xFF);
}

// --- Exact CRC32 Checksum (Standard IEEE 802.3, matching Python zlib.crc32) ---
inline constexpr std::array<uint32_t, 256> make_crc32_table() noexcept {
    std::array<uint32_t, 256> tbl{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (size_t k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        tbl[i] = c;
    }
    return tbl;
}

inline constexpr std::array<uint32_t, 256> CRC32_TABLE = make_crc32_table();

class Crc32 {
public:
    static uint32_t calculate(std::span<const uint8_t> data, uint32_t seed = 0) noexcept {
        uint32_t crc = ~seed;
        for (uint8_t byte : data) {
            crc = CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }
};

} // namespace cacz
