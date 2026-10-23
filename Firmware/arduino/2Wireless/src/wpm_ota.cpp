#include "wpm_ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wpm_crc32.h"
#include "wpm_bus.h"
#include "wpm_json.h"
#include "wpm_ota_record.h"
#include "wpm_version.h"

// Defined in 2Wireless.ino.
uint8_t framRead(int addr);
void framWrite(int addr, uint8_t value);

bool wpm_ota_busy() { return wpm_bus_scan_active(); }

static void readRecord(uint8_t raw[WPM_OTA_RECORD_LEN], WpmOtaRecord* rec) {
  for (size_t i = 0; i < WPM_OTA_RECORD_LEN; i++) raw[i] = framRead((int)(WPM_OTA_RECORD_ADDR + i));
  wpm_ota_record_parse(raw, rec);
}

void wpm_ota_handle_status(WiFiClient& client) {
  uint8_t raw[WPM_OTA_RECORD_LEN];
  WpmOtaRecord rec = {};
  readRecord(raw, &rec);

  String body = "{";
  if (wpm_loader_present()) body += "\"loader\":\"fram-loader-1\",";
  body += "\"pending\":";
  body += rec.pending ? "true" : "false";
  body += ",\"last\":\"";
  body += wpm_ota_last_result_str(rec.last_result);
  body += "\",\"app_crc32\":\"";
  if (rec.last_result == 1) {
    char hex[9];
    snprintf(hex, sizeof hex, "%08lX", (unsigned long)rec.app_crc32);
    body += hex;
  } else {
    body += "unknown";
  }
  body += "\"}";
  wpm_send_json(client, body);
}

void wpm_ota_handle_abort(WiFiClient& client) {
  // Clear the magic first (nothing pending), then reset last_result to none (spec: `last`
  // persists until the next commit or abort). app_crc32 is kept but hidden while last != ok.
  for (int i = 0; i < 4; i++) framWrite((int)(WPM_OTA_RECORD_ADDR + i), 0x00);
  framWrite((int)(WPM_OTA_RECORD_ADDR + 13), 0x00);
  wpm_send_json(client, "{\"ok\":true}");
}

// Text after "key=" within the query part of the URL; false when absent.
static bool queryValue(const String& url, const char* key, String* out) {
  int q = url.indexOf('?');
  if (q < 0) return false;
  int k = url.indexOf(key, q);
  if (k < 0) return false;
  *out = url.substring(k + strlen(key));
  return true;
}

void wpm_ota_handle_commit(WiFiClient& client, const String& urlString) {
  String v;
  uint32_t len = 0;
  uint32_t crcExpected = 0;
  bool haveCrc = false;
  if (queryValue(urlString, "len=", &v)) len = (uint32_t)strtoul(v.c_str(), nullptr, 10);
  if (queryValue(urlString, "crc32=", &v)) {
    crcExpected = (uint32_t)strtoul(v.c_str(), nullptr, 16);
    haveCrc = true;
  }

  // Order: busy, len, image (head incl. marker), crc. The slow CRC pass runs only when the
  // cheap checks pass, so the pure decision is made twice: once without the CRC
  // (crc_actual := crc_expected) and once with the computed value.
  uint8_t head[WPM_OTA_HEAD_LEN];
  for (size_t i = 0; i < WPM_OTA_HEAD_LEN; i++) head[i] = framRead((int)i);
  WpmOtaCheck c = wpm_ota_commit_check(len, head, crcExpected, crcExpected, wpm_ota_busy());
  if (c == WPM_OTA_OK) {
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t buf[256];
    for (uint32_t off = 0; off < len; off += sizeof buf) {
      uint32_t n = len - off;
      if (n > sizeof buf) n = sizeof buf;
      for (uint32_t i = 0; i < n; i++) buf[i] = framRead((int)(off + i));
      crc = wpm_crc32_update(crc, buf, n);
    }
    uint32_t crcActual = crc ^ 0xFFFFFFFFu;
    if (!haveCrc) crcActual = ~crcExpected;  // a missing crc32= can never match
    c = wpm_ota_commit_check(len, head, crcExpected, crcActual, false);
  }
  if (c != WPM_OTA_OK) {
    wpm_send_json(client, wpm_json_err(wpm_ota_check_str(c)));
    return;
  }

  // New pending record: flags (the loader's retry counter) and last_result start at 0 so a
  // stale "ok" can never be mistaken for this commit's outcome; app_crc32 is carried over.
  uint8_t raw[WPM_OTA_RECORD_LEN];
  WpmOtaRecord rec = {};
  readRecord(raw, &rec);
  rec.len = len;
  rec.crc32 = crcExpected;
  rec.flags = 0;
  rec.last_result = 0;
  rec.pending = true;
  wpm_ota_record_serialize(&rec, raw);
  // Body first, magic last: a power cut mid-write never leaves a half record pending.
  for (size_t i = 4; i < WPM_OTA_RECORD_LEN; i++) framWrite((int)(WPM_OTA_RECORD_ADDR + i), raw[i]);
  for (size_t i = 0; i < 4; i++) framWrite((int)(WPM_OTA_RECORD_ADDR + i), raw[i]);

  char hex[9];
  snprintf(hex, sizeof hex, "%08lX", (unsigned long)crcExpected);
  String body = String("{\"ok\":true,\"pending\":{\"len\":") + (unsigned long)len + ",\"crc32\":\"" + hex + "\"}}";
  wpm_send_json(client, body);
  client.flush();
  client.stop();
  delay(300);
  NVIC_SystemReset();
}
