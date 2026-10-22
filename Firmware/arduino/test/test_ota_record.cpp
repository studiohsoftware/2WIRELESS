#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_ota_record.h"

static bool eq_rec(const WpmOtaRecord& a, const WpmOtaRecord& b) {
  return a.len == b.len && a.crc32 == b.crc32 && a.flags == b.flags &&
         a.last_result == b.last_result && a.app_crc32 == b.app_crc32 &&
         a.pending == b.pending;
}

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

int main() {
  // Round trip with pending=true: magic and little-endian len on the wire.
  {
    WpmOtaRecord in = {0x1234u, 0xCBF43926u, 0x01, 0x01, 0xDEADBEEFu, true};
    uint8_t raw[WPM_OTA_RECORD_LEN];
    wpm_ota_record_serialize(&in, raw);
    CHECK(raw[0] == 0x4F && raw[1] == 0x54 && raw[2] == 0x41 && raw[3] == 0x31);
    CHECK(raw[4] == 0x34 && raw[5] == 0x12 && raw[6] == 0x00 && raw[7] == 0x00);
    WpmOtaRecord out = {};
    CHECK(wpm_ota_record_parse(raw, &out));
    CHECK(out.pending);
    CHECK(eq_rec(in, out));
  }

  // pending=false: magic serialized as zeros, parses as not pending, fields intact.
  {
    WpmOtaRecord in = {0x2BD18u, 0x01020304u, 0x80, 0x02, 0xA5A5A5A5u, false};
    uint8_t raw[WPM_OTA_RECORD_LEN];
    wpm_ota_record_serialize(&in, raw);
    CHECK(raw[0] == 0 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0);
    WpmOtaRecord out = {};
    CHECK(wpm_ota_record_parse(raw, &out));
    CHECK(!out.pending);
    CHECK(eq_rec(in, out));
  }

  // All-0xFF and all-0x00 raw: not pending; last_result 0 and >2 both read as "none".
  {
    uint8_t ff[WPM_OTA_RECORD_LEN];
    std::memset(ff, 0xFF, sizeof ff);
    WpmOtaRecord out = {};
    CHECK(wpm_ota_record_parse(ff, &out));
    CHECK(!out.pending);
    CHECK(std::strcmp(wpm_ota_last_result_str(out.last_result), "none") == 0);

    uint8_t zero[WPM_OTA_RECORD_LEN];
    std::memset(zero, 0x00, sizeof zero);
    CHECK(wpm_ota_record_parse(zero, &out));
    CHECK(!out.pending);
    CHECK(std::strcmp(wpm_ota_last_result_str(out.last_result), "none") == 0);
    CHECK(std::strcmp(wpm_ota_last_result_str(0xFF), "none") == 0);
  }

  CHECK(std::strcmp(wpm_ota_last_result_str(1), "ok") == 0);
  CHECK(std::strcmp(wpm_ota_last_result_str(2), "crc_mismatch") == 0);

  // Length checks.
  CHECK(wpm_ota_check_len(0) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_check_len(1) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_check_len(0x2BD18u) == WPM_OTA_OK);
  CHECK(wpm_ota_check_len(0x2BD1Au) == WPM_OTA_ERR_LEN);
  CHECK(wpm_ota_check_len(0x3A000u) == WPM_OTA_ERR_LEN);

  // Head checks (first 44 bytes of the image at 0x6000).
  {
    const uint8_t stock[WPM_OTA_HEAD_LEN] = {0x00, 0x80, 0x00, 0x20, 0x05, 0xcb, 0x00, 0x00,
                                             0xed, 0xca, 0x00, 0x00, 0xed, 0xca, 0x00, 0x00};
    CHECK(wpm_ota_check_head(stock, 108612) == WPM_OTA_ERR_IMAGE);
    CHECK(wpm_ota_check_head(stock, 0) == WPM_OTA_ERR_IMAGE);

    // The offsets are part of the OTA contract (mirrored by the Mac app): pin the literals.
    static_assert(WPM_APP_MARKER_OFF == 0x1C, "marker offset is the contract value");
    static_assert(WPM_OTA_HEAD_LEN == 44, "head length is the contract value");
    static_assert(WPM_APP_MARKER_LEN == 16, "marker length is the contract value");
    const uint8_t magic[8] = {'W', 'P', 'M', 'A', 'P', 'P', '1', 0};
    const uint8_t magic2[8] = {'W', 'P', 'M', 'A', 'P', 'P', '2', 0};
    const uint8_t zero8[8] = {0};
    uint8_t h[WPM_OTA_HEAD_LEN];
    auto mk = [&](uint32_t sp, uint32_t reset, const uint8_t* m, uint32_t base, uint32_t mlen) {
      std::memset(h, 0, sizeof h);
      put32(h, sp);
      put32(h + 4, reset);
      std::memcpy(h + WPM_APP_MARKER_OFF, m, 8);
      put32(h + WPM_APP_MARKER_OFF + 8, base);
      put32(h + WPM_APP_MARKER_OFF + 12, mlen);
    };
    mk(0x20008000u, 0x000140D9u, magic, 0x6000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_OK);
    CHECK(wpm_ota_check_head(h, 0) == WPM_OTA_OK);
    CHECK(wpm_ota_check_head(h, 113870) == WPM_OTA_ERR_IMAGE);
    CHECK(wpm_ota_check_head(h, 113866) == WPM_OTA_ERR_IMAGE);
    mk(0x20008000u, 0x000140D9u, magic, 0x2000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_ERR_IMAGE);
    mk(0x20008000u, 0x000140D9u, magic2, 0x6000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_ERR_IMAGE);
    mk(0x20008000u, 0x000140D9u, zero8, 0x6000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_ERR_IMAGE);
    mk(0x20008000u, 0x000140D8u, magic, 0x6000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_ERR_IMAGE);
    mk(0x20008000u, 0x00004B05u, magic, 0x6000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_ERR_IMAGE);
    mk(0x20008004u, 0x000140D9u, magic, 0x6000u, 113868u);
    CHECK(wpm_ota_check_head(h, 113868) == WPM_OTA_ERR_IMAGE);
    std::memset(h, 0xFF, sizeof h);
    CHECK(wpm_ota_check_head(h, 0) == WPM_OTA_ERR_IMAGE);
  }
  CHECK(std::strcmp(wpm_ota_last_result_str(3), "image") == 0);
  CHECK(std::strcmp(wpm_ota_last_result_str(4), "none") == 0);

  std::printf("test_ota_record: %d failures\n", minitest_failures);
  return minitest_failures;
}
