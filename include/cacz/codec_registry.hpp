#pragma once

#include "cacz/codec.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>

namespace cacz {

class CodecRegistry {
public:
    static CodecRegistry& instance();

    void register_codec(std::unique_ptr<Codec> codec);

    [[nodiscard]] const Codec* get(CodecId id) const;
    [[nodiscard]] const Codec* get(std::string_view name) const;

    [[nodiscard]] std::vector<const Codec*> all() const;
    [[nodiscard]] bool has(CodecId id) const noexcept;
    [[nodiscard]] bool has(std::string_view name) const noexcept;

private:
    CodecRegistry() = default;
    ~CodecRegistry() = default;
    CodecRegistry(const CodecRegistry&) = delete;
    CodecRegistry& operator=(const CodecRegistry&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<uint8_t, std::unique_ptr<Codec>> codecs_by_id_;
    std::unordered_map<std::string, const Codec*> codecs_by_name_;
};

// Global helper function to initialize default built-in codecs
void register_builtin_codecs();

} // namespace cacz
