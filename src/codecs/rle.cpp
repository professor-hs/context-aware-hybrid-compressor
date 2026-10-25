#include "rle.hpp"
#include <algorithm>

namespace cacz {

RleCodec::RleCodec() {
    metadata_.id = CodecId::Rle;
    metadata_.name = "rle";
    metadata_.description = "Packet-based byte-oriented Run-Length Encoding";
    metadata_.is_lossless = true;
    metadata_.default_level = 0;
    metadata_.min_level = 0;
    metadata_.max_level = 0;
    metadata_.supports_streaming = false;
}

std::vector<uint8_t> RleCodec::encode(std::span<const uint8_t> data, int /*level*/) const {
    if (data.empty()) {
        return {};
    }

    const size_t n = data.size();
    std::vector<uint8_t> out;
    out.reserve(n);

    std::vector<uint8_t> literal_buf;
    literal_buf.reserve(MAX_LITERAL_LENGTH);

    auto flush_literals = [&]() {
        size_t idx = 0;
        while (idx < literal_buf.size()) {
            size_t chunk_len = std::min(MAX_LITERAL_LENGTH, literal_buf.size() - idx);
            out.push_back(static_cast<uint8_t>(chunk_len - 1));
            out.insert(out.end(), literal_buf.begin() + static_cast<ptrdiff_t>(idx),
                                  literal_buf.begin() + static_cast<ptrdiff_t>(idx + chunk_len));
            idx += chunk_len;
        }
        literal_buf.clear();
    };

    size_t i = 0;
    while (i < n) {
        uint8_t b = data[i];
        size_t run_len = 1;
        while ((i + run_len < n) && (data[i + run_len] == b) && (run_len < MAX_RUN_LENGTH)) {
            ++run_len;
        }

        if (run_len >= MIN_RUN_LENGTH) {
            flush_literals();
            out.push_back(static_cast<uint8_t>(0x80 | (run_len - MIN_RUN_LENGTH)));
            out.push_back(b);
            i += run_len;
        } else {
            literal_buf.push_back(b);
            ++i;
            if (literal_buf.size() >= MAX_LITERAL_LENGTH) {
                flush_literals();
            }
        }
    }

    flush_literals();
    return out;
}

std::vector<uint8_t> RleCodec::decode(std::span<const uint8_t> data) const {
    if (data.empty()) {
        return {};
    }

    const size_t n = data.size();
    std::vector<uint8_t> out;
    out.reserve(n * 2);

    size_t pos = 0;
    while (pos < n) {
        uint8_t ctrl = data[pos++];
        if (ctrl & 0x80) {
            // Run packet: 0x80 | (run_len - 3)
            size_t run_len = (ctrl & 0x7F) + MIN_RUN_LENGTH;
            if (pos >= n) {
                throw TruncatedDataError("RLE stream truncated inside run packet value");
            }
            uint8_t val = data[pos++];
            out.insert(out.end(), run_len, val);
        } else {
            // Literal packet: ctrl is literal_len - 1
            size_t lit_len = static_cast<size_t>(ctrl) + 1;
            if (pos + lit_len > n) {
                throw TruncatedDataError("RLE stream truncated inside literal packet");
            }
            out.insert(out.end(), data.begin() + static_cast<ptrdiff_t>(pos),
                                  data.begin() + static_cast<ptrdiff_t>(pos + lit_len));
            pos += lit_len;
        }
    }

    return out;
}

} // namespace cacz
