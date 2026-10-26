#include "cacz/model_adapter.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace cacz {

namespace {

struct UrlParts {
    std::string host{"127.0.0.1"};
    uint16_t port{11434};
    std::string path{"/v1/chat/completions"};
};

UrlParts parse_url(const std::string& url) {
    UrlParts parts;
    std::string u = url;
    if (u.starts_with("http://")) {
        u = u.substr(7);
    }
    size_t colon_pos = u.find(':');
    size_t slash_pos = u.find('/');

    if (colon_pos != std::string::npos) {
        parts.host = u.substr(0, colon_pos);
        if (slash_pos != std::string::npos) {
            parts.port = static_cast<uint16_t>(std::stoi(u.substr(colon_pos + 1, slash_pos - colon_pos - 1)));
            parts.path = u.substr(slash_pos);
        } else {
            parts.port = static_cast<uint16_t>(std::stoi(u.substr(colon_pos + 1)));
            parts.path = "/v1/chat/completions";
        }
    } else {
        if (slash_pos != std::string::npos) {
            parts.host = u.substr(0, slash_pos);
            parts.path = u.substr(slash_pos);
        } else {
            parts.host = u;
            parts.path = "/v1/chat/completions";
        }
    }
    return parts;
}

} // namespace

ModelAdapter::ModelAdapter(ModelConfig config) : config_(std::move(config)) {
#ifdef _WIN32
    static bool winsock_inited = false;
    if (!winsock_inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        winsock_inited = true;
    }
#endif
}

bool ModelAdapter::send_http_post(const std::string& host, uint16_t port, const std::string& path,
                                  const std::string& json_body, std::string& response_body, int timeout_ms) const {
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    DWORD tv = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        return false;
    }

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json_body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << json_body;

    std::string req_str = req.str();
    send(sock, req_str.c_str(), static_cast<int>(req_str.size()), 0);

    char buf[2048];
    std::string resp;
    int bytes_read;
    while ((bytes_read = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[bytes_read] = '\0';
        resp.append(buf, bytes_read);
    }
    closesocket(sock);

    size_t body_start = resp.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        response_body = resp.substr(body_start + 4);
    } else {
        response_body = resp;
    }
    return !response_body.empty();
}

bool ModelAdapter::check_availability() const {
    if (!config_.enabled) return false;
    UrlParts parts = parse_url(config_.endpoint_url);
    std::string resp;
    return send_http_post(parts.host, parts.port, "/api/tags", "{}", resp, 1000);
}

std::string ModelAdapter::format_prompt(
    const MediaMetadata& media_meta,
    const ByteCharacteristics& byte_chars,
    const std::string& objective,
    const std::vector<CodecId>& candidate_codecs) const {

    (void)candidate_codecs;
    std::ostringstream ss;
    ss << "{\"model\":\"" << config_.model_name << "\",\"messages\":["
       << "{\"role\":\"system\",\"content\":\"You are an expert compression orchestrator. Analyze file characteristics and output ONLY valid JSON recommending the best codec.\"},"
       << "{\"role\":\"user\",\"content\":\"File type: " << media_meta.format_name
       << ", MIME: " << media_meta.mime_type
       << ", Length: " << byte_chars.length
       << ", Shannon Entropy: " << byte_chars.entropy
       << ", Repetition 4-gram ratio: " << byte_chars.repeated_4gram_ratio
       << ", Run-length ratio: " << byte_chars.run_length_ratio
       << ", Delta variance: " << byte_chars.delta_variance
       << ", Delta run ratio: " << byte_chars.delta_run_ratio
       << ", Objective: " << objective
       << ". Available codecs: [raw, rle, huffman, lz77, delta]. Respond strictly in JSON: "
       << "{\\\"recommended_codec\\\":\\\"<name>\\\",\\\"confidence\\\":0.95,\\\"rationale\\\":\\\"<reason>\\\"}\"}],"
       << "\"stream\":false}";
    return ss.str();
}

AiModelRecommendation ModelAdapter::query_orchestration(
    const MediaMetadata& media_meta,
    const ByteCharacteristics& byte_chars,
    const std::string& objective,
    const std::vector<CodecId>& candidate_codecs) const {

    AiModelRecommendation rec;

    if (!config_.enabled) {
        rec.is_fallback = true;
        rec.fallback_reason = "AI orchestration disabled in configuration";
        rec.recommended_codec = Analyzer::recommend_codec(byte_chars, &rec.rationale);
        rec.codec_name = codec_id_to_name(rec.recommended_codec);
        rec.confidence = 0.90;
        return rec;
    }

    UrlParts parts = parse_url(config_.endpoint_url);
    std::string prompt_json = format_prompt(media_meta, byte_chars, objective, candidate_codecs);
    std::string resp_json;

    bool ok = send_http_post(parts.host, parts.port, parts.path, prompt_json, resp_json, config_.timeout_ms);

    if (!ok || resp_json.empty()) {
        rec.is_fallback = true;
        rec.fallback_reason = "Model endpoint unreachable or timed out (" + config_.endpoint_url + ")";
        rec.recommended_codec = Analyzer::recommend_codec(byte_chars, &rec.rationale);
        rec.codec_name = codec_id_to_name(rec.recommended_codec);
        rec.confidence = 0.85;
        return rec;
    }

    // Parse JSON response
    std::smatch m;
    std::regex codec_re("\"recommended_codec\"\\s*:\\s*\"([a-zA-Z0-9_-]+)\"");
    if (std::regex_search(resp_json, m, codec_re)) {
        std::string c_name = m[1].str();
        std::transform(c_name.begin(), c_name.end(), c_name.begin(), ::tolower);
        try {
            rec.recommended_codec = codec_name_to_id(c_name);
            rec.codec_name = c_name;
            rec.is_fallback = false;

            std::regex rat_re("\"rationale\"\\s*:\\s*\"([^\"]+)\"");
            if (std::regex_search(resp_json, m, rat_re)) {
                rec.rationale = m[1].str();
            } else {
                rec.rationale = "AI model recommended " + c_name;
            }
            rec.confidence = 0.92;
            return rec;
        } catch (...) {
            // Invalid codec name in response
        }
    }

    // Fallback if parsing failed
    rec.is_fallback = true;
    rec.fallback_reason = "AI model returned invalid or unparseable response";
    rec.recommended_codec = Analyzer::recommend_codec(byte_chars, &rec.rationale);
    rec.codec_name = codec_id_to_name(rec.recommended_codec);
    rec.confidence = 0.85;
    return rec;
}

} // namespace cacz
