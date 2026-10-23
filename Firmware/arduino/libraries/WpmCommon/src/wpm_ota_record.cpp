#include "wpm_ota_record.h"

#include <cstring>

namespace {

const uint8_t kMagic[4] = {0x4F, 0x54, 0x41, 0x31};  // "OTA1"

uint32_t read_le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void write_le32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

}  // namespace

bool wpm_ota_record_parse(const uint8_t raw[WPM_OTA_RECORD_LEN], WpmOtaRecord* out) {
  out->pending = std::memcmp(raw, kMagic, sizeof kMagic) == 0;
  out->len = read_le32(raw + 4);
  out->crc32 = read_le32(raw + 8);
  out->flags = raw[12];
  out->last_result = raw[13];
  out->app_crc32 = read_le32(raw + 14);
  return true;
}

void wpm_ota_record_serialize(const WpmOtaRecord* in, uint8_t raw[WPM_OTA_RECORD_LEN]) {
  if (in->pending) {
    std::memcpy(raw, kMagic, sizeof kMagic);
  } else {
    std::memset(raw, 0, 4);
  }
  write_le32(raw + 4, in->len);
  write_le32(raw + 8, in->crc32);
  raw[12] = in->flags;
  raw[13] = in->last_result;
  write_le32(raw + 14, in->app_crc32);
}

WpmOtaCheck wpm_ota_check_len(uint32_t len) {
  if (len == 0 || (len & 1u) != 0 || len > WPM_OTA_MAX_LEN) {
    return WPM_OTA_ERR_LEN;
  }
  return WPM_OTA_OK;
}

const uint8_t WPM_APP_MARKER_MAGIC[8] = {0x57, 0x50, 0x4D, 0x41, 0x50, 0x50, 0x31, 0x00};

WpmOtaCheck wpm_ota_check_head(const uint8_t head[WPM_OTA_HEAD_LEN], uint32_t len) {
  const uint32_t word0 = read_le32(head);
  const uint32_t word1 = read_le32(head + 4);
  const bool sp_ok = word0 >= 0x20000000u && word0 <= 0x20008000u;
  const bool reset_ok = (word1 & 1u) == 1u && word1 >= WPM_APP_FLASH_BASE &&
                        word1 < WPM_FLASH_END;
  if (!sp_ok || !reset_ok) return WPM_OTA_ERR_IMAGE;
  const uint8_t* m = head + WPM_APP_MARKER_OFF;
  if (std::memcmp(m, WPM_APP_MARKER_MAGIC, sizeof WPM_APP_MARKER_MAGIC) != 0) {
    return WPM_OTA_ERR_IMAGE;
  }
  if (read_le32(m + 8) != WPM_APP_FLASH_BASE) return WPM_OTA_ERR_IMAGE;
  if (len != 0 && read_le32(m + 12) != len) return WPM_OTA_ERR_IMAGE;
  return WPM_OTA_OK;
}

const char* wpm_ota_last_result_str(uint8_t v) {
  switch (v) {
    case 1:
      return "ok";
    case 2:
      return "crc_mismatch";
    case 3:
      return "image";
    default:
      return "none";
  }
}

WpmOtaCheck wpm_ota_commit_check(uint32_t len, const uint8_t head[WPM_OTA_HEAD_LEN],
                                 uint32_t crc_expected, uint32_t crc_actual, bool busy) {
  if (busy) return WPM_OTA_ERR_BUSY;
  WpmOtaCheck c = wpm_ota_check_len(len);
  if (c != WPM_OTA_OK) return c;
  c = wpm_ota_check_head(head, len);
  if (c != WPM_OTA_OK) return c;
  return crc_expected == crc_actual ? WPM_OTA_OK : WPM_OTA_ERR_CRC;
}

const char* wpm_ota_check_str(WpmOtaCheck c) {
  switch (c) {
    case WPM_OTA_OK:
      return "ok";
    case WPM_OTA_ERR_LEN:
      return "len";
    case WPM_OTA_ERR_IMAGE:
      return "image";
    case WPM_OTA_ERR_CRC:
      return "crc";
    case WPM_OTA_ERR_BUSY:
      return "busy";
  }
  return "image";
}
