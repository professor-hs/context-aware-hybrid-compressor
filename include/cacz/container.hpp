#pragma once

#include "cacz/common.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>

namespace cacz {

struct ContainerHeader {
    uint16_t version{FORMAT_VERSION_1};
    uint16_t flags{FLAG_MODE_HEURISTIC};
    uint32_t chunk_size{static_cast<uint32_t>(DEFAULT_CHUNK_SIZE)};
    uint32_t total_chunks{0};
    uint64_t original_size{0};
    uint32_t header_crc{0};
};

struct ChunkHeader {
    uint32_t index{0};
    CodecId codec_id{CodecId::Raw};
    uint8_t reserved{0};
    uint32_t original_length{0};
    uint32_t payload_length{0};
    uint32_t uncompressed_crc{0};
};

struct ChunkInspection {
    uint32_t index{0};
    CodecId codec_id{CodecId::Raw};
    std::string codec_name;
    uint32_t original_length{0};
    uint32_t payload_length{0};
    uint32_t uncompressed_crc{0};
    double compression_ratio{1.0};
};

struct ArchiveInspection {
    ContainerHeader header;
    std::string mode_name;
    uint64_t archive_size{0};
    double overall_ratio{1.0};
    std::vector<ChunkInspection> chunks;
    std::unordered_map<std::string, size_t> codec_chunk_counts;
    std::unordered_map<std::string, size_t> codec_uncompressed_bytes;
    std::unordered_map<std::string, size_t> codec_compressed_bytes;
};

struct CompressionOptions {
    uint16_t version{FORMAT_VERSION_1};
    uint16_t mode_flags{FLAG_MODE_HEURISTIC};
    size_t chunk_size{DEFAULT_CHUNK_SIZE};
    bool oracle_search{false};
    bool ai_orchestration{false};
    std::function<void(size_t chunk_idx, size_t total_chunks, CodecId selected_codec)> progress_callback;
};

class Container {
public:
    static void compress_file(const std::filesystem::path& input_path,
                              const std::filesystem::path& output_path,
                              const CompressionOptions& options = {});

    static void decompress_file(const std::filesystem::path& input_path,
                                const std::filesystem::path& output_path);

    static ArchiveInspection inspect(const std::filesystem::path& archive_path);

    static bool verify(const std::filesystem::path& archive_path, std::string* error_msg = nullptr);
};

} // namespace cacz
