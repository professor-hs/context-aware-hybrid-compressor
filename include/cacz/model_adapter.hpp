#pragma once

#include "cacz/common.hpp"
#include "cacz/analyzer.hpp"
#include "cacz/media_detector.hpp"
#include <string>
#include <vector>
#include <optional>

namespace cacz {

struct AiModelRecommendation {
    CodecId recommended_codec{CodecId::Raw};
    std::string codec_name{"raw"};
    int compression_level{0};
    std::string rationale;
    double confidence{0.0};
    bool is_fallback{false};
    std::string fallback_reason;
};

struct ModelConfig {
    std::string endpoint_url{"http://127.0.0.1:11434"}; // Ollama or local LLM server
    std::string model_name{"cacz-orchestrator"};
    int timeout_ms{3000};
    bool enabled{true};
};

class ModelAdapter {
public:
    explicit ModelAdapter(ModelConfig config = {});

    [[nodiscard]] bool check_availability() const;

    [[nodiscard]] AiModelRecommendation query_orchestration(
        const MediaMetadata& media_meta,
        const ByteCharacteristics& byte_chars,
        const std::string& objective,
        const std::vector<CodecId>& candidate_codecs) const;

    [[nodiscard]] const ModelConfig& config() const noexcept { return config_; }
    void set_config(const ModelConfig& cfg) { config_ = cfg; }

private:
    ModelConfig config_;

    std::string format_prompt(
        const MediaMetadata& media_meta,
        const ByteCharacteristics& byte_chars,
        const std::string& objective,
        const std::vector<CodecId>& candidate_codecs) const;

    bool send_http_post(const std::string& host, uint16_t port, const std::string& path,
                        const std::string& json_body, std::string& response_body, int timeout_ms) const;
};

} // namespace cacz
