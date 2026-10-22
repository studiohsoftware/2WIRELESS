#pragma once
// OTA pending record (18 bytes in reserved FRAM) and image sanity checks.
// Pure functions, no Arduino dependencies, so they run on the desktop.
#include <cstddef>
#include <cstdint>

constexpr uint32_t WPM_OTA_RECORD_ADDR = 0x02BE50;
constexpr size_t WPM_OTA_RECORD_LEN = 18;
constexpr uint32_t WPM_OTA_MAX_LEN = 0x2BD18;
constexpr uint32_t WPM_APP_FLASH_BASE = 0x6000;
constexpr uint32_t WPM_FLASH_END = 0x40000;
constexpr uint32_t WPM_LOADER_BLOB_LEN = 0x4000;

// Raw layout (little-endian fields):
//   [0..3] magic "OTA1" when pending, else zeros; [4..7] len; [8..11] crc32;
//   [12] flags; [13] last_result; [14..17] app_crc32.
// last_result: 0 none, 1 ok, 2 crc_mismatch, 3 image.
struct WpmOtaRecord {
  uint32_t len;
  uint32_t crc32;
  uint8_t flags;
  uint8_t last_result;
  uint32_t app_crc32;
  bool pending;
};

// Never rejects: fields are read regardless of the magic. pending is true only
// when the magic matches exactly. Always returns true.
bool wpm_ota_record_parse(const uint8_t raw[WPM_OTA_RECORD_LEN], WpmOtaRecord* out);

// Magic is written iff in->pending; otherwise the first four bytes are zero.
void wpm_ota_record_serialize(const WpmOtaRecord* in, uint8_t raw[WPM_OTA_RECORD_LEN]);

enum WpmOtaCheck { WPM_OTA_OK, WPM_OTA_ERR_LEN, WPM_OTA_ERR_IMAGE, WPM_OTA_ERR_CRC, WPM_OTA_ERR_BUSY };

WpmOtaCheck wpm_ota_check_len(uint32_t len);

constexpr size_t WPM_OTA_HEAD_LEN = 44;      // SP, reset, ..., marker: what the checks need
constexpr size_t WPM_APP_MARKER_OFF = 0x1C;  // vector entries 7..10 (reserved on ARMv6-M)
constexpr size_t WPM_APP_MARKER_LEN = 16;
extern const uint8_t WPM_APP_MARKER_MAGIC[8];  // "WPMAPP1\0"

// Image head sanity: word 0 in 0x20000000..0x20008000, word 1 odd and in [WPM_APP_FLASH_BASE,
// WPM_FLASH_END); marker magic; marker base == WPM_APP_FLASH_BASE; and, when len != 0, marker
// length == len (len 0, never a valid image length, skips the length comparison: the flash check
// at boot has no record to compare against). Anything else -> WPM_OTA_ERR_IMAGE.
WpmOtaCheck wpm_ota_check_head(const uint8_t head[WPM_OTA_HEAD_LEN], uint32_t len);

// 0 -> "none", 1 -> "ok", 2 -> "crc_mismatch", 3 -> "image", anything else -> "none".
const char* wpm_ota_last_result_str(uint8_t v);

// /ota/commit decision: first failing check in the order busy, len, head (with len), crc.
// head = first WPM_OTA_HEAD_LEN bytes of the staged image (FRAM 0).
WpmOtaCheck wpm_ota_commit_check(uint32_t len, const uint8_t head[WPM_OTA_HEAD_LEN],
                                 uint32_t crc_expected, uint32_t crc_actual, bool busy);

// "ok" for WPM_OTA_OK, else "len" | "image" | "crc" | "busy".
const char* wpm_ota_check_str(WpmOtaCheck c);
