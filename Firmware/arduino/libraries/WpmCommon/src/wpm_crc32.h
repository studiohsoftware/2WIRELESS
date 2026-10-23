#pragma once
// CRC-32 (reflected, polynomial 0xEDB88320, as used by zlib). Bitwise, no table.
#include <cstddef>
#include <cstdint>

// Running update. Start with crc = 0xFFFFFFFF; no final XOR is applied here.
uint32_t wpm_crc32_update(uint32_t crc, const uint8_t* data, size_t len);

// Whole-buffer CRC-32: update from 0xFFFFFFFF, then final XOR with 0xFFFFFFFF.
uint32_t wpm_crc32(const uint8_t* data, size_t len);
