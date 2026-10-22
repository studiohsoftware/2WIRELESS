#include "wpm_crc32.h"

uint32_t wpm_crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) {
      crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
  }
  return crc;
}

uint32_t wpm_crc32(const uint8_t* data, size_t len) {
  return wpm_crc32_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}
