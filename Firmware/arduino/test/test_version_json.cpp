#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_version_format.h"

int main() {
  // Full fixture: loader present, app crc known.
  {
    WpmVersionInfo info = {"1.20", "mkr1000", "fram-loader-1", "POR", 42u, -50, 1, 0, true, 0x0A1B2C3Du, nullptr};
    const char* want =
        "{\"version\":\"1.20\",\"board\":\"mkr1000\",\"loader\":\"fram-loader-1\","
        "\"capabilities\":[\"version\",\"ota\",\"bus\"],\"getpresets_wait\":false,\"usbMode\":1,"
        "\"pollOnStart\":0,\"reset_cause\":\"POR\",\"uptime_s\":42,\"rssi\":-50,"
        "\"tcp_sockets_free\":null,\"app_crc32\":\"0A1B2C3D\"}";
    char buf[512];
    size_t n = wpm_version_format(buf, sizeof buf, info);
    CHECK(n == std::strlen(want));
    CHECK(std::strcmp(buf, want) == 0);

    // Truncation: returns the needed length, output stays NUL-terminated.
    char small[16];
    size_t m = wpm_version_format(small, sizeof small, info);
    CHECK(m == std::strlen(want));
    CHECK(std::strlen(small) == sizeof small - 1);
    CHECK(wpm_version_format(nullptr, 0, info) == std::strlen(want));
  }

  // No loader, unknown app crc: loader key absent, "unknown" crc.
  {
    WpmVersionInfo info = {"1.20", "mkr1000", nullptr, "WDT", 4000000000u, 0, 0, 1, false, 0xFFFFFFFFu, nullptr};
    const char* want =
        "{\"version\":\"1.20\",\"board\":\"mkr1000\","
        "\"capabilities\":[\"version\",\"ota\",\"bus\"],\"getpresets_wait\":false,\"usbMode\":0,"
        "\"pollOnStart\":1,\"reset_cause\":\"WDT\",\"uptime_s\":4000000000,\"rssi\":0,"
        "\"tcp_sockets_free\":null,\"app_crc32\":\"unknown\"}";
    char buf[512];
    wpm_version_format(buf, sizeof buf, info);
    CHECK(std::strcmp(buf, want) == 0);
  }
  // Tagged build: "build" key right after "version"; empty tag is omitted.
  {
    WpmVersionInfo info = {"1.20", "mkr1000", "fram-loader-1", "POR", 42u, -50, 1, 0, true, 0x0A1B2C3Du, "b"};
    const char* want =
        "{\"version\":\"1.20\",\"build\":\"b\",\"board\":\"mkr1000\",\"loader\":\"fram-loader-1\","
        "\"capabilities\":[\"version\",\"ota\",\"bus\"],\"getpresets_wait\":false,\"usbMode\":1,"
        "\"pollOnStart\":0,\"reset_cause\":\"POR\",\"uptime_s\":42,\"rssi\":-50,"
        "\"tcp_sockets_free\":null,\"app_crc32\":\"0A1B2C3D\"}";
    char buf[512];
    wpm_version_format(buf, sizeof buf, info);
    CHECK(std::strcmp(buf, want) == 0);
    info.build = "";
    wpm_version_format(buf, sizeof buf, info);
    CHECK(std::strstr(buf, "\"build\"") == nullptr);
  }
  std::printf("test_version_json: %d failures\n", minitest_failures);
  return minitest_failures;
}
