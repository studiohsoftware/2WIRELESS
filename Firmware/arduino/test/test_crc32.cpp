#include <cstdint>
#include <cstdio>

#include "minitest.h"
#include "wpm_crc32.h"

static const uint8_t* bytes(const char* s) {
  return reinterpret_cast<const uint8_t*>(s);
}

int main() {
  CHECK(wpm_crc32(bytes("123456789"), 9) == 0xCBF43926u);
  CHECK(wpm_crc32(nullptr, 0) == 0x00000000u);

  uint32_t u = wpm_crc32_update(0xFFFFFFFFu, bytes("1234"), 4);
  u = wpm_crc32_update(u, bytes("56789"), 5);
  CHECK((u ^ 0xFFFFFFFFu) == 0xCBF43926u);

  std::printf("test_crc32: %d failures\n", minitest_failures);
  return minitest_failures;
}
