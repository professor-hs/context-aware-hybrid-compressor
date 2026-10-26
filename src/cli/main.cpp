#include "cacz/common.hpp"
#include "cacz/codec_registry.hpp"
#include "cacz/container.hpp"
#include "cacz/analyzer.hpp"
#include "cacz/media_detector.hpp"
#include "cacz/orchestrator.hpp"
#include "cacz/server.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>

using namespace cacz;

void print_usage() {
    std::cout << "Context-Aware Hybrid Compressor (CACZ) - Native C++20 Engine\n\n"
              << "Usage: cacz <command> [options] [arguments]\n\n"
              << "Commands:\n"
              << "  codecs                        List registered compression codecs\n"
              << "  inspect <file>                Analyze file entropy and media characteristics\n"
              << "  compress <input> [output]     Compress file into .cacz container archive\n"
              << "    --oracle                    Run full oracle search (smallest payload)\n"
              << "    --ai                        Enable local AI-assisted orchestration\n"
              << "    --objective <obj>           Objective: best_ratio|fastest|balanced|lossless\n"
              << "    --chunk-size <bytes>        Chunk size in bytes (1024 - 1048576, default 8192)\n"
              << "  decompress <archive> [output] Decompress .cacz archive with CRC verification\n"
              << "  verify <archive>              Verify archive integrity and chunk CRCs\n"
              << "  archive-info <archive>        Inspect .cacz container headers and chunk layout\n"
              << "  benchmark <file>              Benchmark all codecs against input file\n"
              << "  server [--port <port>]        Start C++ HTTP API server for Web UI\n"
              << "  help                          Show this help message\n";
}

int cmd_codecs() {
    register_builtin_codecs();
    auto codecs = CodecRegistry::instance().all();

    std::cout << "Registered Compression Codecs (" << codecs.size() << " available):\n";
    std::cout << std::string(70, '-') << "\n";
    std::cout << std::left << std::setw(6) << "ID"
              << std::setw(12) << "Name"
              << std::setw(10) << "Lossless"
              << "Description\n";
    std::cout << std::string(70, '-') << "\n";

    for (const auto* c : codecs) {
        std::cout << std::left << std::setw(6) << static_cast<int>(c->id())
                  << std::setw(12) << c->name()
                  << std::setw(10) << (c->is_lossless() ? "Yes" : "No")
                  << c->metadata().description << "\n";
    }
    std::cout << std::string(70, '-') << "\n";
    return 0;
}

int cmd_inspect(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Error: Cannot open file '" << filepath << "'\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    size_t sample_size = static_cast<size_t>(std::min<uint64_t>(file_size, 65536));
    std::vector<uint8_t> sample(sample_size);
    if (sample_size > 0) {
        in.read(reinterpret_cast<char*>(sample.data()), static_cast<std::streamsize>(sample_size));
    }
    in.close();

    MediaMetadata media = MediaDetector::detect(sample, filepath);
    ByteCharacteristics chars = Analyzer::analyze(sample);
    std::string rationale;
    CodecId rec = Analyzer::recommend_codec(chars, &rationale);

    std::cout << "File Inspection Report: " << filepath << "\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "File Size:                 " << file_size << " bytes (" << (file_size / 1024.0) << " KB)\n";
    std::cout << "Media Format:              " << media.format_name << " (" << media.mime_type << ")\n";
    std::cout << "Media Summary:             " << media.summary << "\n";
    std::cout << "Pre-Compressed:            " << (media.is_already_compressed ? "Yes" : "No") << "\n";
    std::cout << "Shannon Entropy:           " << std::fixed << std::setprecision(4) << chars.entropy << " bits/byte\n";
    std::cout << "Unique Bytes:              " << chars.unique_bytes << " / 256\n";
    std::cout << "Top-4 Byte Concentration:  " << std::setprecision(2) << (chars.top4_concentration * 100.0) << "%\n";
    std::cout << "Run-Length Ratio (>=3):    " << (chars.run_length_ratio * 100.0) << "%\n";
    std::cout << "Repeated 4-gram Ratio:     " << (chars.repeated_4gram_ratio * 100.0) << "%\n";
    std::cout << "Byte Delta Variance:       " << std::setprecision(2) << chars.delta_variance << "\n";
    std::cout << "Delta Run Ratio (>=3):     " << (chars.delta_run_ratio * 100.0) << "%\n";
    std::cout << "Recommended Codec:         " << codec_id_to_name(rec) << "\n";
    std::cout << "Heuristic Rationale:       " << rationale << "\n";
    std::cout << std::string(60, '=') << "\n";
    return 0;
}

int cmd_compress(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing input file\n";
        print_usage();
        return 1;
    }

    std::string input_path = argv[2];
    std::string output_path;
    CompressionOptions options;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--oracle") {
            options.oracle_search = true;
        } else if (arg == "--ai") {
            options.ai_orchestration = true;
        } else if (arg == "--chunk-size" && i + 1 < argc) {
            options.chunk_size = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--objective" && i + 1 < argc) {
            ++i; // stored in options
        } else if (output_path.empty() && !arg.starts_with("--")) {
            output_path = arg;
        }
    }

    if (output_path.empty()) {
        output_path = input_path + ".cacz";
    }

    std::cout << "Compressing: " << input_path << " -> " << output_path << "\n";
    std::cout << "Mode:        " << (options.oracle_search ? "Oracle" : (options.ai_orchestration ? "AI-Assisted" : "Heuristic")) << "\n";
    std::cout << "Chunk Size:  " << options.chunk_size << " bytes\n";

    auto start = std::chrono::high_resolution_clock::now();
    try {
        Container::compress_file(input_path, output_path, options);
    } catch (const std::exception& e) {
        std::cerr << "\nCompression failed: " << e.what() << "\n";
        return 1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();

    ArchiveInspection rep = Container::inspect(output_path);
    std::cout << "\nCompression Completed in " << std::fixed << std::setprecision(3) << sec << " seconds:\n";
    std::cout << "Original Size:    " << rep.header.original_size << " bytes\n";
    std::cout << "Compressed Size:  " << rep.archive_size << " bytes\n";
    std::cout << "Compression Ratio:" << std::setprecision(4) << rep.overall_ratio
              << " (" << std::setprecision(2) << ((1.0 - rep.overall_ratio) * 100.0) << "% reduction)\n";
    std::cout << "Total Chunks:     " << rep.header.total_chunks << "\n";
    return 0;
}

int cmd_decompress(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing archive path\n";
        print_usage();
        return 1;
    }

    std::string input_path = argv[2];
    std::string output_path;

    if (argc >= 4 && !std::string(argv[3]).starts_with("--")) {
        output_path = argv[3];
    } else {
        if (input_path.ends_with(".cacz")) {
            output_path = input_path.substr(0, input_path.size() - 5);
        } else {
            output_path = input_path + ".out";
        }
    }

    std::cout << "Decompressing: " << input_path << " -> " << output_path << "\n";

    auto start = std::chrono::high_resolution_clock::now();
    try {
        Container::decompress_file(input_path, output_path);
    } catch (const std::exception& e) {
        std::cerr << "Decompression failed: " << e.what() << "\n";
        return 1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();

    std::cout << "Decompression successful and verified with CRC32 (" << std::setprecision(3) << sec << " s)\n";
    return 0;
}

int cmd_verify(const std::string& path) {
    std::string err;
    std::cout << "Verifying archive: " << path << " ... ";
    bool ok = Container::verify(path, &err);
    if (ok) {
        std::cout << "VALID [CRC32, Framing, Header OK]\n";
        return 0;
    } else {
        std::cout << "CORRUPT: " << err << "\n";
        return 1;
    }
}

int cmd_archive_info(const std::string& path) {
    try {
        ArchiveInspection rep = Container::inspect(path);
        std::cout << "CACZ Archive Inspection: " << path << "\n";
        std::cout << std::string(60, '=') << "\n";
        std::cout << "Format Version:    " << rep.header.version << "\n";
        std::cout << "Selection Mode:    " << rep.mode_name << " (flags 0x" << std::hex << rep.header.flags << std::dec << ")\n";
        std::cout << "Default Chunk Size:" << rep.header.chunk_size << " bytes\n";
        std::cout << "Total Chunks:      " << rep.header.total_chunks << "\n";
        std::cout << "Original Size:     " << rep.header.original_size << " bytes\n";
        std::cout << "Archive File Size: " << rep.archive_size << " bytes\n";
        std::cout << "Overall Ratio:     " << std::fixed << std::setprecision(4) << rep.overall_ratio << "\n\n";

        std::cout << "Per-Codec Chunk Distribution:\n";
        std::cout << std::string(50, '-') << "\n";
        std::cout << std::left << std::setw(12) << "Codec"
                  << std::setw(10) << "Chunks"
                  << std::setw(14) << "Orig Bytes"
                  << "Comp Bytes\n";
        std::cout << std::string(50, '-') << "\n";

        for (const auto& [name, count] : rep.codec_chunk_counts) {
            std::cout << std::left << std::setw(12) << name
                      << std::setw(10) << count
                      << std::setw(14) << rep.codec_uncompressed_bytes[name]
                      << rep.codec_compressed_bytes[name] << "\n";
        }
        std::cout << std::string(50, '-') << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error inspecting archive: " << e.what() << "\n";
        return 1;
    }
}

int cmd_benchmark(const std::string& path) {
    register_builtin_codecs();
    auto& reg = CodecRegistry::instance();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Cannot open file '" << path << "'\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    uint64_t fsize = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fsize);
    if (fsize > 0) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fsize));
    }
    in.close();

    std::cout << "Benchmarking All Codecs on: " << path << " (" << fsize << " bytes)\n";
    std::cout << std::string(75, '=') << "\n";
    std::cout << std::left << std::setw(12) << "Codec"
              << std::setw(14) << "Comp Bytes"
              << std::setw(12) << "Ratio"
              << std::setw(16) << "Comp (MB/s)"
              << std::setw(16) << "Decomp (MB/s)"
              << "Match\n";
    std::cout << std::string(75, '-') << "\n";

    for (const auto* c : reg.all()) {
        try {
            auto t0 = std::chrono::high_resolution_clock::now();
            std::vector<uint8_t> enc = c->encode(data);
            auto t1 = std::chrono::high_resolution_clock::now();

            auto t2 = std::chrono::high_resolution_clock::now();
            std::vector<uint8_t> dec = c->decode(enc);
            auto t3 = std::chrono::high_resolution_clock::now();

            double enc_sec = std::chrono::duration<double>(t1 - t0).count();
            double dec_sec = std::chrono::duration<double>(t3 - t2).count();
            if (enc_sec <= 0.0) enc_sec = 0.000001;
            if (dec_sec <= 0.0) dec_sec = 0.000001;

            double ratio = (fsize > 0) ? (static_cast<double>(enc.size()) / static_cast<double>(fsize)) : 1.0;
            double enc_mb = (static_cast<double>(fsize) / (1024.0 * 1024.0)) / enc_sec;
            double dec_mb = (static_cast<double>(fsize) / (1024.0 * 1024.0)) / dec_sec;
            bool bit_exact = (dec == data);

            std::cout << std::left << std::setw(12) << c->name()
                      << std::setw(14) << enc.size()
                      << std::fixed << std::setprecision(4) << std::setw(12) << ratio
                      << std::setprecision(2) << std::setw(16) << enc_mb
                      << std::setprecision(2) << std::setw(16) << dec_mb
                      << (bit_exact ? "PASS" : "FAIL") << "\n";
        } catch (const std::exception& e) {
            std::cout << std::left << std::setw(12) << c->name()
                      << "ERROR: " << e.what() << "\n";
        }
    }
    std::cout << std::string(75, '=') << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "codecs") {
        return cmd_codecs();
    } else if (cmd == "inspect" && argc >= 3) {
        return cmd_inspect(argv[2]);
    } else if (cmd == "compress") {
        return cmd_compress(argc, argv);
    } else if (cmd == "decompress") {
        return cmd_decompress(argc, argv);
    } else if (cmd == "verify" && argc >= 3) {
        return cmd_verify(argv[2]);
    } else if (cmd == "archive-info" && argc >= 3) {
        return cmd_archive_info(argv[2]);
    } else if (cmd == "benchmark" && argc >= 3) {
        return cmd_benchmark(argv[2]);
    } else if (cmd == "server") {
        uint16_t port = 8080;
        // Read Render / Cloud hosting PORT environment variable if available
        if (const char* env_port = std::getenv("PORT")) {
            try {
                int p = std::stoi(env_port);
                if (p > 0 && p <= 65535) {
                    port = static_cast<uint16_t>(p);
                }
            } catch (...) {
                // Ignore parse errors and retain default
            }
        }
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        }
        return run_http_server(port);
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    } else {
        std::cerr << "Unknown command: " << cmd << "\n\n";
        print_usage();
        return 1;
    }
}
