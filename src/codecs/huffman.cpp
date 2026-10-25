#include "huffman.hpp"
#include <queue>
#include <array>
#include <tuple>
#include <cassert>

namespace cacz {

namespace {

struct HuffmanNode {
    size_t freq{0};
    int min_symbol{0};
    int symbol{-1}; // -1 for internal node, 0..255 for leaf
    HuffmanNode* left{nullptr};
    HuffmanNode* right{nullptr};

    [[nodiscard]] bool is_leaf() const noexcept {
        return symbol >= 0;
    }
};

struct NodeCompare {
    bool operator()(const std::tuple<size_t, int, size_t, HuffmanNode*>& a,
                    const std::tuple<size_t, int, size_t, HuffmanNode*>& b) const noexcept {
        if (std::get<0>(a) != std::get<0>(b)) {
            return std::get<0>(a) > std::get<0>(b); // lowest frequency first
        }
        if (std::get<1>(a) != std::get<1>(b)) {
            return std::get<1>(a) > std::get<1>(b); // lowest min_symbol first
        }
        return std::get<2>(a) > std::get<2>(b);     // lowest tie_breaker first
    }
};

class BitWriter {
public:
    void write_bit(uint8_t bit) {
        current_byte_ = static_cast<uint8_t>((current_byte_ << 1) | (bit & 1));
        ++bit_count_;
        if (bit_count_ == 8) {
            buffer_.push_back(current_byte_);
            current_byte_ = 0;
            bit_count_ = 0;
        }
    }

    void write_bits(uint32_t value, size_t num_bits) {
        for (size_t i = num_bits; i > 0; --i) {
            write_bit(static_cast<uint8_t>((value >> (i - 1)) & 1));
        }
    }

    void write_byte(uint8_t byte_val) {
        write_bits(byte_val, 8);
    }

    std::vector<uint8_t> flush() {
        if (bit_count_ > 0) {
            buffer_.push_back(static_cast<uint8_t>(current_byte_ << (8 - bit_count_)));
            current_byte_ = 0;
            bit_count_ = 0;
        }
        return std::move(buffer_);
    }

private:
    std::vector<uint8_t> buffer_;
    uint8_t current_byte_{0};
    size_t bit_count_{0};
};

class BitReader {
public:
    BitReader(std::span<const uint8_t> data, size_t byte_offset)
        : data_(data), total_bytes_(data.size()), byte_pos_(byte_offset), bit_pos_(0) {}

    uint8_t read_bit() {
        if (byte_pos_ >= total_bytes_) {
            throw TruncatedDataError("Unexpected EOF while reading Huffman bitstream");
        }
        uint8_t val = static_cast<uint8_t>((data_[byte_pos_] >> (7 - bit_pos_)) & 1);
        ++bit_pos_;
        if (bit_pos_ == 8) {
            bit_pos_ = 0;
            ++byte_pos_;
        }
        return val;
    }

    uint8_t read_byte() {
        uint8_t val = 0;
        for (size_t i = 0; i < 8; ++i) {
            val = static_cast<uint8_t>((val << 1) | read_bit());
        }
        return val;
    }

private:
    std::span<const uint8_t> data_;
    size_t total_bytes_;
    size_t byte_pos_;
    size_t bit_pos_;
};

void generate_codes(const HuffmanNode* node, uint32_t prefix_val, size_t prefix_len,
                    std::array<std::pair<uint32_t, size_t>, 256>& codes) {
    if (node->is_leaf()) {
        assert(node->symbol >= 0 && node->symbol < 256);
        codes[static_cast<size_t>(node->symbol)] = {prefix_val, prefix_len == 0 ? 1 : prefix_len};
    } else {
        assert(node->left && node->right);
        generate_codes(node->left, (prefix_val << 1) | 0, prefix_len + 1, codes);
        generate_codes(node->right, (prefix_val << 1) | 1, prefix_len + 1, codes);
    }
}

void serialize_tree(const HuffmanNode* node, BitWriter& writer) {
    if (node->is_leaf()) {
        writer.write_bit(1);
        writer.write_byte(static_cast<uint8_t>(node->symbol));
    } else {
        writer.write_bit(0);
        assert(node->left && node->right);
        serialize_tree(node->left, writer);
        serialize_tree(node->right, writer);
    }
}

HuffmanNode* deserialize_tree(BitReader& reader, std::vector<HuffmanNode>& pool) {
    uint8_t bit = reader.read_bit();
    pool.emplace_back();
    HuffmanNode* node = &pool.back();

    if (bit == 1) {
        node->symbol = reader.read_byte();
        node->min_symbol = node->symbol;
        node->left = nullptr;
        node->right = nullptr;
        return node;
    } else {
        HuffmanNode* left = deserialize_tree(reader, pool);
        HuffmanNode* right = deserialize_tree(reader, pool);
        node->symbol = -1;
        node->min_symbol = std::min(left->min_symbol, right->min_symbol);
        node->left = left;
        node->right = right;
        return node;
    }
}

} // namespace

HuffmanCodec::HuffmanCodec() {
    metadata_.id = CodecId::Huffman;
    metadata_.name = "huffman";
    metadata_.description = "Deterministic lossless Huffman frequency-tree coding";
    metadata_.is_lossless = true;
    metadata_.default_level = 0;
    metadata_.min_level = 0;
    metadata_.max_level = 0;
    metadata_.supports_streaming = false;
}

std::vector<uint8_t> HuffmanCodec::encode(std::span<const uint8_t> data, int /*level*/) const {
    if (data.empty()) {
        return {};
    }

    const uint32_t original_length = static_cast<uint32_t>(data.size());

    std::array<size_t, 256> freqs{};
    size_t num_symbols = 0;
    for (uint8_t b : data) {
        if (freqs[b] == 0) {
            ++num_symbols;
        }
        ++freqs[b];
    }

    std::vector<uint8_t> out;
    out.reserve(data.size());

    // 4 bytes: original uncompressed size
    out.resize(4);
    write_be32(out.data(), original_length);

    // Single unique symbol case
    if (num_symbols == 1) {
        uint8_t single_sym = 0;
        for (size_t s = 0; s < 256; ++s) {
            if (freqs[s] > 0) {
                single_sym = static_cast<uint8_t>(s);
                break;
            }
        }
        out.push_back(0); // 0 means 1 unique symbol
        out.push_back(single_sym);
        return out;
    }

    // Byte 4: num_symbols - 1 (1..255)
    out.push_back(static_cast<uint8_t>(num_symbols - 1));

    // Build deterministic tree
    std::vector<HuffmanNode> node_pool;
    node_pool.reserve(num_symbols * 3);

    std::priority_queue<std::tuple<size_t, int, size_t, HuffmanNode*>,
                        std::vector<std::tuple<size_t, int, size_t, HuffmanNode*>>,
                        NodeCompare> pq;

    size_t tie_breaker = 0;
    for (size_t s = 0; s < 256; ++s) {
        if (freqs[s] > 0) {
            node_pool.emplace_back(HuffmanNode{
                .freq = freqs[s],
                .min_symbol = static_cast<int>(s),
                .symbol = static_cast<int>(s),
                .left = nullptr,
                .right = nullptr
            });
            pq.emplace(freqs[s], static_cast<int>(s), tie_breaker++, &node_pool.back());
        }
    }

    while (pq.size() > 1) {
        auto [f1, min1, t1, left] = pq.top(); pq.pop();
        auto [f2, min2, t2, right] = pq.top(); pq.pop();

        if (min1 > min2) {
            std::swap(left, right);
            std::swap(min1, min2);
        }

        node_pool.emplace_back(HuffmanNode{
            .freq = f1 + f2,
            .min_symbol = min1,
            .symbol = -1,
            .left = left,
            .right = right
        });
        pq.emplace(f1 + f2, min1, tie_breaker++, &node_pool.back());
    }

    const HuffmanNode* root = std::get<3>(pq.top());

    std::array<std::pair<uint32_t, size_t>, 256> codes{};
    generate_codes(root, 0, 0, codes);

    BitWriter writer;
    serialize_tree(root, writer);

    for (uint8_t b : data) {
        const auto& [c_val, c_len] = codes[b];
        writer.write_bits(c_val, c_len);
    }

    std::vector<uint8_t> payload = writer.flush();
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> HuffmanCodec::decode(std::span<const uint8_t> data) const {
    if (data.empty()) {
        return {};
    }

    if (data.size() < 5) {
        throw TruncatedDataError("Huffman stream too short: expected at least 5 bytes header");
    }

    uint32_t original_length = read_be32(data.data());
    if (original_length == 0) {
        return {};
    }

    size_t num_symbols = static_cast<size_t>(data[4]) + 1;

    // Single symbol case
    if (num_symbols == 1) {
        if (data.size() < 6) {
            throw TruncatedDataError("Single-symbol Huffman stream truncated: missing symbol byte");
        }
        uint8_t sym = data[5];
        return std::vector<uint8_t>(original_length, sym);
    }

    BitReader reader(data, 5);
    std::vector<HuffmanNode> node_pool;
    node_pool.reserve(num_symbols * 4);

    const HuffmanNode* root = nullptr;
    try {
        root = deserialize_tree(reader, node_pool);
    } catch (const TruncatedDataError&) {
        throw;
    } catch (const std::exception& e) {
        throw CorruptedDataError(std::string("Malformed Huffman tree: ") + e.what());
    }

    std::vector<uint8_t> out(original_length);
    const HuffmanNode* curr = root;
    size_t emitted = 0;

    while (emitted < original_length) {
        uint8_t bit = reader.read_bit();
        curr = (bit == 1) ? curr->right : curr->left;
        if (!curr) {
            throw CorruptedDataError("Corrupted Huffman bitstream traversed into nonexistent node");
        }
        if (curr->is_leaf()) {
            out[emitted++] = static_cast<uint8_t>(curr->symbol);
            curr = root;
        }
    }

    return out;
}

} // namespace cacz
