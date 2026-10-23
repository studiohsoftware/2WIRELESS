#include "wpm_dump.h"

#include <cstdio>
#include <cstring>

WpmBusError WpmDumpJob::start(uint8_t addr, uint32_t expected, uint32_t first_wait_ms, uint32_t silence_ms,
                              uint32_t now_ms) {
  if (running()) return WPM_BUS_ERR_BUSY;
  if (addr < WPM_SCAN_ADDR_MIN || addr > WPM_SCAN_ADDR_MAX || expected > WPM_DUMP_SCRATCH_END ||
      first_wait_ms < WPM_DUMP_FIRST_WAIT_MIN_MS || first_wait_ms > WPM_DUMP_FIRST_WAIT_MAX_MS ||
      silence_ms < WPM_DUMP_SILENCE_MIN_MS || silence_ms > WPM_DUMP_SILENCE_MAX_MS)
    return WPM_BUS_ERR_ARGS;
  addr_ = addr;
  expected_ = expected;
  first_wait_ = first_wait_ms;
  silence_ = silence_ms;
  started_ = true;
  t_begin_ = t_start_ = t_last_ = t_end_ = now_ms;
  seen_ = 0;
  err_ = WPM_BUS_ERR_NONE;
  received_ = 0;
  extent_ = 0;
  lines_ = 0;
  dropped_ = 0;
  stray_ = 0;
  early_ = 0;
  complete_ = false;
  state_ = WPM_DUMP_SENDING;
  return WPM_BUS_ERR_NONE;
}

void WpmDumpJob::finish(WpmDumpState s, WpmBusError e, uint32_t now_ms) {
  state_ = s;
  err_ = e;
  t_end_ = now_ms;
}

WpmDumpAction WpmDumpJob::step(uint32_t now_ms) {
  const uint32_t rx = received_;  // one snapshot per step
  const bool done_frame = complete_;
  switch (state_) {
    case WPM_DUMP_SENDING:
      t_start_ = now_ms;
      seen_ = rx;
      state_ = WPM_DUMP_WAITING;
      return WPM_DUMP_ACT_SEND_BACKUP;
    case WPM_DUMP_WAITING:
      if (done_frame) {
        finish(WPM_DUMP_DONE, WPM_BUS_ERR_NONE, now_ms);
      } else if (rx != seen_) {
        seen_ = rx;
        t_last_ = now_ms;
        state_ = WPM_DUMP_TRANSFERRING;
      } else if (now_ms - t_start_ >= first_wait_) {
        finish(WPM_DUMP_FAILED, WPM_BUS_ERR_NO_RESPONSE, now_ms);
      }
      break;
    case WPM_DUMP_TRANSFERRING:
      if (done_frame || (expected_ && rx >= expected_)) {
        finish(WPM_DUMP_DONE, WPM_BUS_ERR_NONE, now_ms);
      } else if (rx != seen_) {
        seen_ = rx;
        t_last_ = now_ms;
      } else if (now_ms - t_last_ >= silence_) {
        finish(WPM_DUMP_DONE, WPM_BUS_ERR_NONE, now_ms);
      }
      break;
    default:
      break;
  }
  return WPM_DUMP_ACT_NONE;
}

void WpmDumpJob::send_failed(uint32_t now_ms) {
  if (state_ == WPM_DUMP_SENDING || state_ == WPM_DUMP_WAITING)
    finish(WPM_DUMP_FAILED, WPM_BUS_ERR_SEND_TIMEOUT, now_ms);
}

void WpmDumpJob::abort(uint32_t now_ms) {
  if (running()) t_end_ = now_ms;
  state_ = WPM_DUMP_IDLE;
}

void WpmDumpJob::on_data(uint32_t offset, uint32_t n) {
  const uint32_t end = offset + n;
  if (end > extent_) extent_ = end;
  received_ = received_ + n;
  lines_ = lines_ + 1;
}

void WpmDumpJob::on_dropped(uint32_t n) { dropped_ = dropped_ + n; }

void WpmDumpJob::on_complete_frame(uint8_t from_addr) {
  if (from_addr != addr_)
    stray_ = stray_ + 1;
  else if (received_ == 0)
    early_ = early_ + 1;  // acknowledgement, not the end: the first-byte wait keeps running
  else
    complete_ = true;
}

uint32_t WpmDumpJob::elapsed_ms(uint32_t now_ms) const {
  if (!started_) return 0;
  return (running() ? now_ms : t_end_) - t_begin_;
}

WpmDumpFrameKind wpm_dump_classify(uint8_t addr, int n, uint8_t f0, uint8_t f3, bool addr_filter) {
  const bool completion = n == 5 && f0 == 0x04 && f3 == 0x0A;
  if (addr_filter) {
    if (addr == 0x50) return n >= 3 ? WPM_DUMP_FRAME_DATA : WPM_DUMP_FRAME_IGNORE;
    if (addr == 0x00 && n == 5) return completion ? WPM_DUMP_FRAME_COMPLETE : WPM_DUMP_FRAME_OTHER;
    return WPM_DUMP_FRAME_OTHER;
  }
  if (completion) return WPM_DUMP_FRAME_COMPLETE;
  return n >= 3 ? WPM_DUMP_FRAME_DATA : WPM_DUMP_FRAME_IGNORE;
}

WpmDumpLinePlan wpm_dump_plan_line(uint8_t hi, uint8_t lo, int n, uint32_t expected, bool drop_past_expected,
                                   uint32_t scratch_end) {
  WpmDumpLinePlan p = {(static_cast<uint32_t>(hi) << 8) | lo, 0, 0};
  const uint32_t cnt = n > 2 ? static_cast<uint32_t>(n - 2) : 0;
  if (drop_past_expected && expected != 0 && p.offset >= expected) {
    p.dropped = cnt;
    return p;
  }
  uint32_t room = p.offset < scratch_end ? scratch_end - p.offset : 0;
  p.write_n = cnt < room ? cnt : room;
  p.dropped = cnt - p.write_n;
  return p;
}

static bool parse_dec(const char* s, size_t len, uint32_t* out) {
  if (len == 0 || len > 9) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + static_cast<uint32_t>(s[i] - '0');
  }
  *out = v;
  return true;
}

static bool parse_hex2(const char* s, size_t len, uint8_t* out) {
  if (len != 2) return false;
  uint8_t v = 0;
  for (size_t i = 0; i < 2; i++) {
    char c = s[i];
    uint8_t d;
    if (c >= '0' && c <= '9') d = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') d = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = static_cast<uint8_t>(c - 'A' + 10);
    else return false;
    v = static_cast<uint8_t>(v * 16 + d);
  }
  *out = v;
  return true;
}

static uint32_t clamp32(uint32_t n, uint32_t lo, uint32_t hi) { return n < lo ? lo : (n > hi ? hi : n); }

bool wpm_dump_parse_args(const char* q, uint8_t* addr, uint32_t* len, uint32_t* first_wait, uint32_t* silence) {
  bool have_addr = false;
  *len = 0;
  *first_wait = WPM_DUMP_FIRST_WAIT_DEFAULT_MS;
  *silence = WPM_DUMP_SILENCE_DEFAULT_MS;
  while (*q) {
    const char* amp = std::strchr(q, '&');
    size_t seg = amp ? static_cast<size_t>(amp - q) : std::strlen(q);
    const char* eq = static_cast<const char*>(std::memchr(q, '=', seg));
    if (!eq) return false;
    size_t klen = static_cast<size_t>(eq - q);
    const char* v = eq + 1;
    size_t vlen = seg - klen - 1;
    uint32_t n;
    if (klen == 4 && std::strncmp(q, "addr", 4) == 0) {
      if (!parse_hex2(v, vlen, addr)) return false;
      have_addr = true;
    } else if (klen == 3 && std::strncmp(q, "len", 3) == 0) {
      if (!parse_dec(v, vlen, len)) return false;
    } else if (klen == 13 && std::strncmp(q, "first_wait_ms", 13) == 0) {
      if (!parse_dec(v, vlen, &n)) return false;
      *first_wait = clamp32(n, WPM_DUMP_FIRST_WAIT_MIN_MS, WPM_DUMP_FIRST_WAIT_MAX_MS);
    } else if (klen == 10 && std::strncmp(q, "silence_ms", 10) == 0) {
      if (!parse_dec(v, vlen, &n)) return false;
      *silence = clamp32(n, WPM_DUMP_SILENCE_MIN_MS, WPM_DUMP_SILENCE_MAX_MS);
    } else {
      return false;
    }
    q = amp ? amp + 1 : q + seg;
  }
  return have_addr;
}

const char* wpm_dump_state_str(WpmDumpState s) {
  switch (s) {
    case WPM_DUMP_IDLE: return "idle";
    case WPM_DUMP_SENDING: return "sending";
    case WPM_DUMP_WAITING: return "waiting";
    case WPM_DUMP_TRANSFERRING: return "transferring";
    case WPM_DUMP_DONE: return "done";
    case WPM_DUMP_FAILED: return "failed";
  }
  return "idle";
}

size_t wpm_dump_status_json(const WpmDumpJob& j, uint32_t now_ms, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int w = std::snprintf(buf, n,
                        "{\"state\":\"%s\",\"addr\":\"%02x\",\"expected\":%lu,\"received\":%lu,"
                        "\"extent\":%lu,\"lines\":%lu,\"dropped\":%lu,\"complete_frame\":%s,"
                        "\"early_done\":%lu,\"error\":%d,\"elapsed_ms\":%lu}",
                        wpm_dump_state_str(j.state()), static_cast<unsigned>(j.addr()),
                        static_cast<unsigned long>(j.expected()), static_cast<unsigned long>(j.received()),
                        static_cast<unsigned long>(j.extent()), static_cast<unsigned long>(j.lines()),
                        static_cast<unsigned long>(j.dropped()), j.complete_frame() ? "true" : "false",
                        static_cast<unsigned long>(j.early_done()),
                        static_cast<int>(j.error()), static_cast<unsigned long>(j.elapsed_ms(now_ms)));
  if (w < 0 || static_cast<size_t>(w) >= n) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(w);
}
