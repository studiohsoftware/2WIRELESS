#pragma once
// Formatter for the GET /version JSON body. Pure C++ (snprintf only) so it is
// testable on the desktop.
#include <stddef.h>
#include <stdint.h>

struct WpmVersionInfo {
  const char* version;
  const char* board;
  const char* loader;       // nullptr = no loader present: "loader" key omitted
  const char* reset_cause;
  uint32_t uptime_s;
  int rssi;
  uint8_t usbMode;
  uint8_t pollOnStart;
  bool has_app_crc;         // false: "app_crc32":"unknown"
  uint32_t app_crc32;
  const char* build;        // nullptr/"" = no build tag: "build" key omitted
};

// Writes the JSON into out (NUL-terminated, truncated to cap-1 chars). Returns
// the number of chars the full JSON needs (snprintf semantics); cap may be 0.
size_t wpm_version_format(char* out, size_t cap, const WpmVersionInfo& info);
