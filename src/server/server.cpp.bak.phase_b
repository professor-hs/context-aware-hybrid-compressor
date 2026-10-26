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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define closesocket close
#endif

namespace cacz {

namespace {

void send_response(
#ifdef _WIN32
    SOCKET client,
#else
    int client,
#endif
    int status_code,
    const std::string& content_type,
    const std::string& body) {

    std::ostringstream ss;
    ss << "HTTP/1.1 " << status_code << " OK\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n\r\n"
       << body;

    std::string resp = ss.str();
    send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
}

void handle_client(
#ifdef _WIN32
    SOCKET client
#else
    int client
#endif
) {
    std::vector<char> buffer(65536);
    int received = recv(client, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
    if (received <= 0) {
        closesocket(client);
        return;
    }
    buffer[received] = '\0';
    std::string req(buffer.data(), received);

    // Parse method and path
    std::istringstream stream(req);
    std::string method, path;
    stream >> method >> path;

    if (method == "OPTIONS") {
        send_response(client, 200, "text/plain", "");
        closesocket(client);
        return;
    }

    register_builtin_codecs();

    if (method == "GET" && path == "/api/health") {
        std::string json = "{\"status\":\"healthy\",\"engine\":\"CACZ Native C++20\",\"version\":\"2.0.0\"}";
        send_response(client, 200, "application/json", json);
    }
    else if (method == "GET" && path == "/api/codecs") {
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
    }
    else if (method == "GET" && path == "/api/model/status") {
        ModelAdapter adapter;
        bool ok = adapter.check_availability();
        std::string json = "{\"available\":" + std::string(ok ? "true" : "false") +
                           ",\"endpoint\":\"" + adapter.config().endpoint_url + "\"}";
        send_response(client, 200, "application/json", json);
    }
    else if (method == "POST" && path == "/api/orchestrate") {
        // Find body
        size_t body_pos = req.find("\r\n\r\n");
        std::string body = (body_pos != std::string::npos) ? req.substr(body_pos + 4) : "";

        // Check for filepath parameter or sample data
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
    }
    else {
        send_response(client, 404, "application/json", "{\"error\":\"Endpoint not found\"}");
    }

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

#ifdef _WIN32
    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
#else
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
#endif
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

    std::cout << "CACZ Native C++20 HTTP Server listening at http://127.0.0.1:" << port << "\n";

    while (true) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
#ifdef _WIN32
        SOCKET client_sock = accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_sock != INVALID_SOCKET) {
#else
        int client_sock = accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), reinterpret_cast<socklen_t*>(&client_len));
        if (client_sock >= 0) {
#endif
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
