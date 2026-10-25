#pragma once

#include "cacz/common.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <memory>

namespace cacz {

struct CodecMetadata {
    CodecId id;
    std::string name;
    std::string description;
    bool is_lossless{true};
    int default_level{0};
    int min_level{0};
    int max_level{0};
    bool supports_streaming{false};
};

class Codec {
public:
    virtual ~Codec() = default;

    [[nodiscard]] virtual CodecId id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual const CodecMetadata& metadata() const noexcept = 0;

    [[nodiscard]] virtual std::vector<uint8_t> encode(std::span<const uint8_t> data, int level = 0) const = 0;
    [[nodiscard]] virtual std::vector<uint8_t> decode(std::span<const uint8_t> data) const = 0;

    [[nodiscard]] virtual bool is_lossless() const noexcept {
        return metadata().is_lossless;
    }
};

} // namespace cacz
