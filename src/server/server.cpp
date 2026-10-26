#include "cacz/server.hpp"
#include "cacz/common.hpp"
#include "cacz/codec_registry.hpp"
#include "cacz/container.hpp"
#include "cacz/analyzer.hpp"
#include "cacz/media_detector.hpp"
#include "cacz/orchestrator.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>
#include <chrono>
#include <random>
#include <algorithm>
#include <unordered_map>
#include <cctype>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace cacz {

// Configurable upload size limit: 100 MB
constexpr uint64_t MAX_UPLOAD_SIZE = 100ULL * 1024ULL * 1024ULL;
constexpr size_t MAX_HEADER_SIZE = 64 * 1024; // 64 KB header limit

const char* http_status_reason(int status_code) noexcept {
    switch (status_code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

std::string sanitize_filename(std::string_view raw_name) {
    std::string s;
    s.reserve(raw_name.size());

    // 1. URL decode %XX
    for (size_t i = 0; i < raw_name.size(); ++i) {
        if (raw_name[i] == '%' && i + 2 < raw_name.size() &&
            std::isxdigit(static_cast<unsigned char>(raw_name[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(raw_name[i + 2]))) {
            std::string hex_str(raw_name.substr(i + 1, 2));
            char decoded = static_cast<char>(std::strtol(hex_str.c_str(), nullptr, 16));
            s.push_back(decoded);
            i += 2;
        } else {
            s.push_back(raw_name[i]);
        }
    }

    // 2. Strip paths (keep only filename component)
    size_t last_sep = s.find_last_of("/\\");
    if (last_sep != std::string::npos) {
        s = s.substr(last_sep + 1);
    }

    // 3. Remove dangerous / illegal characters
    std::string clean;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            clean.push_back('_');
        } else {
            clean.push_back(c);
        }
    }

    // Trim trailing dots and spaces
    while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) {
        clean.pop_back();
    }

    if (clean.empty()) {
        clean = "upload.bin";
    }
    return clean;
}

namespace {

struct TempFileGuard {
    std::vector<std::filesystem::path> paths;

    ~TempFileGuard() {
        for (const auto& p : paths) {
            std::error_code ec;
            std::filesystem::remove(p, ec);
        }
    }

    void track(const std::filesystem::path& p) {
        paths.push_back(p);
    }
};

std::filesystem::path create_temp_path(std::string_view prefix, std::string_view ext) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    std::string filename = std::string(prefix) + "_" + std::to_string(now) + "_" + std::to_string(rd()) + std::string(ext);

    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    return temp_dir / filename;
}

bool send_all(SOCKET client, const char* data, size_t total_bytes) {
    size_t sent = 0;
    while (sent < total_bytes) {
        int r = send(client, data + sent, static_cast<int>(total_bytes - sent), 0);
        if (r <= 0) {
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

void send_response(
    SOCKET client,
    int status_code,
    const std::string& content_type,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {

    std::ostringstream ss;
    ss << "HTTP/1.1 " << status_code << " " << http_status_reason(status_code) << "\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type, Content-Length, X-Filename, X-Objective, X-Oracle, X-AI, X-Requested-With\r\n"
       << "Access-Control-Expose-Headers: Content-Disposition, X-CACZ-Original-Size, X-CACZ-Archive-Size, X-CACZ-Compression-Ratio, X-CACZ-Chunk-Count, X-CACZ-Verified, X-CACZ-Codec\r\n";

    for (const auto& [k, v] : extra_headers) {
        ss << k << ": " << v << "\r\n";
    }

    ss << "Connection: close\r\n\r\n";
    std::string headers = ss.str();

    if (send_all(client, headers.data(), headers.size())) {
        if (!body.empty()) {
            send_all(client, body.data(), body.size());
        }
    }
}

void to_lower_inplace(std::string& s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

void handle_client(SOCKET client) {
    // 1. Read HTTP request headers until \r\n\r\n
    std::vector<char> header_buf;
    header_buf.reserve(8192);

    char chunk[4096];
    size_t header_end_pos = std::string::npos;

    while (header_buf.size() < MAX_HEADER_SIZE) {
        int r = recv(client, chunk, sizeof(chunk), 0);
        if (r <= 0) {
            closesocket(client);
            return;
        }

        size_t prev_size = header_buf.size();
        header_buf.insert(header_buf.end(), chunk, chunk + r);

        // Search for "\r\n\r\n"
        std::string_view view(header_buf.data(), header_buf.size());
        size_t search_start = (prev_size >= 3) ? (prev_size - 3) : 0;
        size_t pos = view.find("\r\n\r\n", search_start);
        if (pos != std::string_view::npos) {
            header_end_pos = pos;
            break;
        }
    }

    if (header_end_pos == std::string::npos) {
        send_response(client, 431, "application/json", "{\"error\":\"Header size exceeds limit\"}");
        closesocket(client);
        return;
    }

    std::string header_str(header_buf.data(), header_end_pos);
    size_t body_start_offset = header_end_pos + 4;
    size_t initial_body_bytes = header_buf.size() - body_start_offset;

    // 2. Parse request line and headers
    std::istringstream stream(header_str);
    std::string request_line;
    if (!std::getline(stream, request_line)) {
        closesocket(client);
        return;
    }

    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::string method, path, proto;
    std::istringstream rl_stream(request_line);
    rl_stream >> method >> path >> proto;

    std::unordered_map<std::string, std::string> headers;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(val.begin());
            to_lower_inplace(key);
            headers[key] = val;
        }
    }

    // 3. Handle OPTIONS for CORS preflight
    if (method == "OPTIONS") {
        send_response(client, 204, "text/plain", "");
        closesocket(client);
        return;
    }

    register_builtin_codecs();

    // 4. GET endpoints
    if (method == "GET" && path == "/api/health") {
        std::string json = "{\"status\":\"healthy\",\"engine\":\"CACZ Native C++20\",\"version\":\"2.0.0\"}";
        send_response(client, 200, "application/json", json);
        closesocket(client);
        return;
    }

    if (method == "GET" && path == "/api/codecs") {
        auto codecs = CodecRegistry::instance().all();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < codecs.size(); ++i) {
            const auto* c = codecs[i];
            ss << "{\"id\":" << static_cast<int>(c->id())
               << ",\"name\":\"" << c->name() << "\""
               << ",\"description\":\"" << c->metadata().description << "\""
               << ",\"lossless\":" << (c->is_lossless() ? "true" : "false") << "}";
            if (i + 1 < codecs.size()) ss << ",";
        }
        ss << "]";
        send_response(client, 200, "application/json", ss.str());
        closesocket(client);
        return;
    }

    if (method == "GET" && path == "/api/model/status") {
        ModelAdapter adapter;
        bool ok = adapter.check_availability();
        std::string json = "{\"available\":" + std::string(ok ? "true" : "false") +
                           ",\"endpoint\":\"" + adapter.config().endpoint_url + "\"}";
        send_response(client, 200, "application/json", json);
        closesocket(client);
        return;
    }

    // 5. POST /api/orchestrate
    if (method == "POST" && path == "/api/orchestrate") {
        uint64_t content_len = 0;
        auto cl_it = headers.find("content-length");
        if (cl_it != headers.end()) {
            try { content_len = std::stoull(cl_it->second); } catch (...) { content_len = 0; }
        }

        std::string body;
        if (content_len > 0 && content_len <= 1048576) {
            body.assign(header_buf.data() + body_start_offset, initial_body_bytes);
            while (body.size() < content_len) {
                int r = recv(client, chunk, sizeof(chunk), 0);
                if (r <= 0) break;
                body.append(chunk, r);
            }
        }

        std::string target_file = "benchmark/test_files/english_text.txt";
        size_t file_idx = body.find("\"file\":\"");
        if (file_idx != std::string::npos) {
            size_t end_idx = body.find("\"", file_idx + 8);
            if (end_idx != std::string::npos) {
                target_file = body.substr(file_idx + 8, end_idx - (file_idx + 8));
            }
        }

        std::string objective = "balanced";
        size_t obj_idx = body.find("\"objective\":\"");
        if (obj_idx != std::string::npos) {
            size_t end_idx = body.find("\"", obj_idx + 13);
            if (end_idx != std::string::npos) {
                objective = body.substr(obj_idx + 13, end_idx - (obj_idx + 13));
            }
        }

        try {
            Orchestrator orch;
            OrchestrationPlan plan = orch.plan(target_file, objective_from_name(objective));

            std::ostringstream ss;
            ss << "{"
               << "\"file\":\"" << target_file << "\""
               << ",\"file_size\":" << plan.file_size
               << ",\"media\":{"
               << "\"format\":\"" << plan.media_metadata.format_name << "\""
               << ",\"mime\":\"" << plan.media_metadata.mime_type << "\""
               << ",\"summary\":\"" << plan.media_metadata.summary << "\""
               << ",\"is_already_compressed\":" << (plan.media_metadata.is_already_compressed ? "true" : "false")
               << "},"
               << "\"characteristics\":{"
               << "\"entropy\":" << plan.byte_characteristics.entropy
               << ",\"unique_bytes\":" << plan.byte_characteristics.unique_bytes
               << ",\"run_length_ratio\":" << plan.byte_characteristics.run_length_ratio
               << ",\"repeated_4gram_ratio\":" << plan.byte_characteristics.repeated_4gram_ratio
               << ",\"delta_variance\":" << plan.byte_characteristics.delta_variance
               << "},"
               << "\"ai_recommendation\":{"
               << "\"recommended_codec\":\"" << plan.ai_recommendation.codec_name << "\""
               << ",\"confidence\":" << plan.ai_recommendation.confidence
               << ",\"rationale\":\"" << plan.ai_recommendation.rationale << "\""
               << ",\"is_fallback\":" << (plan.ai_recommendation.is_fallback ? "true" : "false")
               << "},"
               << "\"chosen_codec\":\"" << plan.chosen_codec_name << "\""
               << ",\"strategy_rationale\":\"" << plan.strategy_rationale << "\""
               << ",\"candidates\":[";

            for (size_t i = 0; i < plan.candidates.size(); ++i) {
                const auto& c = plan.candidates[i];
                ss << "{\"codec\":\"" << c.codec_name << "\""
                   << ",\"compression_ratio\":" << c.compression_ratio
                   << ",\"throughput_mb_s\":" << c.throughput_mb_s
                   << ",\"score\":" << c.score
                   << ",\"is_valid\":" << (c.is_valid ? "true" : "false")
                   << ",\"note\":\"" << c.validation_note << "\"}";
                if (i + 1 < plan.candidates.size()) ss << ",";
            }
            ss << "]}";

            send_response(client, 200, "application/json", ss.str());
        } catch (const std::exception& e) {
            std::string err = "{\"error\":\"" + std::string(e.what()) + "\"}";
            send_response(client, 500, "application/json", err);
        }

        closesocket(client);
        return;
    }

    // 6. Real Binary File Compression: POST /api/compress
    if (method == "POST" && path == "/api/compress") {
        auto cl_it = headers.find("content-length");
        if (cl_it == headers.end()) {
            send_response(client, 411, "application/json", "{\"error\":\"Content-Length header required for upload\"}");
            closesocket(client);
            return;
        }

        uint64_t content_length = 0;
        try {
            content_length = std::stoull(cl_it->second);
        } catch (...) {
            send_response(client, 400, "application/json", "{\"error\":\"Invalid Content-Length header\"}");
            closesocket(client);
            return;
        }

        if (content_length > MAX_UPLOAD_SIZE) {
            send_response(client, 413, "application/json", "{\"error\":\"Uploaded file exceeds maximum limit of 100 MB\"}");
            closesocket(client);
            return;
        }

        // Sanitize filename
        std::string raw_filename;
        auto fn_it = headers.find("x-filename");
        if (fn_it != headers.end() && !fn_it->second.empty()) {
            raw_filename = fn_it->second;
        } else {
            raw_filename = "compressed_file.bin";
        }
        std::string safe_basename = sanitize_filename(raw_filename);

        // Options from headers
        CompressionOptions options;
        auto oracle_it = headers.find("x-oracle");
        if (oracle_it != headers.end() && (oracle_it->second == "1" || oracle_it->second == "true")) {
            options.oracle_search = true;
        }
        auto ai_it = headers.find("x-ai");
        if (ai_it != headers.end() && (ai_it->second == "1" || ai_it->second == "true")) {
            options.ai_orchestration = true;
        }
        auto obj_it = headers.find("x-objective");
        if (obj_it != headers.end() && !obj_it->second.empty()) {
            options.objective = objective_from_name(obj_it->second);
            if (options.objective == Objective::BestRatio) {
                options.oracle_search = true;
            }
        }

        // Create temporary paths with RAII guard
        TempFileGuard guard;
        std::filesystem::path temp_in = create_temp_path("cacz_upload", ".in");
        std::filesystem::path temp_out = create_temp_path("cacz_output", ".cacz");
        guard.track(temp_in);
        guard.track(temp_out);

        // Stream body into temp_in
        std::ofstream in_file(temp_in, std::ios::binary | std::ios::trunc);
        if (!in_file.is_open()) {
            send_response(client, 500, "application/json", "{\"error\":\"Failed to create temporary upload staging file\"}");
            closesocket(client);
            return;
        }

        // Write bytes already read in header_buf
        size_t written_so_far = 0;
        if (initial_body_bytes > 0) {
            size_t bytes_to_write = std::min<size_t>(initial_body_bytes, static_cast<size_t>(content_length));
            in_file.write(header_buf.data() + body_start_offset, static_cast<std::streamsize>(bytes_to_write));
            written_so_far = bytes_to_write;
        }

        // Read remaining body from socket
        bool read_error = false;
        while (written_so_far < content_length) {
            size_t to_read = std::min<size_t>(sizeof(chunk), static_cast<size_t>(content_length - written_so_far));
            int r = recv(client, chunk, static_cast<int>(to_read), 0);
            if (r <= 0) {
                read_error = true;
                break;
            }
            in_file.write(chunk, r);
            written_so_far += static_cast<size_t>(r);
        }
        in_file.close();

        if (read_error || written_so_far < content_length) {
            send_response(client, 400, "application/json", "{\"error\":\"Connection closed prematurely before receiving full request body\"}");
            closesocket(client);
            return;
        }

        // Real compression with C++ Engine
        try {
            Container::compress_file(temp_in, temp_out, options);
        } catch (const std::exception& e) {
            std::string err = "{\"error\":\"Compression engine failed: " + std::string(e.what()) + "\"}";
            send_response(client, 500, "application/json", err);
            closesocket(client);
            return;
        }

        // Real inspection and verification
        ArchiveInspection rep;
        try {
            rep = Container::inspect(temp_out);
        } catch (const std::exception& e) {
            std::string err = "{\"error\":\"Generated archive inspection failed: " + std::string(e.what()) + "\"}";
            send_response(client, 500, "application/json", err);
            closesocket(client);
            return;
        }

        std::string verify_err;
        bool verified = Container::verify(temp_out, &verify_err);
        if (!verified) {
            std::string err = "{\"error\":\"Generated archive failed CRC32 integrity verification: " + verify_err + "\"}";
            send_response(client, 500, "application/json", err);
            closesocket(client);
            return;
        }

        // Determine primary codec used
        std::string primary_codec = "raw";
        size_t max_chunks = 0;
        for (const auto& [cname, cnt] : rep.codec_chunk_counts) {
            if (cnt > max_chunks) {
                max_chunks = cnt;
                primary_codec = cname;
            }
        }

        // Read archive file and send binary response
        std::ifstream archive_file(temp_out, std::ios::binary);
        if (!archive_file.is_open()) {
            send_response(client, 500, "application/json", "{\"error\":\"Failed to open generated archive for streaming\"}");
            closesocket(client);
            return;
        }

        archive_file.seekg(0, std::ios::end);
        uint64_t archive_size = static_cast<uint64_t>(archive_file.tellg());
        archive_file.seekg(0, std::ios::beg);

        std::string download_name = safe_basename + ".cacz";

        std::vector<std::pair<std::string, std::string>> headers_out = {
            {"Content-Disposition", "attachment; filename=\"" + download_name + "\""},
            {"X-CACZ-Original-Size", std::to_string(rep.header.original_size)},
            {"X-CACZ-Archive-Size", std::to_string(archive_size)},
            {"X-CACZ-Compression-Ratio", std::to_string(rep.overall_ratio)},
            {"X-CACZ-Chunk-Count", std::to_string(rep.header.total_chunks)},
            {"X-CACZ-Verified", "true"},
            {"X-CACZ-Codec", primary_codec}
        };

        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/octet-stream\r\n"
           << "Content-Length: " << archive_size << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type, Content-Length, X-Filename, X-Objective, X-Oracle, X-AI, X-Requested-With\r\n"
           << "Access-Control-Expose-Headers: Content-Disposition, X-CACZ-Original-Size, X-CACZ-Archive-Size, X-CACZ-Compression-Ratio, X-CACZ-Chunk-Count, X-CACZ-Verified, X-CACZ-Codec\r\n";

        for (const auto& [k, v] : headers_out) {
            ss << k << ": " << v << "\r\n";
        }
        ss << "Connection: close\r\n\r\n";

        std::string headers_str = ss.str();
        if (!send_all(client, headers_str.data(), headers_str.size())) {
            closesocket(client);
            return;
        }

        // Stream binary archive content
        while (archive_file) {
            archive_file.read(chunk, sizeof(chunk));
            std::streamsize bytes = archive_file.gcount();
            if (bytes <= 0) break;
            if (!send_all(client, chunk, static_cast<size_t>(bytes))) {
                break;
            }
        }
        archive_file.close();
        closesocket(client);
        return;
    }

    // 7. Verification Endpoint: POST /api/verify
    if (method == "POST" && path == "/api/verify") {
        auto cl_it = headers.find("content-length");
        if (cl_it == headers.end()) {
            send_response(client, 411, "application/json", "{\"error\":\"Content-Length header required\"}");
            closesocket(client);
            return;
        }

        uint64_t content_length = 0;
        try { content_length = std::stoull(cl_it->second); } catch (...) { content_length = 0; }

        if (content_length > MAX_UPLOAD_SIZE) {
            send_response(client, 413, "application/json", "{\"error\":\"Archive exceeds 100 MB limit\"}");
            closesocket(client);
            return;
        }

        TempFileGuard guard;
        std::filesystem::path temp_arc = create_temp_path("cacz_verify", ".cacz");
        guard.track(temp_arc);

        std::ofstream arc_file(temp_arc, std::ios::binary | std::ios::trunc);
        if (!arc_file.is_open()) {
            send_response(client, 500, "application/json", "{\"error\":\"Failed to create temporary archive file\"}");
            closesocket(client);
            return;
        }

        size_t written_so_far = 0;
        if (initial_body_bytes > 0) {
            size_t bytes_to_write = std::min<size_t>(initial_body_bytes, static_cast<size_t>(content_length));
            arc_file.write(header_buf.data() + body_start_offset, static_cast<std::streamsize>(bytes_to_write));
            written_so_far = bytes_to_write;
        }

        while (written_so_far < content_length) {
            size_t to_read = std::min<size_t>(sizeof(chunk), static_cast<size_t>(content_length - written_so_far));
            int r = recv(client, chunk, static_cast<int>(to_read), 0);
            if (r <= 0) break;
            arc_file.write(chunk, r);
            written_so_far += static_cast<size_t>(r);
        }
        arc_file.close();

        std::string verify_err;
        bool valid = Container::verify(temp_arc, &verify_err);
        if (valid) {
            ArchiveInspection rep = Container::inspect(temp_arc);
            std::ostringstream ss;
            ss << "{\"valid\":true"
               << ",\"version\":" << rep.header.version
               << ",\"mode\":\"" << rep.mode_name << "\""
               << ",\"original_size\":" << rep.header.original_size
               << ",\"archive_size\":" << rep.archive_size
               << ",\"compression_ratio\":" << rep.overall_ratio
               << ",\"chunks\":" << rep.header.total_chunks
               << "}";
            send_response(client, 200, "application/json", ss.str());
        } else {
            std::ostringstream ss;
            ss << "{\"valid\":false,\"error\":\"" << verify_err << "\"}";
            send_response(client, 200, "application/json", ss.str());
        }

        closesocket(client);
        return;
    }

    // Default 404
    send_response(client, 404, "application/json", "{\"error\":\"Endpoint not found\"}");
    closesocket(client);
}

} // namespace

int run_http_server(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to bind server socket to port " << port << "\n";
        closesocket(server_sock);
        return 1;
    }

    if (listen(server_sock, 10) != 0) {
        std::cerr << "Failed to listen on server socket\n";
        closesocket(server_sock);
        return 1;
    }

    std::cout << "CACZ Native C++20 HTTP Server listening on 0.0.0.0:" << port << "\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_sock != INVALID_SOCKET) {
            std::thread([client_sock]() {
                handle_client(client_sock);
            }).detach();
        }
    }

    closesocket(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

} // namespace cacz
