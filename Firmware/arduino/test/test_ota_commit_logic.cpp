#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_ota_record.h"

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

static void mk_head(uint8_t h[WPM_OTA_HEAD_LEN], uint32_t mlen) {
  std::memset(h, 0, WPM_OTA_HEAD_LEN);
  put32(h, 0x20008000u);
  put32(h + 4, 0x00006B05u);
  std::memcpy(h + WPM_APP_MARKER_OFF, "WPMAPP1", 8);
  put32(h + WPM_APP_MARKER_OFF + 8, 0x6000u);
  put32(h + WPM_APP_MARKER_OFF + 12, mlen);
}

int main() {
  const uint32_t L = 256;
  uint8_t good[WPM_OTA_HEAD_LEN];
  uint8_t bad[WPM_OTA_HEAD_LEN];
  mk_head(good, L);
  std::memset(bad, 0xFF, sizeof bad);

  CHECK(wpm_ota_commit_check(L, good, 0x1234u, 0x1234u, false) == WPM_OTA_OK);
  CHECK(wpm_ota_commit_check(L, good, 0x1234u, 0x1235u, false) == WPM_OTA_ERR_CRC);
  CHECK(wpm_ota_commit_check(L, bad, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_IMAGE);
  CHECK(wpm_ota_commit_check(0, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(255, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(1001, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(0x2BD1Au, good, 0x1234u, 0x1234u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(L, good, 0x1234u, 0x1234u, true) == WPM_OTA_ERR_BUSY);

  // Marker length differs from len (both even, within max): image, reported before crc.
  CHECK(wpm_ota_commit_check(L + 2, good, 5u, 5u, false) == WPM_OTA_ERR_IMAGE);
  CHECK(wpm_ota_commit_check(L - 2, good, 1u, 2u, false) == WPM_OTA_ERR_IMAGE);

  // First-failure precedence: busy > len > image > crc.
  CHECK(wpm_ota_commit_check(0, bad, 1u, 2u, true) == WPM_OTA_ERR_BUSY);
  CHECK(wpm_ota_commit_check(L, bad, 1u, 2u, true) == WPM_OTA_ERR_BUSY);
  CHECK(wpm_ota_commit_check(0, bad, 1u, 2u, false) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_commit_check(L, bad, 1u, 2u, false) == WPM_OTA_ERR_IMAGE);
  CHECK(wpm_ota_commit_check(L, good, 1u, 2u, false) == WPM_OTA_ERR_CRC);

  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_OK), "ok") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_LEN), "len") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_IMAGE), "image") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_CRC), "crc") == 0);
  CHECK(std::strcmp(wpm_ota_check_str(WPM_OTA_ERR_BUSY), "busy") == 0);
  std::printf("test_ota_commit_logic: %d failures\n", minitest_failures);
  return minitest_failures;
}
