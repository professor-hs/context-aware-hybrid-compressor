#include "cacz/analyzer.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <numeric>

namespace cacz {

ByteCharacteristics Analyzer::analyze(std::span<const uint8_t> data) {
    ByteCharacteristics chars;
    chars.length = data.size();
    if (data.empty()) {
        return chars;
    }

    const size_t n = data.size();

    // 1. Frequencies and unique bytes
    for (uint8_t b : data) {
        ++chars.frequencies[b];
    }

    size_t unique_cnt = 0;
    std::vector<size_t> sorted_freqs;
    sorted_freqs.reserve(256);

    for (size_t f : chars.frequencies) {
        if (f > 0) {
            ++unique_cnt;
            sorted_freqs.push_back(f);
            double p = static_cast<double>(f) / static_cast<double>(n);
            chars.entropy -= p * std::log2(p);
        }
    }
    chars.unique_bytes = unique_cnt;

    // Top-4 byte concentration
    std::sort(sorted_freqs.begin(), sorted_freqs.end(), std::greater<size_t>());
    size_t top4_sum = 0;
    for (size_t i = 0; i < std::min<size_t>(4, sorted_freqs.size()); ++i) {
        top4_sum += sorted_freqs[i];
    }
    chars.top4_concentration = static_cast<double>(top4_sum) / static_cast<double>(n);

    // 2. Run-length ratio (bytes in runs >= 3)
    size_t bytes_in_runs = 0;
    size_t i = 0;
    while (i < n) {
        uint8_t b = data[i];
        size_t rlen = 1;
        while (i + rlen < n && data[i + rlen] == b) {
            ++rlen;
        }
        if (rlen >= 3) {
            bytes_in_runs += rlen;
        }
        i += rlen;
    }
    chars.run_length_ratio = static_cast<double>(bytes_in_runs) / static_cast<double>(n);

    // 3. Repeated 4-gram ratio
    if (n >= 4) {
        std::unordered_set<uint32_t> seen_4grams;
        seen_4grams.reserve(std::min<size_t>(n, 4096));
        size_t repeated_count = 0;

        // Sample up to 1024 4-grams if chunk is large, or all if <= 4096
        size_t stride = (n > 4096) ? (n / 1024) : 1;
        size_t samples = 0;

        for (size_t j = 0; j <= n - 4; j += stride) {
            uint32_t gram = (static_cast<uint32_t>(data[j]) << 24) |
                            (static_cast<uint32_t>(data[j + 1]) << 16) |
                            (static_cast<uint32_t>(data[j + 2]) << 8) |
                            static_cast<uint32_t>(data[j + 3]);
            ++samples;
            if (!seen_4grams.insert(gram).second) {
                ++repeated_count;
            }
        }
        chars.repeated_4gram_ratio = samples > 0 ? (static_cast<double>(repeated_count) / static_cast<double>(samples)) : 0.0;
    }

    // 4. Delta variance and delta-run ratio
    if (n >= 2) {
        std::vector<uint8_t> deltas(n - 1);
        double delta_sum = 0.0;
        for (size_t k = 1; k < n; ++k) {
            uint8_t d = static_cast<uint8_t>((data[k] - data[k - 1]) & 0xFF);
            deltas[k - 1] = d;
            delta_sum += d;
        }
        double delta_mean = delta_sum / static_cast<double>(deltas.size());
        double var_sum = 0.0;
        for (uint8_t d : deltas) {
            double diff = static_cast<double>(d) - delta_mean;
            var_sum += diff * diff;
        }
        chars.delta_variance = var_sum / static_cast<double>(deltas.size());

        size_t delta_runs = 0;
        size_t d_idx = 0;
        while (d_idx < deltas.size()) {
            uint8_t dv = deltas[d_idx];
            size_t drlen = 1;
            while (d_idx + drlen < deltas.size() && deltas[d_idx + drlen] == dv) {
                ++drlen;
            }
            if (drlen >= 3) {
                delta_runs += drlen;
            }
            d_idx += drlen;
        }
        chars.delta_run_ratio = static_cast<double>(delta_runs) / static_cast<double>(deltas.size());
    }

    // Classification flags
    chars.is_high_entropy = (chars.entropy > 7.5);
    chars.is_highly_repetitive = (chars.run_length_ratio > 0.40 || chars.repeated_4gram_ratio > 0.40);
    chars.has_strong_delta_pattern = (chars.delta_run_ratio > 0.25 || chars.delta_variance < 80.0);
    chars.is_already_compressed = (chars.entropy > 7.75 && chars.repeated_4gram_ratio < 0.05 && chars.run_length_ratio < 0.02);

    return chars;
}

CodecId Analyzer::recommend_codec(const ByteCharacteristics& chars, std::string* rationale) {
    if (chars.is_already_compressed) {
        if (rationale) *rationale = "High entropy (>7.75) with negligible redundancy; storing raw to avoid expansion";
        return CodecId::Raw;
    }

    if (chars.has_strong_delta_pattern && chars.delta_run_ratio > 0.30) {
        if (rationale) *rationale = "Strong monotonic/stride pattern detected; delta transformation optimal";
        return CodecId::Delta;
    }

    if (chars.run_length_ratio > 0.40 && chars.repeated_4gram_ratio < 0.35) {
        if (rationale) *rationale = "Long homogeneous byte runs detected; RLE optimal";
        return CodecId::Rle;
    }

    if (chars.repeated_4gram_ratio >= 0.35) {
        if (rationale) *rationale = "High substring repetition detected; LZ77 dictionary compression optimal";
        return CodecId::Lz77;
    }

    if (chars.entropy < 6.5 || chars.top4_concentration > 0.45) {
        if (rationale) *rationale = "Skewed byte frequency distribution with low entropy; Huffman coding optimal";
        return CodecId::Huffman;
    }

    if (chars.entropy > 7.6 && chars.repeated_4gram_ratio < 0.10) {
        if (rationale) *rationale = "High entropy with low repetition; fallback to raw";
        return CodecId::Raw;
    }

    if (rationale) *rationale = "General structured data; LZ77 preferred";
    return CodecId::Lz77;
}

} // namespace cacz
