// Minimal raw-TCP HTTP/1.1 POST for lwIP NO_SYS mode.
// Exists only because lwIP's bundled http_client is GET-only; used solely
// for the land command. One request in flight at a time.
#pragma once

#include <cstdint>
#include <cstddef>

// status: HTTP status code, or <0 on connect/timeout/transport failure.
// body points into an internal buffer, valid only during the callback.
typedef void (*http_post_cb)(int status, const char* body, size_t body_len);

bool http_post(const char* host_ip, uint16_t port, const char* path,
               const char* json_body, http_post_cb cb);

bool http_post_busy();
