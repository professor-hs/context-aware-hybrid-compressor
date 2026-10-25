#include "raw.hpp"

namespace cacz {

RawCodec::RawCodec() {
    metadata_.id = CodecId::Raw;
    metadata_.name = "raw";
    metadata_.description = "Uncompressed verbatim storage (passthrough)";
    metadata_.is_lossless = true;
    metadata_.default_level = 0;
    metadata_.min_level = 0;
    metadata_.max_level = 0;
    metadata_.supports_streaming = true;
}

std::vector<uint8_t> RawCodec::encode(std::span<const uint8_t> data, int /*level*/) const {
    return std::vector<uint8_t>(data.begin(), data.end());
}

std::vector<uint8_t> RawCodec::decode(std::span<const uint8_t> data) const {
    return std::vector<uint8_t>(data.begin(), data.end());
}

} // namespace cacz
