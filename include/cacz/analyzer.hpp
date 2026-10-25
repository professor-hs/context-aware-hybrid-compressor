#pragma once

#include "cacz/common.hpp"
#include <array>
#include <vector>
#include <span>
#include <string>

namespace cacz {

struct ByteCharacteristics {
    size_t length{0};
    double entropy{0.0};
    size_t unique_bytes{0};
    double top4_concentration{0.0};
    double run_length_ratio{0.0};
    double repeated_4gram_ratio{0.0};
    double delta_variance{0.0};
    double delta_run_ratio{0.0};
    std::array<size_t, 256> frequencies{};

    // Classification helpers
    bool is_high_entropy{false};
    bool is_highly_repetitive{false};
    bool has_strong_delta_pattern{false};
    bool is_already_compressed{false};
};

class Analyzer {
public:
    static ByteCharacteristics analyze(std::span<const uint8_t> data);

    // Fast heuristic recommendation based on analyzed characteristics
    static CodecId recommend_codec(const ByteCharacteristics& chars, std::string* rationale = nullptr);
};

} // namespace cacz
