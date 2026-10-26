#include "cacz/container.hpp"
#include "cacz/codec_registry.hpp"
#include "cacz/analyzer.hpp"
#include <fstream>
#include <chrono>
#include <random>
#include <iostream>

namespace cacz {

namespace {

std::filesystem::path make_temp_path(const std::filesystem::path& target) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    return target.string() + ".tmp." + std::to_string(now) + "_" + std::to_string(rd());
}

} // namespace

void Container::compress_file(const std::filesystem::path& input_path,
                              const std::filesystem::path& output_path,
                              const CompressionOptions& options) {
    register_builtin_codecs();
    auto& registry = CodecRegistry::instance();

    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        throw CompressionError("Failed to open input file for reading: " + input_path.string());
    }

    in.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    size_t chunk_size = options.chunk_size;
    if (chunk_size < MIN_CHUNK_SIZE || chunk_size > MAX_CHUNK_SIZE) {
        chunk_size = DEFAULT_CHUNK_SIZE;
    }

    uint32_t total_chunks = 0;
    if (file_size == 0) {
        total_chunks = 0;
    } else {
        total_chunks = static_cast<uint32_t>((file_size + chunk_size - 1) / chunk_size);
    }

    uint16_t flags = options.mode_flags;
    if (options.oracle_search || options.objective == Objective::BestRatio) {
        flags = FLAG_MODE_ORACLE;
    } else if (options.ai_orchestration) {
        flags = FLAG_MODE_AI;
    }

    // Build 28-byte header
    std::array<uint8_t, FILE_HEADER_SIZE_V1> header_buf{};
    std::copy(std::begin(MAGIC), std::end(MAGIC), header_buf.begin());
    write_be16(&header_buf[4], options.version);
    write_be16(&header_buf[6], flags);
    write_be32(&header_buf[8], static_cast<uint32_t>(chunk_size));
    write_be32(&header_buf[12], total_chunks);
    write_be64(&header_buf[16], file_size);

    uint32_t header_crc = Crc32::calculate(std::span<const uint8_t>(header_buf.data(), 24));
    write_be32(&header_buf[24], header_crc);

    std::filesystem::path temp_out_path = make_temp_path(output_path);
    std::ofstream out(temp_out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw CompressionError("Failed to create temporary output file: " + temp_out_path.string());
    }

    out.write(reinterpret_cast<const char*>(header_buf.data()), header_buf.size());

    std::vector<uint8_t> chunk_buffer(chunk_size);
    uint32_t chunk_idx = 0;

    const std::array<CodecId, 5> standard_codecs = {
        CodecId::Raw, CodecId::Rle, CodecId::Huffman, CodecId::Lz77, CodecId::Delta
    };

    while (in) {
        in.read(reinterpret_cast<char*>(chunk_buffer.data()), static_cast<std::streamsize>(chunk_size));
        std::streamsize bytes_read = in.gcount();
        if (bytes_read <= 0) {
            break;
        }

        std::span<const uint8_t> raw_chunk(chunk_buffer.data(), static_cast<size_t>(bytes_read));
        uint32_t uncompressed_crc = Crc32::calculate(raw_chunk);

        CodecId chosen_codec_id = CodecId::Raw;
        std::vector<uint8_t> chosen_payload;

        bool run_oracle = options.oracle_search || (options.objective == Objective::BestRatio);

        if (run_oracle) {
            // Oracle mode: evaluate candidate codecs and pick the smallest payload
            size_t best_size = raw_chunk.size();
            chosen_payload.assign(raw_chunk.begin(), raw_chunk.end());
            chosen_codec_id = CodecId::Raw;

            for (CodecId cid : standard_codecs) {
                if (cid == CodecId::Raw) continue;
                // If fastest or minimum_memory objective, skip expensive LZ77
                if ((options.objective == Objective::Fastest || options.objective == Objective::MinimumMemory) &&
                    cid == CodecId::Lz77) {
                    continue;
                }
                try {
                    const Codec* c = registry.get(cid);
                    std::vector<uint8_t> enc = c->encode(raw_chunk);
                    if (enc.size() < best_size) {
                        best_size = enc.size();
                        chosen_payload = std::move(enc);
                        chosen_codec_id = cid;
                    }
                } catch (...) {
                    // Skip codec on error
                }
            }
        } else {
            // Heuristic mode: analyze chunk characteristics
            ByteCharacteristics chars = Analyzer::analyze(raw_chunk);
            CodecId recommended = Analyzer::recommend_codec(chars);

            // Objective adjustments
            if (options.objective == Objective::Fastest && recommended == CodecId::Lz77) {
                // For fastest throughput, favor high-speed codecs
                if (chars.run_length_ratio > 0.15) {
                    recommended = CodecId::Rle;
                } else if (chars.delta_variance < 150.0) {
                    recommended = CodecId::Delta;
                } else {
                    recommended = CodecId::Huffman;
                }
            } else if (options.objective == Objective::MinimumMemory && recommended == CodecId::Lz77) {
                recommended = CodecId::Huffman;
            }

            if (recommended == CodecId::Raw) {
                chosen_codec_id = CodecId::Raw;
                chosen_payload.assign(raw_chunk.begin(), raw_chunk.end());
            } else {
                try {
                    const Codec* c = registry.get(recommended);
                    std::vector<uint8_t> enc = c->encode(raw_chunk);
                    // Check for negative compression
                    if (enc.size() < raw_chunk.size()) {
                        chosen_codec_id = recommended;
                        chosen_payload = std::move(enc);
                    } else {
                        // Fallback to RAW
                        chosen_codec_id = CodecId::Raw;
                        chosen_payload.assign(raw_chunk.begin(), raw_chunk.end());
                    }
                } catch (...) {
                    chosen_codec_id = CodecId::Raw;
                    chosen_payload.assign(raw_chunk.begin(), raw_chunk.end());
                }
            }
        }

        // Write 18-byte Chunk Record Header
        std::array<uint8_t, CHUNK_RECORD_HEADER_SIZE> chunk_hdr_buf{};
        write_be32(&chunk_hdr_buf[0], chunk_idx);
        chunk_hdr_buf[4] = static_cast<uint8_t>(chosen_codec_id);
        chunk_hdr_buf[5] = 0x00; // reserved
        write_be32(&chunk_hdr_buf[6], static_cast<uint32_t>(raw_chunk.size()));
        write_be32(&chunk_hdr_buf[10], static_cast<uint32_t>(chosen_payload.size()));
        write_be32(&chunk_hdr_buf[14], uncompressed_crc);

        out.write(reinterpret_cast<const char*>(chunk_hdr_buf.data()), chunk_hdr_buf.size());
        out.write(reinterpret_cast<const char*>(chosen_payload.data()), static_cast<std::streamsize>(chosen_payload.size()));

        if (options.progress_callback) {
            options.progress_callback(chunk_idx, total_chunks, chosen_codec_id);
        }

        ++chunk_idx;
    }

    out.flush();
    out.close();
    in.close();

    // Atomic replacement
    std::error_code ec;
    std::filesystem::rename(temp_out_path, output_path, ec);
    if (ec) {
        std::filesystem::remove(temp_out_path, ec);
        throw CompressionError("Failed to rename temporary output file to final path: " + ec.message());
    }
}

void Container::decompress_file(const std::filesystem::path& input_path,
                                const std::filesystem::path& output_path) {
    register_builtin_codecs();
    auto& registry = CodecRegistry::instance();

    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        throw DecompressionError("Failed to open archive for reading: " + input_path.string());
    }

    in.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (file_size < FILE_HEADER_SIZE_V1) {
        throw TruncatedDataError("File too small to contain a valid CACZ header");
    }

    std::array<uint8_t, FILE_HEADER_SIZE_V1> hdr_buf{};
    in.read(reinterpret_cast<char*>(hdr_buf.data()), FILE_HEADER_SIZE_V1);
    if (in.gcount() != FILE_HEADER_SIZE_V1) {
        throw TruncatedDataError("Incomplete CACZ file header read");
    }

    // Magic check
    if (std::memcmp(hdr_buf.data(), MAGIC, 4) != 0) {
        throw InvalidHeaderError("Invalid magic bytes: not a CACZ container archive");
    }

    // Header CRC check
    uint32_t expected_hdr_crc = read_be32(&hdr_buf[24]);
    uint32_t actual_hdr_crc = Crc32::calculate(std::span<const uint8_t>(hdr_buf.data(), 24));
    if (actual_hdr_crc != expected_hdr_crc) {
        throw CorruptedDataError("File header CRC32 mismatch: container header is corrupt");
    }

    uint16_t version = read_be16(&hdr_buf[4]);
    if (version != FORMAT_VERSION_1 && version != FORMAT_VERSION_2) {
        throw InvalidHeaderError("Unsupported container format version: " + std::to_string(version));
    }

    uint32_t total_chunks = read_be32(&hdr_buf[12]);
    uint64_t original_size = read_be64(&hdr_buf[16]);

    std::filesystem::path temp_out_path = make_temp_path(output_path);
    std::ofstream out(temp_out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw DecompressionError("Failed to create temporary output file: " + temp_out_path.string());
    }

    uint64_t total_decompressed_bytes = 0;
    std::vector<uint8_t> payload_buf;

    for (uint32_t chunk_idx = 0; chunk_idx < total_chunks; ++chunk_idx) {
        std::array<uint8_t, CHUNK_RECORD_HEADER_SIZE> chunk_hdr_buf{};
        in.read(reinterpret_cast<char*>(chunk_hdr_buf.data()), CHUNK_RECORD_HEADER_SIZE);
        if (in.gcount() != CHUNK_RECORD_HEADER_SIZE) {
            throw TruncatedDataError("Premature EOF while reading chunk header for chunk " + std::to_string(chunk_idx));
        }

        uint32_t index = read_be32(&chunk_hdr_buf[0]);
        if (index != chunk_idx) {
            throw CorruptedDataError("Chunk sequence out-of-order: expected chunk " +
                                     std::to_string(chunk_idx) + ", found " + std::to_string(index));
        }

        CodecId codec_id = static_cast<CodecId>(chunk_hdr_buf[4]);
        uint32_t orig_len = read_be32(&chunk_hdr_buf[6]);
        uint32_t payload_len = read_be32(&chunk_hdr_buf[10]);
        uint32_t expected_chunk_crc = read_be32(&chunk_hdr_buf[14]);

        payload_buf.resize(payload_len);
        if (payload_len > 0) {
            in.read(reinterpret_cast<char*>(payload_buf.data()), static_cast<std::streamsize>(payload_len));
            if (static_cast<size_t>(in.gcount()) != payload_len) {
                throw TruncatedDataError("Premature EOF reading payload for chunk " + std::to_string(chunk_idx));
            }
        }

        const Codec* codec = registry.get(codec_id);
        std::vector<uint8_t> decoded = codec->decode(std::span<const uint8_t>(payload_buf.data(), payload_len));

        if (decoded.size() != orig_len) {
            throw CorruptedDataError("Decompressed length mismatch in chunk " + std::to_string(chunk_idx) +
                                     ": expected " + std::to_string(orig_len) + ", produced " + std::to_string(decoded.size()));
        }

        uint32_t actual_chunk_crc = Crc32::calculate(decoded);
        if (actual_chunk_crc != expected_chunk_crc) {
            throw CorruptedDataError("Uncompressed CRC32 mismatch in chunk " + std::to_string(chunk_idx));
        }

        if (!decoded.empty()) {
            out.write(reinterpret_cast<const char*>(decoded.data()), static_cast<std::streamsize>(decoded.size()));
        }
        total_decompressed_bytes += decoded.size();
    }

    // Verify trailing bytes
    char trailing_byte;
    in.read(&trailing_byte, 1);
    if (in.gcount() > 0) {
        throw CorruptedDataError("Extraneous trailing data detected after final chunk record");
    }

    if (total_decompressed_bytes != original_size) {
        throw CorruptedDataError("Overall decompressed file size mismatch: expected " +
                                 std::to_string(original_size) + ", produced " + std::to_string(total_decompressed_bytes));
    }

    out.flush();
    out.close();
    in.close();

    std::error_code ec;
    std::filesystem::rename(temp_out_path, output_path, ec);
    if (ec) {
        std::filesystem::remove(temp_out_path, ec);
        throw DecompressionError("Failed to rename temporary output file to final path: " + ec.message());
    }
}

ArchiveInspection Container::inspect(const std::filesystem::path& archive_path) {
    register_builtin_codecs();

    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open()) {
        throw DecompressionError("Failed to open archive: " + archive_path.string());
    }

    in.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (file_size < FILE_HEADER_SIZE_V1) {
        throw TruncatedDataError("File too small for valid header");
    }

    std::array<uint8_t, FILE_HEADER_SIZE_V1> hdr_buf{};
    in.read(reinterpret_cast<char*>(hdr_buf.data()), FILE_HEADER_SIZE_V1);

    if (std::memcmp(hdr_buf.data(), MAGIC, 4) != 0) {
        throw InvalidHeaderError("Invalid magic identifier");
    }

    uint32_t expected_hdr_crc = read_be32(&hdr_buf[24]);
    uint32_t actual_hdr_crc = Crc32::calculate(std::span<const uint8_t>(hdr_buf.data(), 24));
    if (actual_hdr_crc != expected_hdr_crc) {
        throw CorruptedDataError("Header CRC32 checksum mismatch");
    }

    ArchiveInspection report;
    report.archive_size = file_size;
    report.header.version = read_be16(&hdr_buf[4]);
    report.header.flags = read_be16(&hdr_buf[6]);
    report.header.chunk_size = read_be32(&hdr_buf[8]);
    report.header.total_chunks = read_be32(&hdr_buf[12]);
    report.header.original_size = read_be64(&hdr_buf[16]);
    report.header.header_crc = expected_hdr_crc;

    if (report.header.flags & FLAG_MODE_ORACLE) {
        report.mode_name = "oracle";
    } else if (report.header.flags & FLAG_MODE_AI) {
        report.mode_name = "ai-orchestrated";
    } else {
        report.mode_name = "heuristic";
    }

    report.overall_ratio = (report.header.original_size > 0)
        ? (static_cast<double>(report.archive_size) / static_cast<double>(report.header.original_size))
        : 1.0;

    for (uint32_t chunk_idx = 0; chunk_idx < report.header.total_chunks; ++chunk_idx) {
        std::array<uint8_t, CHUNK_RECORD_HEADER_SIZE> chunk_hdr_buf{};
        in.read(reinterpret_cast<char*>(chunk_hdr_buf.data()), CHUNK_RECORD_HEADER_SIZE);
        if (in.gcount() != CHUNK_RECORD_HEADER_SIZE) {
            throw TruncatedDataError("Premature EOF reading chunk header");
        }

        uint32_t index = read_be32(&chunk_hdr_buf[0]);
        CodecId cid = static_cast<CodecId>(chunk_hdr_buf[4]);
        uint32_t orig_len = read_be32(&chunk_hdr_buf[6]);
        uint32_t payload_len = read_be32(&chunk_hdr_buf[10]);
        uint32_t uncompressed_crc = read_be32(&chunk_hdr_buf[14]);

        in.seekg(payload_len, std::ios::cur);

        std::string cname = codec_id_to_name(cid);
        double ratio = (orig_len > 0) ? (static_cast<double>(payload_len) / static_cast<double>(orig_len)) : 1.0;

        report.chunks.push_back(ChunkInspection{
            .index = index,
            .codec_id = cid,
            .codec_name = cname,
            .original_length = orig_len,
            .payload_length = payload_len,
            .uncompressed_crc = uncompressed_crc,
            .compression_ratio = ratio
        });

        report.codec_chunk_counts[cname] += 1;
        report.codec_uncompressed_bytes[cname] += orig_len;
        report.codec_compressed_bytes[cname] += payload_len;
    }

    return report;
}

bool Container::verify(const std::filesystem::path& archive_path, std::string* error_msg) {
    try {
        inspect(archive_path);
        // Also test decompressing to null/memory
        auto temp_file = make_temp_path(archive_path);
        decompress_file(archive_path, temp_file);
        std::error_code ec;
        std::filesystem::remove(temp_file, ec);
        return true;
    } catch (const std::exception& e) {
        if (error_msg) *error_msg = e.what();
        return false;
    }
}

} // namespace cacz
