#include "wpm_bus.h"

#include "wpm_bus_scan.h"
#include "wpm_dump_dev.h"
#include "wpm_json.h"

// Defined in 2Wireless.ino.
bool wpm_bus_master_query(uint8_t addr);
bool wpm_bus_master_polldone();
bool wpm_v2_pin();  // digitalRead(V2VERSION_PIN); unlike v2version, not flipped by a /v2/ prefix
extern volatile unsigned long readTime;  // millis() of the last module read (written in requestEvent)
extern volatile unsigned long cardWriteTime;  // millis() of the last transaction addressed to the card (receiveEvent)
uint8_t framRead(int addr);

// Stock display cache written by setDisplayMessage(0, ...) on every 0x13 frame: [addr][7 chars].
static const int DISPLAY_CACHE_ADDR = 0x02C0FE;

static const uint32_t BUS_QUIET_MS = 50;
static const uint32_t READ_IDLE_MS = 50;     // no SEND while a module read is this recent
static const uint32_t READ_BUSY_MS = 1000;   // /bus/scan and /bus/polldone refuse within this

static WpmBusScan scan;
// ISR-written, main-loop-read (32-bit / byte accesses are atomic on the SAMD21).
static volatile uint32_t last_frame_ms = 0;
static volatile bool frame_seen = false;

static bool readRecent(uint32_t ms) { return (uint32_t)(millis() - (uint32_t)readTime) < ms; }

// Not reached while a /getpresets capture caches I2C to FRAM or while i2c_guard is set, so
// bus_quiet's frame part can read true during a capture.
void wpm_bus_note_frame() {
  last_frame_ms = millis();
  frame_seen = true;
}

void wpm_bus_on_version_frame(uint8_t addr, const uint8_t ver[7]) {
  scan.on_version_frame(addr, ver, 7);
}

void wpm_bus_on_query_reply(uint8_t addr) { scan.on_query_reply(addr); }

bool wpm_bus_scan_active() { return scan.state() == WPM_SCAN_SCANNING; }

bool wpm_bus_read_recent(uint32_t ms) { return readRecent(ms); }

static const uint32_t CARD_WRITE_COOLDOWN_MS = 50;
bool wpm_card_write_recent(uint32_t ms) { return (uint32_t)(millis() - (uint32_t)cardWriteTime) < ms; }
bool wpm_bus_job_busy() {
  return wpm_bus_scan_active() || wpm_dump_active() || wpm_card_write_recent(CARD_WRITE_COOLDOWN_MS);
}

static volatile bool job_master = false;
void wpm_job_master_begin() { job_master = true; }
void wpm_job_master_end() { job_master = false; }
bool wpm_job_master_allowed() { return job_master; }

void wpm_bus_loop() {
  // Modules read presets from the WPM with interrupts off and no timeouts: never start a send
  // while reads are recent (step() is skipped, which defers the SEND phase; the reply window and
  // the gap are wall-clock and keep running). Note: masterBeginTransmission's
  // busy-wait reads I2CM STATUS while the SERCOM is still in slave mode (stock behaviour), and a
  // sweep runs it once per address; PR 2/4 will master more.
  if (readRecent(READ_IDLE_MS)) return;
  if (scan.step(millis()) == WPM_SCAN_ACT_QUERY) {
    wpm_job_master_begin();
    bool sent = wpm_bus_master_query(scan.current_addr());
    wpm_job_master_end();
    if (!sent) scan.query_failed(millis());
  }
}

void wpm_bus_handle_scan(WiFiClient& client, const String& urlString) {
  if (wpm_v2_pin()) {  // QUERY is not defined on a V2 bus
    wpm_send_json(client, wpm_json_err("unsupported"));
    return;
  }
  if (wpm_bus_job_busy() || readRecent(READ_BUSY_MS)) {
    scan.set_error(WPM_BUS_ERR_BUSY);
    wpm_send_json(client, wpm_json_err("busy"));
    return;
  }
  // Query text: after '?' up to the space before " HTTP".
  String query = "";
  int q = urlString.indexOf('?');
  if (q >= 0) {
    int e = urlString.indexOf(' ', q);
    query = urlString.substring(q + 1, e < 0 ? urlString.length() : e);
  }
  uint8_t first, last;
  uint32_t window;
  if (!wpm_bus_parse_scan_args(query.c_str(), &first, &last, &window)) {
    scan.set_error(WPM_BUS_ERR_ARGS);
    wpm_send_json(client, wpm_json_err("args"));
    return;
  }
  switch (scan.start(first, last, millis(), window)) {
    case WPM_BUS_ERR_NONE: wpm_send_json(client, "{\"ok\":true}"); break;
    case WPM_BUS_ERR_BUSY: wpm_send_json(client, wpm_json_err("busy")); break;
    default: wpm_send_json(client, wpm_json_err("args")); break;
  }
}

// Streamed in chunks (chunked transfer, like /getpresets): WiFi101 rejects a single send over
// 1400 bytes, which a full sweep would exceed. Heap use stays under ~500 bytes.
static void sendChunk(WiFiClient& client, String& chunk) {
  client.println(chunk.length(), HEX);
  client.println(chunk);
  chunk = "";
}

void wpm_bus_handle_scan_status(WiFiClient& client) {
  writeHeader(client, "HTTP/1.1 200 OK", "Content-type:application/json", -1);
  char buf[96];
  wpm_scan_status_head_json(scan.state(), scan.idx(), scan.count(), buf, sizeof buf);
  String chunk = buf;
  size_t n = scan.count();
  // Read the display cache once (not per entry): [addr][7 chars].
  uint8_t c[1 + WPM_SCAN_VER_LEN];
  for (size_t k = 0; k < sizeof c; k++) c[k] = framRead(DISPLAY_CACHE_ADDR + (int)k);
  bool cache_ok = false;
  for (size_t k = 1; k < sizeof c; k++)
    if (c[k]) cache_ok = true;
  for (size_t i = 0; i < n; i++) {
    if (i) chunk += ",";
    char e[112];
    WpmScanFound f = scan.found()[i];
    if (!f.ver_valid && cache_ok && c[0] == f.addr) {  // fall back to the stock display cache
      for (size_t k = 0; k < WPM_SCAN_VER_LEN; k++) f.ver[k] = (char)c[1 + k];
      f.ver_valid = true;
      f.ver_src = WPM_VER_SRC_CACHE;
    }
    if (wpm_scan_entry_json(f, e, sizeof e)) chunk += e;
    if (chunk.length() >= 400) sendChunk(client, chunk);
  }
  chunk += "]}";
  sendChunk(client, chunk);
  client.println("0");
  client.println();
}

// last_error mirrors the scan's last_error(): start() sets it (busy/args) and clears it on
// success, query_failed() sets 4, and /bus/polldone overrides it with 4 via set_error().
void wpm_bus_handle_status(WiFiClient& client) {
  bool seen = frame_seen;
  // Quiet: no slave frame and no module read for >= 50 ms (no frame ever counts as quiet).
  bool quiet = (!seen || (uint32_t)(millis() - last_frame_ms) >= BUS_QUIET_MS) && !readRecent(BUS_QUIET_MS) &&
               !wpm_card_write_recent(BUS_QUIET_MS);
  int dialect = seen ? (wpm_v2_pin() ? 2 : 1) : 0;
  String body = "{\"wpm_present\":true,\"bus_quiet\":";
  body += quiet ? "true" : "false";
  body += ",\"dialect\":";
  body += dialect;
  body += ",\"last_error\":";
  body += (int)scan.last_error();
  body += "}";
  wpm_send_json(client, body);
}

void wpm_bus_handle_polldone(WiFiClient& client) {
  if (wpm_bus_job_busy() || readRecent(READ_BUSY_MS)) {
    scan.set_error(WPM_BUS_ERR_BUSY);
    wpm_send_json(client, wpm_json_err("busy"));
    return;
  }
  wpm_job_master_begin();
  bool sent = wpm_bus_master_polldone();
  wpm_job_master_end();
  if (!sent) {
    scan.set_error(WPM_BUS_ERR_SEND_TIMEOUT);
    wpm_send_json(client, wpm_json_err("send_timeout"));
    return;
  }
  wpm_send_json(client, "{\"ok\":true}");
}
