#include "cacz/codec_registry.hpp"
#include "raw.hpp"
#include "rle.hpp"
#include "huffman.hpp"
#include "lz77.hpp"
#include "delta.hpp"
#include <stdexcept>

namespace cacz {

CodecRegistry& CodecRegistry::instance() {
    static CodecRegistry reg;
    return reg;
}

void CodecRegistry::register_codec(std::unique_ptr<Codec> codec) {
    if (!codec) return;
    std::lock_guard<std::mutex> lock(mutex_);
    uint8_t id_val = static_cast<uint8_t>(codec->id());
    std::string name_str = std::string(codec->name());

    codecs_by_name_[name_str] = codec.get();
    codecs_by_id_[id_val] = std::move(codec);
}

const Codec* CodecRegistry::get(CodecId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = codecs_by_id_.find(static_cast<uint8_t>(id));
    if (it != codecs_by_id_.end()) {
        return it->second.get();
    }
    throw UnsupportedCodecError("Codec ID " + std::to_string(static_cast<int>(id)) + " is not registered");
}

const Codec* CodecRegistry::get(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = codecs_by_name_.find(std::string(name));
    if (it != codecs_by_name_.end()) {
        return it->second;
    }
    throw UnsupportedCodecError("Codec '" + std::string(name) + "' is not registered");
}

std::vector<const Codec*> CodecRegistry::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const Codec*> list;
    list.reserve(codecs_by_id_.size());
    for (const auto& [id, ptr] : codecs_by_id_) {
        list.push_back(ptr.get());
    }
    return list;
}

bool CodecRegistry::has(CodecId id) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return codecs_by_id_.contains(static_cast<uint8_t>(id));
}

bool CodecRegistry::has(std::string_view name) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return codecs_by_name_.contains(std::string(name));
}

void register_builtin_codecs() {
    auto& reg = CodecRegistry::instance();
    if (!reg.has(CodecId::Raw)) {
        reg.register_codec(std::make_unique<RawCodec>());
    }
    if (!reg.has(CodecId::Rle)) {
        reg.register_codec(std::make_unique<RleCodec>());
    }
    if (!reg.has(CodecId::Huffman)) {
        reg.register_codec(std::make_unique<HuffmanCodec>());
    }
    if (!reg.has(CodecId::Lz77)) {
        reg.register_codec(std::make_unique<Lz77Codec>());
    }
    if (!reg.has(CodecId::Delta)) {
        reg.register_codec(std::make_unique<DeltaCodec>());
    }
}

} // namespace cacz
