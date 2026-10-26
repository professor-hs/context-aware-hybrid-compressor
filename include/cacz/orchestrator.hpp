#pragma once

#include "cacz/common.hpp"
#include "cacz/analyzer.hpp"
#include "cacz/media_detector.hpp"
#include "cacz/model_adapter.hpp"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace cacz {

struct CandidateEvaluation {
    CodecId codec_id{CodecId::Raw};
    std::string codec_name{"raw"};
    size_t sample_original_size{0};
    size_t sample_compressed_size{0};
    double compression_ratio{1.0};
    double throughput_mb_s{0.0};
    double score{0.0};
    bool is_valid{true};
    std::string validation_note;
};

struct OrchestrationPlan {
    std::filesystem::path input_file;
    uint64_t file_size{0};
    MediaMetadata media_metadata;
    ByteCharacteristics byte_characteristics;
    Objective objective{Objective::Balanced};
    std::string objective_name{"balanced"};

    AiModelRecommendation ai_recommendation;
    std::vector<CandidateEvaluation> candidates;
    CodecId chosen_codec{CodecId::Raw};
    std::string chosen_codec_name{"raw"};
    std::string strategy_rationale;

    bool deterministic_fallback_triggered{false};
    std::string fallback_reason;
};

class Orchestrator {
public:
    explicit Orchestrator(ModelConfig model_config = {});

    [[nodiscard]] OrchestrationPlan plan(
        const std::filesystem::path& input_path,
        Objective objective = Objective::Balanced) const;

    void set_model_config(const ModelConfig& cfg) {
        model_adapter_.set_config(cfg);
    }

private:
    ModelAdapter model_adapter_;

    std::vector<CodecId> filter_candidates_for_objective(
        Objective objective,
        const MediaMetadata& media_meta,
        const ByteCharacteristics& chars) const;

    CandidateEvaluation evaluate_candidate(
        CodecId cid,
        std::span<const uint8_t> sample,
        Objective objective) const;
};

} // namespace cacz
