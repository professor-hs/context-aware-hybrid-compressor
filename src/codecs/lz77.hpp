#pragma once

#include "cacz/codec.hpp"

namespace cacz {

class Lz77Codec final : public Codec {
public:
    static constexpr size_t WINDOW_SIZE = 4096;
    static constexpr size_t MIN_MATCH = 3;
    static constexpr size_t MAX_MATCH = 258;
    static constexpr size_t MAX_CHAIN = 16;

    Lz77Codec();
    ~Lz77Codec() override = default;

    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Lz77; }
    [[nodiscard]] std::string_view name() const noexcept override { return "lz77"; }
    [[nodiscard]] const CodecMetadata& metadata() const noexcept override { return metadata_; }

    [[nodiscard]] std::vector<uint8_t> encode(std::span<const uint8_t> data, int level = 0) const override;
    [[nodiscard]] std::vector<uint8_t> decode(std::span<const uint8_t> data) const override;

private:
    CodecMetadata metadata_;
};

} // namespace cacz
