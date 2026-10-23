#include "wpm_version_format.h"

#include <stdio.h>

size_t wpm_version_format(char* out, size_t cap, const WpmVersionInfo& info) {
  char loader[48] = "";
  if (info.loader) snprintf(loader, sizeof loader, "\"loader\":\"%s\",", info.loader);
  char tag[48] = "";
  if (info.build && info.build[0]) snprintf(tag, sizeof tag, "\"build\":\"%s\",", info.build);
  char crc[16];
  if (info.has_app_crc) snprintf(crc, sizeof crc, "%08lX", (unsigned long)info.app_crc32);
  else snprintf(crc, sizeof crc, "unknown");
  int n = snprintf(out, cap,
                   "{\"version\":\"%s\",%s\"board\":\"%s\",%s"
                   "\"capabilities\":[\"version\",\"ota\",\"bus\",\"dump\"],\"getpresets_wait\":false,"
                   "\"usbMode\":%u,\"pollOnStart\":%u,\"reset_cause\":\"%s\","
                   "\"uptime_s\":%lu,\"rssi\":%d,\"tcp_sockets_free\":null,"
                   "\"app_crc32\":\"%s\"}",
                   info.version, tag, info.board, loader, (unsigned)info.usbMode,
                   (unsigned)info.pollOnStart, info.reset_cause,
                   (unsigned long)info.uptime_s, info.rssi, crc);
  return n < 0 ? 0 : (size_t)n;
}
