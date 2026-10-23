#include "wpm_dump_dev.h"

#include <stdio.h>
#include <string.h>

#include "WireByte.h"
#include "wpm_bus.h"
#include "wpm_dump.h"
#include "wpm_json.h"

// Defined in 2Wireless.ino.
bool wpm_dump_master_backup(uint8_t addr);
void framWrite(int addr, uint8_t value);
void wpm_stock_decode_frame(const uint8_t* f, int n);  // the stock >4-byte general-call decoder
extern uint8_t wpm_isr_frame[256];                     // static ISR frame buffer (sketch)

static const uint32_t READ_IDLE_MS = 50;    // no SEND while a module read is this recent (as the scan)
static const uint32_t READ_BUSY_MS = 1000;  // /dump refuses within this of a module read
// The card's I2C slave address: 0x50 | CARD_ADDRESS, and CARD_ADDRESS is 0x00 in the sketch.
static const uint8_t CARD_I2C_ADDR = 0x50;

static WpmDumpJob job;
// Diagnostic: first 8 matched addresses seen by the ISR handler in this job. The hook is live from
// state "sending", so a frame logged between start() and the clear below can be lost (harmless).
static const uint8_t ADDR_LOG_MAX = 8;
static volatile uint8_t addr_log[ADDR_LOG_MAX];
static volatile uint8_t addr_log_n = 0;

bool wpm_dump_active() {
  WpmDumpState s = job.state();
  return s == WPM_DUMP_SENDING || s == WPM_DUMP_WAITING || s == WPM_DUMP_TRANSFERRING;
}

void wpm_dump_loop() {
  // Modules read presets from the WPM with interrupts off and no timeouts: do not master the bus
  // right after a read. The hold is capped at the job's first_wait_ms (measured from start): after
  // that the BACKUP is reported as not sent (error 4) instead of holding forever.
  if (job.state() == WPM_DUMP_SENDING && wpm_bus_read_recent(READ_IDLE_MS)) {
    if (job.elapsed_ms(millis()) >= job.first_wait_ms()) job.send_failed(millis());
    return;
  }
  if (job.step(millis()) == WPM_DUMP_ACT_SEND_BACKUP) {
    wpm_job_master_begin();
    bool sent = wpm_dump_master_backup(job.addr());
    wpm_job_master_end();
    if (!sent) job.send_failed(millis());
  }
}

// Apply a data line [off_hi][off_lo][data...] of n >= 3 bytes (the arithmetic is in WpmCommon).
static void data_line(const uint8_t* f, int n, bool drop_past_expected) {
  // The scratch bound is defensive: a u16 offset plus a 254-byte line stays below WPM_DUMP_SCRATCH_END
  // (0x2BD18), so only the shape-parsing fallback ever drops; it keeps a wider offset away from the
  // settings and the OTA record.
  WpmDumpLinePlan p = wpm_dump_plan_line(f[0], f[1], n, job.expected(), drop_past_expected);
  for (uint32_t i = 0; i < p.write_n; i++) framWrite((int)(p.offset + i), f[2 + i]);
  if (p.dropped) job.on_dropped(p.dropped);
  if (p.write_n) job.on_data(p.offset, p.write_n);
}

static void log_addr(uint8_t a) {
  if (addr_log_n < ADDR_LOG_MAX) {
    addr_log[addr_log_n] = a;
    addr_log_n = addr_log_n + 1;
  }
}

// Reads the whole pending transaction (at most 256 bytes, the ring buffer size).
static int drain(uint8_t* f, int cap) {
  int n = 0;
  while (Wire.available() > 0) {
    int b = Wire.read();
    if (n < cap) f[n++] = (uint8_t)b;
  }
  return n;
}

bool wpm_dump_isr_frame(int /*howMany*/) {
  uint8_t* f = wpm_isr_frame;  // one static ISR buffer shared with the stock decoder path
  const uint8_t a = Wire.lastAddress();
#if WPM_DUMP_ADDR_FILTER
  const int avail = Wire.available();
  // Decide by address before draining: a general-call frame of any length but 5 stays unread for
  // the stock path (MIDI clock and 0x13 display frames keep working); peek is not available.
  if (!(a == CARD_I2C_ADDR || (a == 0x00 && avail == 5))) {
    if (avail > 0) log_addr(a);
    return false;
  }
  const bool filter = true;
#else
  const bool filter = false;
#endif
  const int n = drain(f, 256);
  if (n > 0) log_addr(a);
  switch (wpm_dump_classify(a, n, n > 0 ? f[0] : 0, n > 3 ? f[3] : 0, filter)) {
    case WPM_DUMP_FRAME_DATA: data_line(f, n, !filter); break;
    case WPM_DUMP_FRAME_COMPLETE:
      wpm_bus_note_frame();
      job.on_complete_frame(f[2]);
      break;
    case WPM_DUMP_FRAME_OTHER: wpm_stock_decode_frame(f, n); break;  // a 5-byte MIDI clock / display frame
    default: break;
  }
  return true;
}

void wpm_dump_handle_start(WiFiClient& client, const String& urlString) {
  if (wpm_bus_job_busy() || wpm_bus_read_recent(READ_BUSY_MS)) {
    wpm_send_json(client, wpm_json_err("busy"));
    return;
  }
  // Query text: after '?' up to the space before " HTTP".
  String query = "";
  int qpos = urlString.indexOf('?');
  if (qpos >= 0) {
    int e = urlString.indexOf(' ', qpos);
    query = urlString.substring(qpos + 1, e < 0 ? urlString.length() : e);
  }
  uint8_t addr = 0;
  uint32_t len, first_wait, silence;
  if (!wpm_dump_parse_args(query.c_str(), &addr, &len, &first_wait, &silence)) {
    wpm_send_json(client, wpm_json_err("args"));
    return;
  }
  switch (job.start(addr, len, first_wait, silence, millis())) {
    case WPM_BUS_ERR_NONE:
      addr_log_n = 0;
      wpm_send_json(client, "{\"ok\":true}");
      break;
    case WPM_BUS_ERR_BUSY: wpm_send_json(client, wpm_json_err("busy")); break;
    default: wpm_send_json(client, wpm_json_err("args")); break;
  }
}

void wpm_dump_handle_status(WiFiClient& client) {
  char buf[256];
  size_t n = wpm_dump_status_json(job, millis(), buf, sizeof buf);
  if (n < 2) {  // cannot happen with a 256-byte buffer; kept as a safe reply
    wpm_send_json(client, wpm_json_err("args"));
    return;
  }
  buf[n - 1] = '\0';  // drop the closing '}'
  String body = buf;
  body += ",\"addr_log\":\"";
  uint8_t cnt = addr_log_n;
  if (cnt > ADDR_LOG_MAX) cnt = ADDR_LOG_MAX;
  for (uint8_t i = 0; i < cnt; i++) {
    char h[4];
    snprintf(h, sizeof h, i ? " %02x" : "%02x", (unsigned)addr_log[i]);
    body += h;
  }
  body += "\"}";
  wpm_send_json(client, body);
}

void wpm_dump_handle_abort(WiFiClient& client) {
  job.abort(millis());
  wpm_send_json(client, "{\"ok\":true}");
}
