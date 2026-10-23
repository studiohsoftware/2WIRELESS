#pragma once
// The first-stage FRAM loader, placed at flash 0x2000 by the core's linker
// script (.sketch_boot). Generated into wpm_loader_blob.cpp by tools/blob2c.py.
#include <stdint.h>

extern const uint8_t wpm_loader_blob[0x4000] __attribute__((section(".sketch_boot")));
