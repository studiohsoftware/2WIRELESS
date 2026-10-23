#pragma once
// /dump, /dump/status and /dump/abort routes: byte-counting preset capture. A BACKUP is mastered
// to a module; the module writes its bank to the card address (0x50) as [off_hi][off_lo][data...]
// lines, which the I2C receive ISR stores straight into the FRAM scratch area (0..0x2BD18) while
// the state machine (libraries/WpmCommon/src/wpm_dump.h) counts bytes and decides the end.
//
// /dump?addr=XX[&len=N][&first_wait_ms=M][&silence_ms=S]: addr is 2 hex digits (required), len is
// the expected byte count (default 0 = unknown), the waits are decimal and clamped to the class
// limits. Replies {"ok":true}, or {"ok":false,"error":"busy"|"args"}. A BACKUP that could not be
// mastered shows up in /dump/status as state "failed", error 4.
// /dump/status: the Task 1 JSON, then one more key, always last:
//   "addr_log":"50 50 00"
// DIAGNOSTIC, present in 1.21: the matched slave addresses (7-bit hex, space separated) of the
// first 8 transactions the dump ISR handler saw during the current job, "" when none. 0x50 is the
// card address, 00 a general call, ff when WPM_DUMP_ADDR_FILTER is 0. Clients must not rely on it.
#include <Arduino.h>
#include <WiFi101.h>

#include "wpm_dump_config.h"  // WPM_DUMP_ADDR_FILTER

// Notes: every bus master send is refused while a bus job runs (HTTP routes answer busy; USB MIDI
// and poll-on-start sends are dropped), and for 50 ms after the last transaction addressed to the
// card (the module may still be streaming lines or sending its completion after the job ended).
// While a dump runs the I2C ISR writes FRAM; the sketch's FRAM primitives are critical sections in
// thread mode, so ordinary routes (/currentpreset, /version, settings) are safe meanwhile. /readmemory
// and /writememory stay available, but the ISR writes FRAM concurrently: a read may return
// stale (in-progress) bank and a write into the capture area is overwritten by the lines, so clients wait for state "done" before reading the bank.

bool wpm_dump_active();  // job state SENDING, WAITING or TRANSFERRING
void wpm_dump_loop();    // call once per loop() pass (main loop only), after wpm_bus_loop()
// ISR: receiveEvent calls this while wpm_dump_active(). Returns true when the frame was handled
// (data line, completion frame, or a 5-byte general-call frame forwarded to the stock decoder);
// false leaves every byte unread for the stock receive path (other general-call frames).
bool wpm_dump_isr_frame(int howMany);

// urlString: "...GET /dump?addr=28&len=990 ...".
void wpm_dump_handle_start(WiFiClient& client, const String& urlString);
void wpm_dump_handle_status(WiFiClient& client);
void wpm_dump_handle_abort(WiFiClient& client);
