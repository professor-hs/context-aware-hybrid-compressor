#include "cacz/media_detector.hpp"
#include <algorithm>
#include <cstring>

namespace cacz {

namespace {

bool starts_with(std::span<const uint8_t> data, const std::vector<uint8_t>& prefix) {
    if (data.size() < prefix.size()) return false;
    return std::memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

uint32_t read_be32(const uint8_t* p) {
    return static_cast<uint32_t>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

uint16_t read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

} // namespace

MediaMetadata MediaDetector::detect(std::span<const uint8_t> header,
                                    const std::filesystem::path& file_path) {
    MediaMetadata meta;
    std::string ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // 1. PNG Image
    if (starts_with(header, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A})) {
        meta.type = MediaType::Image;
        meta.mime_type = "image/png";
        meta.format_name = "PNG";
        meta.is_already_compressed = true;
        if (header.size() >= 24) {
            meta.width = read_be32(&header[16]);
            meta.height = read_be32(&header[20]);
            meta.bit_depth = header.size() > 24 ? header[24] : 8;
            meta.channels = (header.size() > 25 && header[25] == 6) ? 4 : 3;
        }
        meta.summary = "PNG Image (" + std::to_string(meta.width) + "x" + std::to_string(meta.height) + ", deflate compressed)";
        return meta;
    }

    // 2. JPEG Image
    if (starts_with(header, {0xFF, 0xD8, 0xFF})) {
        meta.type = MediaType::Image;
        meta.mime_type = "image/jpeg";
        meta.format_name = "JPEG";
        meta.is_already_compressed = true;
        // Search SOF0 marker for dimensions
        for (size_t i = 2; i + 9 < header.size(); ++i) {
            if (header[i] == 0xFF && (header[i + 1] == 0xC0 || header[i + 1] == 0xC2)) {
                meta.bit_depth = header[i + 4];
                meta.height = read_be16(&header[i + 5]);
                meta.width = read_be16(&header[i + 7]);
                meta.channels = header[i + 9];
                break;
            }
        }
        meta.summary = "JPEG Image (" + std::to_string(meta.width) + "x" + std::to_string(meta.height) + ", DCT lossy compressed)";
        return meta;
    }

    // 3. WebP Image
    if (header.size() >= 12 && starts_with(header, {'R', 'I', 'F', 'F'}) &&
        header[8] == 'W' && header[9] == 'E' && header[10] == 'B' && header[11] == 'P') {
        meta.type = MediaType::Image;
        meta.mime_type = "image/webp";
        meta.format_name = "WebP";
        meta.is_already_compressed = true;
        meta.summary = "WebP Image (already compressed container)";
        return meta;
    }

    // 4. BMP Image (Uncompressed Bitmap)
    if (starts_with(header, {'B', 'M'})) {
        meta.type = MediaType::Image;
        meta.mime_type = "image/bmp";
        meta.format_name = "BMP";
        meta.is_already_compressed = false; // High compression opportunity!
        if (header.size() >= 28) {
            meta.width = read_le32(&header[18]);
            meta.height = read_le32(&header[22]);
            meta.bit_depth = read_le16(&header[28]);
            meta.channels = (meta.bit_depth == 32) ? 4 : (meta.bit_depth == 24 ? 3 : 1);
        }
        meta.summary = "Uncompressed BMP Image (" + std::to_string(meta.width) + "x" + std::to_string(meta.height) + ", " + std::to_string(meta.bit_depth) + " bpp)";
        return meta;
    }

    // 5. WAV Audio (Uncompressed PCM or ADPCM)
    if (header.size() >= 12 && starts_with(header, {'R', 'I', 'F', 'F'}) &&
        header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E') {
        meta.type = MediaType::Audio;
        meta.mime_type = "audio/wav";
        meta.format_name = "WAV";
        meta.is_already_compressed = false; // PCM audio compresses very well with Delta / RLE
        // Inspect fmt subchunk
        for (size_t i = 12; i + 16 < header.size(); ++i) {
            if (header[i] == 'f' && header[i + 1] == 'm' && header[i + 2] == 't' && header[i + 3] == ' ') {
                meta.audio_channels = header[i + 10];
                meta.sample_rate = read_le32(&header[i + 12]);
                if (i + 22 < header.size()) {
                    meta.bits_per_sample = header[i + 22];
                }
                break;
            }
        }
        meta.summary = "WAV PCM Audio (" + std::to_string(meta.sample_rate) + " Hz, " +
                       std::to_string(meta.audio_channels) + " ch, " + std::to_string(meta.bits_per_sample) + " bit)";
        return meta;
    }

    // 6. MP3 Audio
    if (starts_with(header, {'I', 'D', '3'}) ||
        (header.size() >= 2 && header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)) {
        meta.type = MediaType::Audio;
        meta.mime_type = "audio/mpeg";
        meta.format_name = "MP3";
        meta.is_already_compressed = true;
        meta.summary = "MP3 Audio (MPEG Layer 3 perceptual lossy compressed)";
        return meta;
    }

    // 7. FLAC Audio
    if (starts_with(header, {'f', 'L', 'a', 'C'})) {
        meta.type = MediaType::Audio;
        meta.mime_type = "audio/flac";
        meta.format_name = "FLAC";
        meta.is_already_compressed = true;
        meta.summary = "FLAC Lossless Compressed Audio";
        return meta;
    }

    // 8. MP4 / MOV Video
    if (header.size() >= 8 && header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') {
        meta.type = MediaType::Video;
        meta.mime_type = "video/mp4";
        meta.format_name = "MP4";
        meta.is_already_compressed = true;
        meta.container_format = "ISO Base Media (MP4)";
        meta.summary = "MP4 Video Container (H.264/HEVC/AV1 compressed)";
        return meta;
    }

    // 9. Matroska / WebM Video
    if (starts_with(header, {0x1A, 0x45, 0xDF, 0xA3})) {
        meta.type = MediaType::Video;
        meta.mime_type = "video/webm";
        meta.format_name = "WebM/MKV";
        meta.is_already_compressed = true;
        meta.container_format = "Matroska / EBML";
        meta.summary = "MKV/WebM Video Container";
        return meta;
    }

    // 10. ZIP Archive
    if (starts_with(header, {'P', 'K', 0x03, 0x04})) {
        meta.type = MediaType::Archive;
        meta.mime_type = "application/zip";
        meta.format_name = "ZIP";
        meta.is_already_compressed = true;
        meta.summary = "ZIP Compressed Archive";
        return meta;
    }

    // 11. GZIP Archive
    if (starts_with(header, {0x1F, 0x8B})) {
        meta.type = MediaType::Archive;
        meta.mime_type = "application/gzip";
        meta.format_name = "GZIP";
        meta.is_already_compressed = true;
        meta.summary = "GZIP Compressed Archive";
        return meta;
    }

    // Fallbacks based on file extension
    if (ext == ".py" || ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" ||
        ext == ".js" || ext == ".ts" || ext == ".tsx" || ext == ".html" || ext == ".css") {
        meta.type = MediaType::SourceCode;
        meta.mime_type = "text/plain";
        meta.format_name = "Source Code";
        meta.is_already_compressed = false;
        meta.summary = "Source Code (" + ext + ")";
        return meta;
    }

    if (ext == ".json" || ext == ".csv" || ext == ".xml" || ext == ".yaml" || ext == ".yml") {
        meta.type = MediaType::StructuredData;
        meta.mime_type = "application/json";
        meta.format_name = "Structured Data";
        meta.is_already_compressed = false;
        meta.summary = "Structured Records / Data (" + ext + ")";
        return meta;
    }

    if (ext == ".txt" || ext == ".log" || ext == ".md") {
        meta.type = MediaType::Text;
        meta.mime_type = "text/plain";
        meta.format_name = "Text";
        meta.is_already_compressed = false;
        meta.summary = "Plain Text / Log File";
        return meta;
    }

    // Check if contents are predominantly ASCII printable text
    if (!header.empty()) {
        size_t printable = 0;
        for (uint8_t b : header) {
            if ((b >= 32 && b <= 126) || b == '\n' || b == '\r' || b == '\t') {
                ++printable;
            }
        }
        if (static_cast<double>(printable) / static_cast<double>(header.size()) > 0.85) {
            meta.type = MediaType::Text;
            meta.mime_type = "text/plain";
            meta.format_name = "Text";
            meta.is_already_compressed = false;
            meta.summary = "ASCII Text Document";
            return meta;
        }
    }

    meta.type = MediaType::General;
    meta.mime_type = "application/octet-stream";
    meta.format_name = "Binary";
    meta.is_already_compressed = false;
    meta.summary = "General Binary File";
    return meta;
}

} // namespace cacz
