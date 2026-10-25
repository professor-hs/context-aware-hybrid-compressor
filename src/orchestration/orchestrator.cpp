#include "cacz/orchestrator.hpp"
#include "cacz/codec_registry.hpp"
#include <fstream>
#include <chrono>
#include <algorithm>

namespace cacz {

Orchestrator::Orchestrator(ModelConfig model_config)
    : model_adapter_(std::move(model_config)) {}

std::vector<CodecId> Orchestrator::filter_candidates_for_objective(
    Objective objective,
    const MediaMetadata& media_meta,
    const ByteCharacteristics& chars) const {

    (void)chars;
    std::vector<CodecId> list;

    // RAW is always a baseline candidate
    list.push_back(CodecId::Raw);

    if (media_meta.is_already_compressed) {
        // For already-compressed media, avoid expensive search
        return list;
    }

    if (objective == Objective::Fastest) {
        // Fast codecs
        list.push_back(CodecId::Rle);
        list.push_back(CodecId::Delta);
        list.push_back(CodecId::Huffman);
        return list;
    }

    if (objective == Objective::MinimumMemory) {
        list.push_back(CodecId::Rle);
        list.push_back(CodecId::Delta);
        list.push_back(CodecId::Huffman);
        return list;
    }

    // Default & BestRatio: all registered codecs
    list.push_back(CodecId::Rle);
    list.push_back(CodecId::Huffman);
    list.push_back(CodecId::Lz77);
    list.push_back(CodecId::Delta);

    return list;
}

CandidateEvaluation Orchestrator::evaluate_candidate(
    CodecId cid,
    std::span<const uint8_t> sample,
    Objective objective) const {

    CandidateEvaluation eval;
    eval.codec_id = cid;
    eval.codec_name = codec_id_to_name(cid);
    eval.sample_original_size = sample.size();

    if (sample.empty() || cid == CodecId::Raw) {
        eval.sample_compressed_size = sample.size();
        eval.compression_ratio = 1.0;
        eval.throughput_mb_s = 5000.0;
        eval.score = (objective == Objective::Fastest) ? 100.0 : 1.0;
        eval.is_valid = true;
        eval.validation_note = "Raw verbatim passthrough (baseline)";
        return eval;
    }

    auto& reg = CodecRegistry::instance();
    if (!reg.has(cid)) {
        eval.is_valid = false;
        eval.validation_note = "Codec not registered in engine";
        return eval;
    }

    const Codec* codec = reg.get(cid);
    try {
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<uint8_t> enc = codec->encode(sample);
        auto end = std::chrono::high_resolution_clock::now();

        double elapsed_sec = std::chrono::duration<double>(end - start).count();
        if (elapsed_sec <= 0.0) elapsed_sec = 0.000001;

        eval.sample_compressed_size = enc.size();
        eval.compression_ratio = static_cast<double>(enc.size()) / static_cast<double>(sample.size());
        eval.throughput_mb_s = (static_cast<double>(sample.size()) / (1024.0 * 1024.0)) / elapsed_sec;

        // Scoring based on active objective
        switch (objective) {
            case Objective::BestRatio:
                eval.score = (eval.compression_ratio > 0.0) ? (1.0 / eval.compression_ratio) : 0.0;
                break;
            case Objective::Fastest:
                eval.score = eval.throughput_mb_s / (eval.compression_ratio + 0.5);
                break;
            case Objective::MinimumMemory:
                eval.score = (1.0 / eval.compression_ratio) * ((cid == CodecId::Rle || cid == CodecId::Delta) ? 1.5 : 1.0);
                break;
            case Objective::Balanced:
            default:
                eval.score = (1.0 / eval.compression_ratio) * 0.7 + (eval.throughput_mb_s / 50.0) * 0.3;
                break;
        }

        if (eval.compression_ratio >= 1.0) {
            eval.validation_note = "Negative compression on sample (expands data)";
            eval.score *= 0.1;
        } else {
            eval.validation_note = "Valid compression reduction verified";
        }
        eval.is_valid = true;
    } catch (const std::exception& e) {
        eval.is_valid = false;
        eval.validation_note = std::string("Trial encoding error: ") + e.what();
    }

    return eval;
}

OrchestrationPlan Orchestrator::plan(
    const std::filesystem::path& input_path,
    Objective objective) const {

    register_builtin_codecs();

    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        throw CompressionError("Failed to open file for orchestration planning: " + input_path.string());
    }

    in.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    // Read initial sample (up to 32 KB)
    size_t sample_size = static_cast<size_t>(std::min<uint64_t>(file_size, 32768));
    std::vector<uint8_t> sample_buf(sample_size);
    if (sample_size > 0) {
        in.read(reinterpret_cast<char*>(sample_buf.data()), static_cast<std::streamsize>(sample_size));
    }
    in.close();

    OrchestrationPlan plan;
    plan.input_file = input_path;
    plan.file_size = file_size;
    plan.objective = objective;
    plan.objective_name = objective_to_name(objective);

    // 1. Media detection
    plan.media_metadata = MediaDetector::detect(sample_buf, input_path);

    // 2. Byte characteristics analysis
    plan.byte_characteristics = Analyzer::analyze(sample_buf);

    // 3. Filter candidates for objective
    std::vector<CodecId> candidate_ids = filter_candidates_for_objective(objective, plan.media_metadata, plan.byte_characteristics);

    // 4. Query AI model adapter
    plan.ai_recommendation = model_adapter_.query_orchestration(
        plan.media_metadata, plan.byte_characteristics, plan.objective_name, candidate_ids);

    plan.deterministic_fallback_triggered = plan.ai_recommendation.is_fallback;
    plan.fallback_reason = plan.ai_recommendation.fallback_reason;

    // 5. Benchmark / evaluate candidate codecs on sample
    for (CodecId cid : candidate_ids) {
        plan.candidates.push_back(evaluate_candidate(cid, sample_buf, objective));
    }

    // Sort candidates by score descending
    std::sort(plan.candidates.begin(), plan.candidates.end(),
              [](const CandidateEvaluation& a, const CandidateEvaluation& b) {
                  return a.score > b.score;
              });

    // 6. Deterministic Validation Engine
    // Check if AI recommendation is valid and beneficial
    bool ai_accepted = false;
    if (!plan.ai_recommendation.is_fallback) {
        for (const auto& cand : plan.candidates) {
            if (cand.codec_id == plan.ai_recommendation.recommended_codec) {
                if (cand.is_valid && cand.compression_ratio < 1.0) {
                    ai_accepted = true;
                    plan.chosen_codec = cand.codec_id;
                    plan.chosen_codec_name = cand.codec_name;
                    plan.strategy_rationale = "AI recommendation verified by trial benchmark: " +
                                              plan.ai_recommendation.rationale;
                }
                break;
            }
        }
    }

    if (!ai_accepted) {
        // Fall back to top-scoring valid candidate
        for (const auto& cand : plan.candidates) {
            if (cand.is_valid && (cand.compression_ratio < 1.0 || cand.codec_id == CodecId::Raw)) {
                plan.chosen_codec = cand.codec_id;
                plan.chosen_codec_name = cand.codec_name;
                plan.strategy_rationale = "Deterministic engine selected highest-scoring candidate (" +
                                          cand.codec_name + ") under " + plan.objective_name + " objective";
                break;
            }
        }
    }

    return plan;
}

} // namespace cacz
