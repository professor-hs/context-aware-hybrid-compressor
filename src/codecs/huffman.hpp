#pragma once

#include "cacz/codec.hpp"

namespace cacz {

class HuffmanCodec final : public Codec {
public:
    HuffmanCodec();
    ~HuffmanCodec() override = default;

    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Huffman; }
    [[nodiscard]] std::string_view name() const noexcept override { return "huffman"; }
    [[nodiscard]] const CodecMetadata& metadata() const noexcept override { return metadata_; }

    [[nodiscard]] std::vector<uint8_t> encode(std::span<const uint8_t> data, int level = 0) const override;
    [[nodiscard]] std::vector<uint8_t> decode(std::span<const uint8_t> data) const override;

private:
    CodecMetadata metadata_;
};

} // namespace cacz
