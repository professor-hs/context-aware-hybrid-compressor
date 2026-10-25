#include "cacz/common.hpp"
#include "cacz/codec_registry.hpp"
#include "cacz/container.hpp"
#include "cacz/analyzer.hpp"
#include "cacz/media_detector.hpp"
#include "cacz/orchestrator.hpp"
#include "cacz/server.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <numeric>
#include <random>
#include <fstream>
#include <filesystem>

using namespace cacz;

int g_tests_run = 0;
int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        ++g_tests_run; \
        std::cout << "Running " #fn "... "; \
        if (fn()) { \
            ++g_tests_passed; \
            std::cout << "PASSED\n"; \
        } else { \
            std::cout << "FAILED\n"; \
        } \
    } while (0)

bool test_rle_roundtrip() {
    register_builtin_codecs();
    const Codec* rle = CodecRegistry::instance().get(CodecId::Rle);

    // Empty
    TEST_ASSERT(rle->encode({}).empty(), "RLE encode empty");
    TEST_ASSERT(rle->decode({}).empty(), "RLE decode empty");

    // Single byte
    std::vector<uint8_t> d1 = {'A'};
    TEST_ASSERT(rle->decode(rle->encode(d1)) == d1, "RLE single byte");

    // Short run (< 3)
    std::vector<uint8_t> d2 = {'A', 'A'};
    TEST_ASSERT(rle->decode(rle->encode(d2)) == d2, "RLE run of 2");

    // Standard run (>= 3)
    std::vector<uint8_t> d3 = {'A', 'A', 'A', 'A', 'A'};
    TEST_ASSERT(rle->decode(rle->encode(d3)) == d3, "RLE run of 5");

    // Long run (> 130 bytes)
    std::vector<uint8_t> d4(500, 'Z');
    auto enc4 = rle->encode(d4);
    TEST_ASSERT(enc4.size() < 20, "RLE high compression on 500 identical bytes");
    TEST_ASSERT(rle->decode(enc4) == d4, "RLE long run roundtrip");

    // Mixed literals and runs
    std::vector<uint8_t> d5 = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!', '!', '!', '!', '!'};
    TEST_ASSERT(rle->decode(rle->encode(d5)) == d5, "RLE mixed roundtrip");

    return true;
}

bool test_huffman_roundtrip() {
    register_builtin_codecs();
    const Codec* huff = CodecRegistry::instance().get(CodecId::Huffman);

    // Empty
    TEST_ASSERT(huff->encode({}).empty(), "Huffman encode empty");
    TEST_ASSERT(huff->decode({}).empty(), "Huffman decode empty");

    // Single repeated byte
    std::vector<uint8_t> d1(100, 'X');
    TEST_ASSERT(huff->decode(huff->encode(d1)) == d1, "Huffman single symbol repeated");

    // Natural text
    std::string text = "The quick brown fox jumps over the lazy dog. Repetition repetition repetition!";
    std::vector<uint8_t> d2(text.begin(), text.end());
    TEST_ASSERT(huff->decode(huff->encode(d2)) == d2, "Huffman text roundtrip");

    // All 256 byte values
    std::vector<uint8_t> d3(256);
    std::iota(d3.begin(), d3.end(), 0);
    TEST_ASSERT(huff->decode(huff->encode(d3)) == d3, "Huffman all 256 bytes roundtrip");

    return true;
}

bool test_lz77_roundtrip() {
    register_builtin_codecs();
    const Codec* lz = CodecRegistry::instance().get(CodecId::Lz77);

    // Empty
    TEST_ASSERT(lz->encode({}).empty(), "LZ77 empty");
    TEST_ASSERT(lz->decode({}).empty(), "LZ77 empty decode");

    // Overlapping match run
    std::vector<uint8_t> d1(300, 'B');
    auto enc1 = lz->encode(d1);
    TEST_ASSERT(enc1.size() < 30, "LZ77 overlapping run compression");
    TEST_ASSERT(lz->decode(enc1) == d1, "LZ77 overlapping run roundtrip");

    // Repeated phrases
    std::string phrase = "Context-Aware Hybrid Compressor with AI Orchestration! ";
    std::string repeated;
    for (int i = 0; i < 20; ++i) repeated += phrase;
    std::vector<uint8_t> d2(repeated.begin(), repeated.end());
    auto enc2 = lz->encode(d2);
    TEST_ASSERT(enc2.size() < d2.size() / 3, "LZ77 phrase repetition compression");
    TEST_ASSERT(lz->decode(enc2) == d2, "LZ77 phrase repetition roundtrip");

    return true;
}

bool test_delta_roundtrip() {
    register_builtin_codecs();
    const Codec* delta = CodecRegistry::instance().get(CodecId::Delta);

    // Monotonic ramp
    std::vector<uint8_t> d1(500);
    for (size_t i = 0; i < d1.size(); ++i) d1[i] = static_cast<uint8_t>(i & 0xFF);
    auto enc1 = delta->encode(d1);
    TEST_ASSERT(enc1.size() < 30, "Delta high compression on monotonic sequence");
    TEST_ASSERT(delta->decode(enc1) == d1, "Delta monotonic roundtrip");

    // Constant stride (+5)
    std::vector<uint8_t> d2(200);
    uint8_t val = 10;
    for (size_t i = 0; i < d2.size(); ++i) {
        d2[i] = val;
        val += 5;
    }
    TEST_ASSERT(delta->decode(delta->encode(d2)) == d2, "Delta constant stride roundtrip");

    return true;
}

bool test_analyzer_entropy() {
    // Zero entropy for uniform data
    std::vector<uint8_t> uniform(100, 0x42);
    auto c1 = Analyzer::analyze(uniform);
    TEST_ASSERT(c1.entropy == 0.0, "Zero entropy for identical bytes");
    TEST_ASSERT(c1.unique_bytes == 1, "1 unique byte");

    // High entropy for pseudo-random data
    std::vector<uint8_t> random(4096);
    std::mt19937 rng(42);
    for (auto& b : random) b = static_cast<uint8_t>(rng() & 0xFF);
    auto c2 = Analyzer::analyze(random);
    TEST_ASSERT(c2.entropy > 7.8, "Random data entropy > 7.8");
    TEST_ASSERT(c2.unique_bytes == 256, "Random data 256 unique bytes");

    return true;
}

bool test_container_roundtrip() {
    std::string test_in = "temp_test_input.txt";
    std::string test_cacz = "temp_test_input.txt.cacz";
    std::string test_out = "temp_test_restored.txt";

    std::ofstream f(test_in, std::ios::binary);
    std::string content = "Context-Aware Hybrid Compressor Native C++20 Test Suite.\n";
    for (int i = 0; i < 500; ++i) {
        f << content << " Line number: " << i << "\n";
    }
    f.close();

    // 1. Heuristic mode
    Container::compress_file(test_in, test_cacz);
    Container::decompress_file(test_cacz, test_out);

    std::ifstream orig(test_in, std::ios::binary);
    std::string orig_data((std::istreambuf_iterator<char>(orig)), std::istreambuf_iterator<char>());
    orig.close();

    std::ifstream rest(test_out, std::ios::binary);
    std::string rest_data((std::istreambuf_iterator<char>(rest)), std::istreambuf_iterator<char>());
    rest.close();

    TEST_ASSERT(orig_data == rest_data, "Container heuristic mode bit-for-bit exact match");

    // 2. Oracle mode
    CompressionOptions opt;
    opt.oracle_search = true;
    Container::compress_file(test_in, test_cacz, opt);
    Container::decompress_file(test_cacz, test_out);

    std::ifstream rest_oracle(test_out, std::ios::binary);
    std::string rest_oracle_data((std::istreambuf_iterator<char>(rest_oracle)), std::istreambuf_iterator<char>());
    rest_oracle.close();

    TEST_ASSERT(orig_data == rest_oracle_data, "Container oracle mode bit-for-bit exact match");

    // Cleanup temp test files
    std::error_code ec;
    std::filesystem::remove(test_in, ec);
    std::filesystem::remove(test_cacz, ec);
    std::filesystem::remove(test_out, ec);

    return true;
}

bool test_container_corruption_detection() {
    std::string test_in = "temp_corrupt_in.txt";
    std::string test_cacz = "temp_corrupt.cacz";
    std::string test_out = "temp_corrupt_out.txt";

    std::ofstream f(test_in, std::ios::binary);
    f << "Important payload that should not be corrupted without detection!\n";
    f.close();

    Container::compress_file(test_in, test_cacz);

    // Read archive bytes
    std::ifstream in(test_cacz, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // Test 1: Bad magic bytes
    auto bad_magic = bytes;
    bad_magic[0] = 'X';
    std::ofstream f_bm(test_cacz, std::ios::binary);
    f_bm.write(reinterpret_cast<const char*>(bad_magic.data()), bad_magic.size());
    f_bm.close();

    bool caught_invalid_magic = false;
    try {
        Container::decompress_file(test_cacz, test_out);
    } catch (const InvalidHeaderError&) {
        caught_invalid_magic = true;
    } catch (...) {}
    TEST_ASSERT(caught_invalid_magic, "InvalidHeaderError caught on bad magic");

    // Test 2: Tampered payload byte (CRC32 mismatch)
    auto tampered = bytes;
    tampered[FILE_HEADER_SIZE_V1 + CHUNK_RECORD_HEADER_SIZE + 2] ^= 0xFF;
    std::ofstream f_t(test_cacz, std::ios::binary);
    f_t.write(reinterpret_cast<const char*>(tampered.data()), tampered.size());
    f_t.close();

    bool caught_crc_mismatch = false;
    try {
        Container::decompress_file(test_cacz, test_out);
    } catch (const CorruptedDataError&) {
        caught_crc_mismatch = true;
    } catch (...) {}
    TEST_ASSERT(caught_crc_mismatch, "CorruptedDataError caught on tampered payload CRC");

    std::error_code ec;
    std::filesystem::remove(test_in, ec);
    std::filesystem::remove(test_cacz, ec);
    std::filesystem::remove(test_out, ec);
    return true;
}

bool test_cross_compatibility_with_python() {
    // Verify that existing Python CACZ archives decompress bit-for-bit in C++
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"english_text.cacz", "benchmark/test_files/english_text.txt"},
        {"numeric_records.cacz", "benchmark/test_files/numeric_records.csv"},
        {"mixed_log.cacz", "benchmark/test_files/mixed_application.log"}
    };

    for (const auto& [cacz_file, original_file] : pairs) {
        if (!std::filesystem::exists(cacz_file) || !std::filesystem::exists(original_file)) {
            continue;
        }

        std::string temp_out = "temp_python_cross.out";
        Container::decompress_file(cacz_file, temp_out);

        std::ifstream orig_f(original_file, std::ios::binary);
        std::string orig((std::istreambuf_iterator<char>(orig_f)), std::istreambuf_iterator<char>());
        orig_f.close();

        std::ifstream dec_f(temp_out, std::ios::binary);
        std::string dec((std::istreambuf_iterator<char>(dec_f)), std::istreambuf_iterator<char>());
        dec_f.close();

        std::error_code ec;
        std::filesystem::remove(temp_out, ec);

        TEST_ASSERT(orig == dec, "Cross-compatibility bit-for-bit exact for " + cacz_file);
    }
    return true;
}

bool test_server_helpers() {
    // 1. Filename sanitization
    TEST_ASSERT(sanitize_filename("../../../etc/passwd") == "passwd", "Strip path traversal Unix");
    TEST_ASSERT(sanitize_filename("C:\\Windows\\System32\\calc.exe") == "calc.exe", "Strip path traversal Windows");
    TEST_ASSERT(sanitize_filename("my%20data%20file.csv") == "my data file.csv", "URL decode spaces");
    TEST_ASSERT(sanitize_filename("bad<name>:\"file\".bin") == "bad_name___file_.bin", "Strip illegal chars");
    TEST_ASSERT(sanitize_filename("   ...   ") == "upload.bin", "Fallback on empty dots/spaces");
    TEST_ASSERT(sanitize_filename("") == "upload.bin", "Fallback on empty string");

    // 2. HTTP status reasons
    TEST_ASSERT(std::string(http_status_reason(200)) == "OK", "Status 200");
    TEST_ASSERT(std::string(http_status_reason(400)) == "Bad Request", "Status 400");
    TEST_ASSERT(std::string(http_status_reason(404)) == "Not Found", "Status 404");
    TEST_ASSERT(std::string(http_status_reason(413)) == "Payload Too Large", "Status 413");
    TEST_ASSERT(std::string(http_status_reason(500)) == "Internal Server Error", "Status 500");
    return true;
}

bool test_large_binary_with_null_bytes() {
    // 128 KB binary payload (> 65,536 buffer) with embedded zeroes and repetitive patterns
    const size_t size = 128 * 1024;
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        if (i % 8 == 0) data[i] = 0x00; // embedded zero bytes
        else data[i] = static_cast<uint8_t>((i / 16) & 0xFF);
    }

    std::string test_in = "temp_large_binary.bin";
    std::string test_arc = "temp_large_binary.cacz";
    std::string test_out = "temp_large_binary_restored.bin";

    std::ofstream out(test_in, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.close();

    Container::compress_file(test_in, test_arc);

    std::string verify_err;
    bool ok = Container::verify(test_arc, &verify_err);
    TEST_ASSERT(ok, "Large binary archive verification passed");

    ArchiveInspection rep = Container::inspect(test_arc);
    TEST_ASSERT(rep.header.original_size == size, "Original size matches in inspection");
    TEST_ASSERT(rep.archive_size < size, "Size reduced on repetitive structured binary");

    Container::decompress_file(test_arc, test_out);

    std::ifstream in(test_out, std::ios::binary);
    std::vector<uint8_t> restored((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::error_code ec;
    std::filesystem::remove(test_in, ec);
    std::filesystem::remove(test_arc, ec);
    std::filesystem::remove(test_out, ec);

    TEST_ASSERT(restored == data, "128 KB binary with embedded null bytes exact match");
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  CACZ C++20 Test Suite: Native Verification Engine\n";
    std::cout << "========================================================\n";

    RUN_TEST(test_rle_roundtrip);
    RUN_TEST(test_huffman_roundtrip);
    RUN_TEST(test_lz77_roundtrip);
    RUN_TEST(test_delta_roundtrip);
    RUN_TEST(test_analyzer_entropy);
    RUN_TEST(test_container_roundtrip);
    RUN_TEST(test_container_corruption_detection);
    RUN_TEST(test_cross_compatibility_with_python);
    RUN_TEST(test_server_helpers);
    RUN_TEST(test_large_binary_with_null_bytes);

    std::cout << "========================================================\n";
    std::cout << "  Tests Completed: " << g_tests_passed << " / " << g_tests_run << " PASSED\n";
    std::cout << "========================================================\n";

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
