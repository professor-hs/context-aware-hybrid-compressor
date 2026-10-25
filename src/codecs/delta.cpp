#include "delta.hpp"

namespace cacz {

DeltaCodec::DeltaCodec() {
    metadata_.id = CodecId::Delta;
    metadata_.name = "delta";
    metadata_.description = "Reversible byte-delta modulo-256 transform coupled with RLE";
    metadata_.is_lossless = true;
    metadata_.default_level = 0;
    metadata_.min_level = 0;
    metadata_.max_level = 0;
    metadata_.supports_streaming = false;
}

std::vector<uint8_t> DeltaCodec::encode(std::span<const uint8_t> data, int level) const {
    if (data.empty()) {
        return {};
    }

    const size_t n = data.size();
    std::vector<uint8_t> transformed(n);
    transformed[0] = data[0];
    uint8_t prev = data[0];
    for (size_t i = 1; i < n; ++i) {
        uint8_t curr = data[i];
        transformed[i] = static_cast<uint8_t>((curr - prev) & 0xFF);
        prev = curr;
    }

    std::vector<uint8_t> rle_encoded = rle_codec_.encode(transformed, level);

    std::vector<uint8_t> out;
    out.reserve(1 + rle_encoded.size());
    out.push_back(DELTA_MAGIC_V1);
    out.insert(out.end(), rle_encoded.begin(), rle_encoded.end());
    return out;
}

std::vector<uint8_t> DeltaCodec::decode(std::span<const uint8_t> data) const {
    if (data.empty()) {
        return {};
    }

    if (data.size() < 2) {
        throw TruncatedDataError("Delta stream truncated: requires at least 2 bytes");
    }

    if (data[0] != DELTA_MAGIC_V1) {
        throw InvalidHeaderError("Invalid Delta magic byte");
    }

    std::vector<uint8_t> diffs = rle_codec_.decode(data.subspan(1));
    if (diffs.empty()) {
        return {};
    }

    const size_t n = diffs.size();
    std::vector<uint8_t> out(n);
    out[0] = diffs[0];
    uint8_t running = diffs[0];
    for (size_t i = 1; i < n; ++i) {
        running = static_cast<uint8_t>((running + diffs[i]) & 0xFF);
        out[i] = running;
    }

    return out;
}

} // namespace cacz
