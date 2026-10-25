#include "lz77.hpp"
#include <unordered_map>
#include <algorithm>

namespace cacz {

namespace {

struct Token {
    bool is_match{false};
    uint8_t literal{0};
    uint16_t offset{0};
    uint16_t length{0};
};

} // namespace

Lz77Codec::Lz77Codec() {
    metadata_.id = CodecId::Lz77;
    metadata_.name = "lz77";
    metadata_.description = "Sliding-window dictionary compression (LZSS flag-byte framing)";
    metadata_.is_lossless = true;
    metadata_.default_level = 0;
    metadata_.min_level = 0;
    metadata_.max_level = 0;
    metadata_.supports_streaming = false;
}

std::vector<uint8_t> Lz77Codec::encode(std::span<const uint8_t> data, int /*level*/) const {
    if (data.empty()) {
        return {};
    }

    const size_t n = data.size();
    std::vector<uint8_t> out;
    out.reserve(n);

    // 4-byte header: original length in big-endian
    out.resize(4);
    write_be32(out.data(), static_cast<uint32_t>(n));

    // Hash table for 3-byte prefix -> list of candidate positions
    // Use uint32_t key for 3 bytes: (b0 << 16) | (b1 << 8) | b2
    std::unordered_map<uint32_t, std::vector<size_t>> head;
    head.reserve(n / 2);

    std::vector<Token> tokens;
    tokens.reserve(512);

    auto flush_tokens = [&]() {
        size_t idx = 0;
        const size_t num_tokens = tokens.size();
        while (idx < num_tokens) {
            size_t group_size = std::min<size_t>(8, num_tokens - idx);
            uint8_t flags = 0;
            std::vector<uint8_t> payload;
            payload.reserve(group_size * 3);

            for (size_t bit_pos = 0; bit_pos < group_size; ++bit_pos) {
                const auto& tok = tokens[idx + bit_pos];
                if (tok.is_match) {
                    flags |= static_cast<uint8_t>(1 << bit_pos);
                    uint16_t offset_val = tok.offset - 1;
                    uint8_t len_code = 0;
                    int extra = -1;

                    if (tok.length < 18) {
                        len_code = static_cast<uint8_t>(tok.length - 3);
                    } else {
                        len_code = 15;
                        extra = static_cast<int>(tok.length - 18);
                    }

                    uint8_t b0 = static_cast<uint8_t>((offset_val >> 4) & 0xFF);
                    uint8_t b1 = static_cast<uint8_t>(((offset_val & 0x0F) << 4) | (len_code & 0x0F));
                    payload.push_back(b0);
                    payload.push_back(b1);
                    if (extra >= 0) {
                        payload.push_back(static_cast<uint8_t>(extra));
                    }
                } else {
                    payload.push_back(tok.literal);
                }
            }

            out.push_back(flags);
            out.insert(out.end(), payload.begin(), payload.end());
            idx += group_size;
        }
        tokens.clear();
    };

    size_t i = 0;
    while (i < n) {
        size_t best_offset = 0;
        size_t best_len = 0;

        if (i + MIN_MATCH <= n) {
            uint32_t prefix = (static_cast<uint32_t>(data[i]) << 16) |
                              (static_cast<uint32_t>(data[i + 1]) << 8) |
                              static_cast<uint32_t>(data[i + 2]);

            auto it = head.find(prefix);
            if (it != head.end()) {
                const auto& candidates = it->second;
                size_t chain_count = 0;

                for (auto rit = candidates.rbegin(); rit != candidates.rend(); ++rit) {
                    size_t pos = *rit;
                    size_t offset = i - pos;
                    if (offset > WINDOW_SIZE) {
                        break;
                    }
                    ++chain_count;
                    if (chain_count > MAX_CHAIN) {
                        break;
                    }

                    if (best_len >= MIN_MATCH) {
                        size_t check_idx = pos + best_len;
                        size_t target_idx = i + best_len;
                        if (target_idx >= n || check_idx >= i || data[check_idx] != data[target_idx]) {
                            continue;
                        }
                    }

                    size_t match_len = 0;
                    size_t max_possible = std::min(MAX_MATCH, n - i);
                    while (match_len < max_possible && data[pos + match_len] == data[i + match_len]) {
                        ++match_len;
                    }

                    if (match_len > best_len) {
                        best_len = match_len;
                        best_offset = offset;
                        if (match_len == MAX_MATCH) {
                            break;
                        }
                    }
                }
            }
        }

        if (best_len >= MIN_MATCH) {
            tokens.push_back(Token{
                .is_match = true,
                .literal = 0,
                .offset = static_cast<uint16_t>(best_offset),
                .length = static_cast<uint16_t>(best_len)
            });

            size_t end = std::min(i + best_len, n >= MIN_MATCH ? n - MIN_MATCH + 1 : 0);
            size_t insert_end = std::min(i + 4, end);
            for (size_t j = i; j < insert_end; ++j) {
                uint32_t p = (static_cast<uint32_t>(data[j]) << 16) |
                             (static_cast<uint32_t>(data[j + 1]) << 8) |
                             static_cast<uint32_t>(data[j + 2]);
                head[p].push_back(j);
            }
            i += best_len;
        } else {
            tokens.push_back(Token{
                .is_match = false,
                .literal = data[i],
                .offset = 0,
                .length = 0
            });

            if (i + MIN_MATCH <= n) {
                uint32_t prefix = (static_cast<uint32_t>(data[i]) << 16) |
                                  (static_cast<uint32_t>(data[i + 1]) << 8) |
                                  static_cast<uint32_t>(data[i + 2]);
                head[prefix].push_back(i);
            }
            ++i;
        }

        if (tokens.size() >= 512) {
            flush_tokens();
        }
    }

    flush_tokens();
    return out;
}

std::vector<uint8_t> Lz77Codec::decode(std::span<const uint8_t> data) const {
    if (data.empty()) {
        return {};
    }

    if (data.size() < 4) {
        throw TruncatedDataError("LZ77 stream truncated: expected 4-byte header");
    }

    const uint32_t original_length = read_be32(data.data());
    if (original_length == 0) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(original_length);

    size_t i = 4;
    const size_t n = data.size();

    while (out.size() < original_length) {
        if (i >= n) {
            throw TruncatedDataError("LZ77 stream truncated: output smaller than header length");
        }

        uint8_t flags = data[i++];

        for (size_t bit_pos = 0; bit_pos < 8; ++bit_pos) {
            if (out.size() >= original_length) {
                break;
            }

            bool is_match = ((flags >> bit_pos) & 1) != 0;

            if (is_match) {
                if (i + 2 > n) {
                    throw TruncatedDataError("LZ77 stream truncated: incomplete match reference");
                }

                uint8_t b0 = data[i];
                uint8_t b1 = data[i + 1];
                i += 2;

                uint16_t offset_val = static_cast<uint16_t>((static_cast<uint16_t>(b0) << 4) | (b1 >> 4));
                size_t offset = static_cast<size_t>(offset_val) + 1;
                uint8_t len_code = b1 & 0x0F;

                size_t length = 0;
                if (len_code < 15) {
                    length = static_cast<size_t>(len_code) + 3;
                } else {
                    if (i >= n) {
                        throw TruncatedDataError("LZ77 stream truncated: missing extended length byte");
                    }
                    uint8_t extra = data[i++];
                    length = 18 + static_cast<size_t>(extra);
                }

                if (offset > out.size() || offset == 0) {
                    throw CorruptedDataError("Invalid LZ77 backward reference: offset exceeds current output buffer");
                }

                size_t ref_start = out.size() - offset;
                // Overlapping match reproduction: copy byte-by-byte
                for (size_t k = 0; k < length; ++k) {
                    out.push_back(out[ref_start + k]);
                }
            } else {
                if (i >= n) {
                    throw TruncatedDataError("LZ77 stream truncated: missing literal byte");
                }
                out.push_back(data[i++]);
            }
        }
    }

    return out;
}

} // namespace cacz
