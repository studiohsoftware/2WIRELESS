#include "wpm_version.h"

#include <WiFi101.h>

#include "wpm_ota_record.h"
#include "wpm_version_format.h"

// Defined in 2Wireless.ino.
uint8_t framRead(int addr);

bool wpm_loader_present() {
  const uint8_t* p = (const uint8_t*)0x2000;
  bool allFF = true;
  for (int i = 0; i < 8; i++) {
    if (p[i] != 0xFF) allFF = false;
  }
  if (allFF) return false;
  uint32_t sp = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  uint32_t rv = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
  return sp >= 0x20000000u && sp <= 0x20008000u && (rv & 1u) && rv >= 0x2000u && rv < 0x40000u;
}

const char* wpm_reset_cause_str() {
  uint8_t r = PM->RCAUSE.reg;
  if (r & (1u << 0)) return "POR";
  if (r & (1u << 1)) return "BOD";
  if (r & (1u << 2)) return "BOD";
  if (r & (1u << 4)) return "EXT";
  if (r & (1u << 5)) return "WDT";
  if (r & (1u << 6)) return "SYST";
  return "UNKNOWN";
}

String wpm_version_json(bool loaderPresent, uint8_t usbMode, uint8_t pollOnStart) {
  uint8_t raw[WPM_OTA_RECORD_LEN];
  for (size_t i = 0; i < WPM_OTA_RECORD_LEN; i++) raw[i] = framRead((int)(WPM_OTA_RECORD_ADDR + i));
  WpmOtaRecord rec = {};
  wpm_ota_record_parse(raw, &rec);

  WpmVersionInfo info;
  info.version = FW_VERSION;
  info.board = "mkr1000";
  info.loader = loaderPresent ? "fram-loader-1" : nullptr;
  info.reset_cause = wpm_reset_cause_str();
  info.uptime_s = millis() / 1000;
  info.rssi = (int)WiFi.RSSI();
  info.usbMode = usbMode;
  info.pollOnStart = pollOnStart;
  info.has_app_crc = (rec.last_result == 1);
  info.app_crc32 = rec.app_crc32;
#ifdef FW_BUILD_TAG
  info.build = FW_BUILD_TAG;
#else
  info.build = nullptr;
#endif

  char buf[352];
  wpm_version_format(buf, sizeof buf, info);
  return String(buf);
}
