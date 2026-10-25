#pragma once

#include "cacz/codec.hpp"

namespace cacz {

class RleCodec final : public Codec {
public:
    static constexpr size_t MAX_RUN_LENGTH = 130;
    static constexpr size_t MIN_RUN_LENGTH = 3;
    static constexpr size_t MAX_LITERAL_LENGTH = 128;

    RleCodec();
    ~RleCodec() override = default;

    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Rle; }
    [[nodiscard]] std::string_view name() const noexcept override { return "rle"; }
    [[nodiscard]] const CodecMetadata& metadata() const noexcept override { return metadata_; }

    [[nodiscard]] std::vector<uint8_t> encode(std::span<const uint8_t> data, int level = 0) const override;
    [[nodiscard]] std::vector<uint8_t> decode(std::span<const uint8_t> data) const override;

private:
    CodecMetadata metadata_;
};

} // namespace cacz
