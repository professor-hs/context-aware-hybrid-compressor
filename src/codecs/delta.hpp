#pragma once

#include "cacz/codec.hpp"
#include "rle.hpp"

namespace cacz {

class DeltaCodec final : public Codec {
public:
    static constexpr uint8_t DELTA_MAGIC_V1 = 0x01;

    DeltaCodec();
    ~DeltaCodec() override = default;

    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Delta; }
    [[nodiscard]] std::string_view name() const noexcept override { return "delta"; }
    [[nodiscard]] const CodecMetadata& metadata() const noexcept override { return metadata_; }

    [[nodiscard]] std::vector<uint8_t> encode(std::span<const uint8_t> data, int level = 0) const override;
    [[nodiscard]] std::vector<uint8_t> decode(std::span<const uint8_t> data) const override;

private:
    CodecMetadata metadata_;
    RleCodec rle_codec_;
};

} // namespace cacz
