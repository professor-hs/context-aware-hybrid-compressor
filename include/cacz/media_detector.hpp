#pragma once

#include <string>
#include <vector>
#include <span>
#include <cstdint>
#include <optional>
#include <filesystem>

namespace cacz {

enum class MediaType {
    General,
    Text,
    SourceCode,
    StructuredData, // JSON, CSV, XML
    Image,
    Audio,
    Video,
    Archive         // ZIP, GZ, 7Z
};

struct MediaMetadata {
    MediaType type{MediaType::General};
    std::string mime_type{"application/octet-stream"};
    std::string format_name{"unknown"};
    bool is_already_compressed{false};

    // Image attributes (optional)
    uint32_t width{0};
    uint32_t height{0};
    uint8_t channels{0};
    uint8_t bit_depth{0};

    // Audio attributes (optional)
    uint32_t sample_rate{0};
    uint8_t audio_channels{0};
    uint8_t bits_per_sample{0};

    // Video/Container
    std::string container_format;

    // Detailed description
    std::string summary;
};

class MediaDetector {
public:
    static MediaMetadata detect(std::span<const uint8_t> header_bytes,
                                const std::filesystem::path& file_path = {});

    static bool is_image(const MediaMetadata& meta) noexcept { return meta.type == MediaType::Image; }
    static bool is_audio(const MediaMetadata& meta) noexcept { return meta.type == MediaType::Audio; }
    static bool is_video(const MediaMetadata& meta) noexcept { return meta.type == MediaType::Video; }
};

} // namespace cacz
