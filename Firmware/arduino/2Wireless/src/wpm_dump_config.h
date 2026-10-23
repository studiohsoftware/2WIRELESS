#pragma once
// Compile-time switch for the matched-address filter of the /dump ISR path. Kept in its own
// header so the vendored Wire (WireByte.h) can use it without pulling in WiFi101.
//
// 1 (default, confirmed on the bench): WireByte.cpp records the 7-bit address that matched each
// transaction (Wire.lastAddress()); the dump handler applies data lines only from transactions
// addressed to the card (0x50) and leaves general-call frames to the stock path.
// 0: no address capture; the handler falls back to shape parsing.
#ifndef WPM_DUMP_ADDR_FILTER
#define WPM_DUMP_ADDR_FILTER 1
#endif
