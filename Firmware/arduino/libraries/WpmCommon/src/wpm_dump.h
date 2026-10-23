#pragma once
// Byte-counting preset capture job (/dump): a BACKUP is mastered to a module, the module writes its
// bank to the card address as [offset_hi][offset_lo][data...] lines, and this state machine tracks
// the first-byte wait, the byte counters, and the end of the transfer (completion frame, expected
// length reached, or silence). Pure C++17, no Arduino dependencies, so it runs on the desktop.
// on_data(), on_dropped() and on_complete_frame() are ISR-side (bounded volatile writes only, no
// allocation); everything else runs from the main loop. step() snapshots the counters.
#include <cstddef>
#include <cstdint>

#include "wpm_bus_scan.h"  // WpmBusError

constexpr uint32_t WPM_DUMP_SCRATCH_END = 0x2BD18;  // exclusive; same as WPM_OTA_MAX_LEN
constexpr uint32_t WPM_DUMP_FIRST_WAIT_DEFAULT_MS = 5000, WPM_DUMP_FIRST_WAIT_MIN_MS = 200,
                   WPM_DUMP_FIRST_WAIT_MAX_MS = 60000;
constexpr uint32_t WPM_DUMP_SILENCE_DEFAULT_MS = 1000, WPM_DUMP_SILENCE_MIN_MS = 200,
                   WPM_DUMP_SILENCE_MAX_MS = 20000;

enum WpmDumpState {
  WPM_DUMP_IDLE,
  WPM_DUMP_SENDING,
  WPM_DUMP_WAITING,
  WPM_DUMP_TRANSFERRING,
  WPM_DUMP_DONE,
  WPM_DUMP_FAILED
};
enum WpmDumpAction { WPM_DUMP_ACT_NONE, WPM_DUMP_ACT_SEND_BACKUP };

class WpmDumpJob {
 public:
  // BUSY while sending/waiting/transferring; ARGS for addr outside 0x08..0x77, expected >
  // WPM_DUMP_SCRATCH_END, first_wait or silence outside their clamps (the route clamps; the class
  // refuses). Neither refusal changes the current state or counters. Success clears all counters.
  WpmBusError start(uint8_t addr, uint32_t expected, uint32_t first_wait_ms, uint32_t silence_ms,
                    uint32_t now_ms);
  WpmDumpAction step(uint32_t now_ms);  // SEND_BACKUP exactly once, right after start
  void send_failed(uint32_t now_ms);    // -> FAILED, error SEND_TIMEOUT (only while SENDING/WAITING)
  void abort(uint32_t now_ms);          // any state -> IDLE (counters kept for status until next start)
  // ISR side, volatile writes only (single ISR writer assumed):
  // A data line landed: n bytes written at offset (after the caller bounded it).
  void on_data(uint32_t offset, uint32_t n);
  void on_dropped(uint32_t n);                // bytes beyond the scratch area, not written
  void on_complete_frame(uint8_t from_addr);  // stray if from another address; early_done if no data yet
  // (a 251e acknowledges the BACKUP with a 0x0A frame seconds before it streams); else ends the job

  WpmDumpState state() const { return state_; }
  uint8_t addr() const { return addr_; }
  uint32_t expected() const { return expected_; }
  uint32_t first_wait_ms() const { return first_wait_; }
  uint32_t received() const { return received_; }
  uint32_t extent() const { return extent_; }  // max offset+n
  uint32_t lines() const { return lines_; }
  uint32_t dropped() const { return dropped_; }
  bool complete_frame() const { return complete_; }
  uint32_t early_done() const { return early_; }  // completion frames from the job address before any data
  WpmBusError error() const { return err_; }
  uint32_t stray_frames() const { return stray_; }
  // Milliseconds since start; frozen at the moment the job ended or was aborted. 0 before any start.
  uint32_t elapsed_ms(uint32_t now_ms) const;

 private:
  bool running() const {
    return state_ == WPM_DUMP_SENDING || state_ == WPM_DUMP_WAITING || state_ == WPM_DUMP_TRANSFERRING;
  }
  void finish(WpmDumpState s, WpmBusError e, uint32_t now_ms);
  WpmDumpState state_ = WPM_DUMP_IDLE;
  uint8_t addr_ = 0;
  bool started_ = false;
  uint32_t expected_ = 0, first_wait_ = WPM_DUMP_FIRST_WAIT_DEFAULT_MS,
           silence_ = WPM_DUMP_SILENCE_DEFAULT_MS;
  uint32_t t_begin_ = 0, t_start_ = 0, t_last_ = 0, t_end_ = 0;
  uint32_t seen_ = 0;  // received_ as of the last step()
  WpmBusError err_ = WPM_BUS_ERR_NONE;
  // ISR-shared.
  volatile uint32_t received_ = 0, extent_ = 0, lines_ = 0, dropped_ = 0, stray_ = 0, early_ = 0;
  volatile bool complete_ = false;
};

// ---- Pure helpers for the device layer (desktop-tested) ----
// What the dump ISR does with a transaction. addr is the matched 7-bit address (ignored when
// addr_filter is false), n the byte count, f0/f3 the first and fourth bytes when n >= 4.
enum WpmDumpFrameKind {
  WPM_DUMP_FRAME_IGNORE,    // too short to be a line: consume and drop
  WPM_DUMP_FRAME_DATA,      // a [off_hi][off_lo][data...] line
  WPM_DUMP_FRAME_COMPLETE,  // 5-byte completion frame 04 .. <addr> 0A ..
  WPM_DUMP_FRAME_OTHER      // anything else (general-call MIDI/display frames): stock path
};
// Filter on: card address (0x50) with n >= 3 is DATA (n < 3 IGNORE); a general call (0x00) of 5 bytes
// is COMPLETE if f0 == 0x04 && f3 == 0x0A, else OTHER; everything else OTHER. Filter off: shape only
// (COMPLETE, else DATA when n >= 3, else IGNORE).
WpmDumpFrameKind wpm_dump_classify(uint8_t addr, int n, uint8_t f0, uint8_t f3, bool addr_filter);
// Plan for a data line of n >= 3 bytes starting [hi][lo]: write_n bytes go to FRAM at offset, the
// rest are dropped (beyond scratch_end, or the whole line when drop_past_expected, expected != 0 and
// offset >= expected: the shape-parsing fallback).
struct WpmDumpLinePlan {
  uint32_t offset, write_n, dropped;
};
WpmDumpLinePlan wpm_dump_plan_line(uint8_t hi, uint8_t lo, int n, uint32_t expected, bool drop_past_expected,
                                   uint32_t scratch_end = WPM_DUMP_SCRATCH_END);
// Parses "addr=28&len=990&first_wait_ms=M&silence_ms=S" (no leading '?'). addr: exactly 2 hex digits,
// either case, required. Numbers: 1..9 decimal digits. Waits are clamped to the class limits; len is
// left for start() to range check. Unknown keys, empty or malformed values fail.
bool wpm_dump_parse_args(const char* q, uint8_t* addr, uint32_t* len, uint32_t* first_wait, uint32_t* silence);

const char* wpm_dump_state_str(WpmDumpState s);  // "idle"|"sending"|"waiting"|"transferring"|"done"|"failed"
// {"state":"transferring","addr":"28","expected":990,"received":528,"extent":528,"lines":8,"dropped":0,
//  "complete_frame":false,"early_done":0,"error":0,"elapsed_ms":1234}
// Returns the length written, or 0 if n is too small. The worst case is 222 bytes plus the NUL, so
// routes pass a 256-byte buffer.
size_t wpm_dump_status_json(const WpmDumpJob& j, uint32_t now_ms, char* buf, size_t n);
