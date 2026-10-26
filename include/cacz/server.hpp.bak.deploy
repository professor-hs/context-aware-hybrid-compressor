#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cacz {

// Safe filename sanitizer for uploaded files & download headers
std::string sanitize_filename(std::string_view raw_name);

// Standard HTTP status reason phrases
const char* http_status_reason(int status_code) noexcept;

// Start native C++ HTTP server
int run_http_server(uint16_t port = 8080);

} // namespace cacz
