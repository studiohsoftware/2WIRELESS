# PR 1: Bus Scan and Status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The WPM can discover which 200e modules are on its bus (`/bus/scan`, `/bus/scan/status`), report bus health (`/bus/status`) and send the bus POLL_DONE frame on demand (`/bus/polldone`), mirroring Xenomorphicle's SCAN / BUS_STATUS / POLL_DONE, as firmware 1.20 with capability `bus`.

**Architecture:** A pure, desktop-tested scan state machine in `WpmCommon` (`wpm_bus_scan`) drives one QUERY (0x1A) per address from `loop()`, with a 300 ms reply window and a 150 ms gap; the I2C slave ISR reports the module's reply into a one-slot mailbox the state machine consumes. **Bench correction (2026-10-22 17:00):** a module answers QUERY with the 5-byte identity ACK `04 22 <addr> 1C FF` (byte 1 may also be `00`), never with a 0x13 version frame (0x13 only follows a front-panel hold). `found[]` is therefore driven by the 0x1C ACK; `ver_valid` is true only when a 0x13 frame from that address arrived in the window or the stock display cache at FRAM 0x02C0FE (`[addr][7 chars]`, one slot) names that address; otherwise `ver_valid:false` and `version` is seven spaces. Each entry also says where the version came from: `"ver_src":"frame"|"cache"|"none"` (agreed with the app session 17:05), so a stale cached version is never mistaken for a fresh one. A thin device layer (`2Wireless/src/wpm_bus.cpp`) owns the routes, the `loop()` hook, and the guarded master sends. Stock routes, pages and FRAM layout are untouched; the new routes answer HTTP 200 JSON like PR 0's.

**Tech Stack:** Arduino SAMD core 1.8.14 (C++17), WiFi101, the vendored `WireByte`; desktop tests with the existing `Firmware/arduino/test/` minitest harness (clang++, `-Werror`); bench over Wi-Fi with curl bound to `en1`.

**Spec:** `docs/superpowers/specs/2026-10-21-wpm-firmware-prs-design.md` §3 (contract), §4 (facts), §5 "(call) Jobs run in loop()", §6 "PR 1", §6a (field meanings), §7. Branch `worktree-pr1-bus-scan`, stacked on PR 0 (`worktree-pr0-ota-loader`, b6d58a1).

## Global Constraints

- Compatibility contract (spec §3): every existing route keeps URL, parameter order, reply shape and default behaviour; stock pages work unchanged; FRAM layout untouched (PR 1 stores nothing in FRAM); boot timing unchanged; new behaviour only through new routes; refusals are HTTP 200 `{"ok":false,"error":"<code>"}`; a `v2/` prefix keeps its stock meaning.
- Route dispatch in `2Wireless.ino` is first-match `indexOf` over the request line: longer paths are matched before their prefixes (`/bus/scan/status` before `/bus/scan`, `/bus/status` is distinct from both).
- `2Wireless.ino` uses CRLF line endings; every edit preserves them (byte-preserving patch, CRLF count unchanged except for added lines, which are also CRLF).
- No heap allocation in the ISR beyond what stock already does; the ISR hook writes volatiles only.
- Timings (spec §6 PR 1 and Xenomorphicle): reply window `WPM_SCAN_REPLY_MS = 300`, gap `WPM_SCAN_GAP_MS = 150`; address range `0x08..0x77` inclusive; `first > last` is an empty sweep that ends `done` with no entries.
- Version: `FW_VERSION` "1.20"; `/version` `capabilities` becomes `["version","ota","bus"]`. The `/version` shape is otherwise unchanged.
- Error codes on the new routes: `busy` (a scan is running), `args` (address outside 0x08..0x77 or unparsable), `unsupported` (QUERY is not defined on a V2 bus: `v2version` pin set), `send_timeout` (the master send failed: bus error before begin). `/bus/status.last_error` numbers follow §6a: 0 none, 1 busy, 2 bad args, 4 send timeout, 5 no response (3 is never produced by PR 1).
- `/ota/commit` and a running scan are mutually exclusive: `wpm_ota_busy()` returns true while a scan is `scanning`; `/bus/scan` answers `busy` while a scan is running (there is no other job in 1.20).
- Agreed with the app session (2026-10-22 15:10): `/bus/scan` takes an optional trailing `window=MS` (per-address reply window, default 300, clamped to 100..2000; the gap stays 150); `first`/`last` parse case-insensitively; `first == last` is legal and costs one window plus one gap (~450 ms), the app uses it as a one-address version read-back; while a scan runs, the bus-emitting stock routes `/getpresets?addr` and `/setpresets?addr` answer `{"ok":false,"error":"busy"}` (a state a stock client can never reach, so stock behaviour is unchanged), while `/readmemory`, `/writememory`, `/presetnames` and the rest stay available. A capture cannot overlap a scan start because `/getpresets` completes inside one request.
- Commit trailers on every commit: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01TT8DVnxoAbrr5uYq3D89kJ`.

## Review Focus

1. A stray 0x13 frame from another module during the window (a live WPM elsewhere, a module announcing itself) must not be reported as the queried address's answer, and a reply arriving after the window must not be retro-fitted. Pinned by Task 1 tests `stray_reply_ignored` and `late_reply_ignored`.
2. The version string in `found[]` must always yield valid JSON even if a module sends a quote, a backslash, a control byte or a byte ≥ 0x7F. Pinned by Task 1 test `entry_json_escapes`.
3. `/bus/scan?first=77&last=08` (reversed) and `/bus/scan?first=00` (below 0x08) must not start a sweep that masters frames at reserved addresses. Pinned by Task 1 `start_rejects_bad_args` and `reversed_range_is_empty_done`.
4. The scan must never master while the slave ISR is receiving (`i2c_guard`) and must survive a bus error on one address without aborting the sweep. Pinned by Task 1 `send_failure_continues_sweep` and the device-side use of `masterBeginTransmission` (Task 2) plus the bench "scan during preset traffic" step (Task 3).
5. Stock routes containing "bus" or "status" substrings must not be shadowed: `/getsetupdata1`, `/usbMode`, `/ssid` contain neither; `/currentpreset` does not match `/bus/status`. Pinned by Task 2's dispatch placement test (bench: every stock route in RESULTS B3 answers unchanged) and the desktop test `route_match_order` in Task 1 (a pure helper picks the longest match).

---

### Task 1: `WpmCommon` scan state machine and JSON (desktop-tested)

**Files:**
- Create: `Firmware/arduino/libraries/WpmCommon/src/wpm_bus_scan.h`, `src/wpm_bus_scan.cpp`
- Create: `Firmware/arduino/test/test_bus_scan.cpp`

**Interfaces:**
- Produces (pure C++17, no Arduino includes, `<cstdint>`/`<cstddef>`/`<cstring>` only):
  ```
  constexpr uint8_t  WPM_SCAN_ADDR_MIN = 0x08;
  constexpr uint8_t  WPM_SCAN_ADDR_MAX = 0x77;
  constexpr size_t   WPM_SCAN_MAX_FOUND = 112;     // 0x08..0x77 inclusive
  constexpr uint32_t WPM_SCAN_REPLY_MS = 300;
  constexpr uint32_t WPM_SCAN_GAP_MS = 150;
  constexpr size_t   WPM_SCAN_VER_LEN = 7;

  enum WpmScanState { WPM_SCAN_IDLE, WPM_SCAN_SCANNING, WPM_SCAN_DONE };
  enum WpmScanAction { WPM_SCAN_ACT_NONE, WPM_SCAN_ACT_QUERY };   // QUERY: caller masters a QUERY to current_addr()
  enum WpmBusError { WPM_BUS_ERR_NONE = 0, WPM_BUS_ERR_BUSY = 1, WPM_BUS_ERR_ARGS = 2,
                     WPM_BUS_ERR_SEND_TIMEOUT = 4, WPM_BUS_ERR_NO_RESPONSE = 5 };

  struct WpmScanFound { uint8_t addr; bool ver_valid; char ver[WPM_SCAN_VER_LEN]; };

  class WpmBusScan {
   public:
    // Refuses with BUSY while scanning, ARGS if either bound is outside 0x08..0x77.
    // first > last is accepted and ends DONE at the first step() with no entries.
    WpmBusError start(uint8_t first, uint8_t last, uint32_t now_ms);
    // Advances the sweep. Returns ACT_QUERY exactly once per address (the caller then masters
    // the QUERY to current_addr() and reports failure with query_failed(now)); otherwise NONE.
    WpmScanAction step(uint32_t now_ms);
    void query_failed(uint32_t now_ms);                 // sets last_error SEND_TIMEOUT, skips to the gap
    // ISR-side: a 0x13 frame from from_addr with n version bytes (n <= 7). Writes a one-slot
    // mailbox only; step() consumes it. A frame for another address, or outside a window, is
    // counted in stray_replies() and otherwise ignored.
    void on_version_frame(uint8_t from_addr, const uint8_t* ver, size_t n);
    WpmScanState state() const; uint8_t current_addr() const;
    size_t idx() const;                                 // addresses completed so far in this sweep
    size_t count() const;                               // entries in found()
    const WpmScanFound* found() const;                  // count() entries, in sweep order
    WpmBusError last_error() const;                     // persists until the next start()/clear_error()
    void clear_error();
    uint32_t stray_replies() const;
  };

  const char* wpm_scan_state_str(WpmScanState s);      // "idle" | "scanning" | "done"
  // One found entry: {"addr":"28","ver_valid":true,"version":"31.0   "}; addr is 2 lower-case hex
  // digits; version is exactly 7 characters after escaping: '"' and '\\' are escaped with a
  // backslash, any byte < 0x20 or >= 0x7F becomes '.'. Returns the length written (buf >= 64).
  size_t wpm_scan_entry_json(const WpmScanFound& f, char* buf, size_t n);
  // Header up to and including "found":[  : {"state":"scanning","idx":3,"count":1,"found":[
  size_t wpm_scan_status_head_json(WpmScanState s, size_t idx, size_t count, char* buf, size_t n);
  // Longest-prefix route pick for the dispatch order test: returns the index of the longest
  // candidate that occurs in `line`, or -1. Candidates are the four PR 1 routes.
  int wpm_bus_route_match(const char* line, const char* const* candidates, size_t n);
  ```
- The mailbox: `volatile` fields `mb_addr`, `mb_len`, `mb_ver[7]`, `mb_full`; `on_version_frame` writes them only when `mb_full` is false; `step()` reads and clears. `ver_valid` is true iff `n == 7`; with `n < 7` the entry is still recorded with `ver_valid:false` and `version` padded with spaces (the module answered).

- [ ] **Step 1: Write the failing tests** in `test/test_bus_scan.cpp` (minitest style as `test_ota_record.cpp`; a fake clock variable `now`):

  - `start_rejects_bad_args`: `start(0x07,0x28,0)==WPM_BUS_ERR_ARGS`, `start(0x28,0x78,0)==ARGS`, state stays IDLE, `last_error()==ARGS`.
  - `reversed_range_is_empty_done`: `start(0x30,0x28,0)==NONE`; `step(0)==ACT_NONE`; `state()==DONE`; `count()==0`; `idx()==0`.
  - `busy_while_scanning`: `start(0x28,0x29,0)==NONE`; `start(0x28,0x29,0)==BUSY`; `last_error()==BUSY`.
  - `single_address_answers`: `start(0x28,0x28,1000)`; `step(1000)==ACT_QUERY`, `current_addr()==0x28`; `step(1100)==ACT_NONE`; `on_version_frame(0x28,"31.0   ",7)`; `step(1150)==ACT_NONE`; `state()==SCANNING` until the gap: `step(1150+150)` → `state()==DONE`, `count()==1`, `found()[0].addr==0x28`, `ver_valid`, `memcmp(ver,"31.0   ",7)==0`, `idx()==1`.
  - `no_answer_times_out`: `start(0x10,0x10,0)`; `step(0)==ACT_QUERY`; `step(299)==ACT_NONE`, SCANNING; `step(300)` → gap; `step(449)` SCANNING; `step(450)` → DONE, `count()==0`, `last_error()==NONE` (a silent address is not an error).
  - `two_addresses_second_answers`: `start(0x10,0x11,0)`; query 0x10 at 0; no reply; at 450 `step` returns ACT_QUERY with `current_addr()==0x11`; reply for 0x11 at 500 → found 1 entry addr 0x11; DONE after the gap; `idx()==2`.
  - `stray_reply_ignored`: during 0x28's window, `on_version_frame(0x44,"259e   ",7)` → not recorded, `stray_replies()==1`; then the real 0x28 reply is recorded.
  - `late_reply_ignored`: 0x28 times out (gap entered); `on_version_frame(0x28,...)` during the gap → `count()==0`, `stray_replies()==1`.
  - `short_version_marks_invalid`: reply with 4 bytes "251e" → entry recorded, `ver_valid==false`, `ver=="251e   "`.
  - `send_failure_continues_sweep`: `start(0x10,0x11,0)`; `step(0)==ACT_QUERY`; `query_failed(0)`; `last_error()==SEND_TIMEOUT`; `step(150)` returns ACT_QUERY for 0x11 (no 300 ms wait after a failed send); sweep completes DONE.
  - `restart_after_done_clears`: after a DONE sweep with 1 entry, `start(...)` again → `count()==0`, `idx()==0`, `stray_replies()==0`, `last_error()==NONE`.
  - `entry_json_exact`: `{"addr":"28","ver_valid":true,"version":"31.0   "}` for the 0x28 entry; `{"addr":"5c","ver_valid":false,"version":"251e   "}` for the short one.
  - `entry_json_escapes`: ver bytes `"a\"b\\c\x01\x80"` → `"version":"a\"b\\c.."` (7 visible characters: `a`, `\"`, `b`, `\\`, `c`, `.`, `.`).
  - `status_head_exact`: `{"state":"scanning","idx":3,"count":1,"found":[` and `{"state":"idle","idx":0,"count":0,"found":[`.
  - `route_match_order`: candidates `{"/bus/scan/status","/bus/scan","/bus/status","/bus/polldone"}`; `"GET /bus/scan/status HTTP/1.1"` → 0; `"GET /bus/scan?first=08&last=77 HTTP/1.1"` → 1; `"GET /bus/status HTTP/1.1"` → 2; `"GET /currentpreset HTTP/1.1"` → -1.
  - `found_cap`: a sweep 0x08..0x77 where every address answers records exactly 112 entries and does not overrun (the 113th is impossible; assert `count()==WPM_SCAN_MAX_FOUND`).

- [ ] **Step 2: Run to verify failure**: `make -C Firmware/arduino/test` → compile error (header missing).
- [ ] **Step 3: Implement** `wpm_bus_scan.h/.cpp` per the interface. Phases inside SCANNING: SEND → WAIT (300 ms) → GAP (150 ms) → next. `query_failed` moves straight to GAP.
- [ ] **Step 4: Run tests** → all suites `0 failures`, `-Werror` clean.
- [ ] **Step 5: Commit** `feat(common): bus scan state machine and status JSON with desktop tests`

### Task 2: Device routes, loop hook, ISR hook, version 1.20

**Files:**
- Create: `Firmware/arduino/2Wireless/src/wpm_bus.h`, `src/wpm_bus.cpp`
- Modify: `Firmware/arduino/2Wireless/2Wireless.ino` (CRLF; byte-preserving edits): `receiveEvent` DISPLAY branch, `loop()`, the new-route dispatch block (near `/ota/status`), include list; add `bool wpm_bus_master_query(uint8_t addr)` and `bool wpm_bus_master_polldone()` next to `sendQuery`/`sendPollingComplete`.
- Modify: `Firmware/arduino/2Wireless/src/wpm_ota.cpp` (`wpm_ota_busy()`), `src/wpm_version.h` or wherever `FW_VERSION` is defined (→ "1.20"), `libraries/WpmCommon/src/wpm_version_format.cpp` (capabilities), `test/test_version_json.cpp` (expected string).

**Interfaces:**
- Consumes Task 1's `WpmBusScan` and JSON helpers; the sketch's `v2version`, `masterBeginTransmission(int)`, `switchToSlave()`, `Wire`, `i2c_guard`, `wpm_send_json`, `wpm_json_err`, `millis()`.
- Produces (`wpm_bus.h`):
  ```
  void wpm_bus_loop();                                     // call once per loop() pass
  void wpm_bus_on_version_frame(uint8_t addr, const uint8_t ver[7]);  // from receiveEvent (ISR)
  void wpm_bus_note_frame();                               // from receiveEvent for every >4-byte frame (ISR)
  bool wpm_bus_scan_active();                              // SCANNING
  void wpm_bus_handle_scan(WiFiClient&, const String& urlString);        // /bus/scan?first=HH&last=HH
  void wpm_bus_handle_scan_status(WiFiClient&);                          // /bus/scan/status
  void wpm_bus_handle_status(WiFiClient&);                               // /bus/status
  void wpm_bus_handle_polldone(WiFiClient&);                             // /bus/polldone
  ```
  Sketch-side helpers (in `2Wireless.ino`, mirroring `sendQuery`/`sendPollingComplete` but returning success): `bool wpm_bus_master_query(uint8_t addr)` sends `04 <addr> 22 1A FF` via `masterBeginTransmission(0)`; returns false if that returned non-zero (bus error) and never masters on `v2version`; `bool wpm_bus_master_polldone()` sends the stock POLL_DONE frame (`04 00 22 14 FF`, or `14` on V2) the same way.

- [ ] **Step 1: ISR hook.** In `receiveEvent`'s DISPLAY_EVENT branch, collect the 7 version bytes into a local `uint8_t ver7[7]` as they are read (keep the stock `String` build and `setDisplayMessage` call unchanged), then call `wpm_bus_on_version_frame(moduleAddr, ver7)`. At the top of the `Wire.available() > 4` branch call `wpm_bus_note_frame()`. Both hooks only write volatiles (`wpm_bus.cpp` keeps `volatile uint32_t last_frame_ms`, the `WpmBusScan` mailbox).
- [ ] **Step 2: Routes.** `/bus/scan`: parse `first=` and `last=` as 2 hex digits, either case (defaults `08` and `77` when absent), and optional `window=` decimal ms clamped to 100..2000 (default 300) passed as `start()`'s `reply_ms`; `v2version` → `{"ok":false,"error":"unsupported"}`; `start()` BUSY → `"busy"`, ARGS → `"args"`; success `{"ok":true}`. At the top of the stock `/getpresets?addr` and `/setpresets?addr` handlers add a guard: if `wpm_bus_scan_active()` → `wpm_send_json(client, wpm_json_err("busy"))` and skip the stock body (the stock header write must not happen first). `/bus/scan/status`: build `String` = head + entries joined by `,` + `]}` and `wpm_send_json`. `/bus/status`: `{"wpm_present":true,"bus_quiet":<true if no frame for >= 50 ms or none ever>,"dialect":<0 before any frame, else v2version ? 2 : 1>,"last_error":<scan last_error number>}`. `/bus/polldone`: scanning → `"busy"`; `wpm_bus_master_polldone()` false → `"send_timeout"` and last_error 4; else `{"ok":true}`.
- [ ] **Step 3: loop hook.** `wpm_bus_loop()`: `switch (scan.step(millis()))`: on ACT_QUERY call `wpm_bus_master_query(scan.current_addr())`; on false call `scan.query_failed(millis())`. Insert the call at the top of `loop()` after the WPM_DIAG block. Never called from the HTTP handler (the sweep runs between requests, so `/bus/scan/status` is answerable during a scan; each `loop()` pass handles one client request, which can take up to ~1 s for a slow route, so the 300 ms window may stretch; accepted).
- [ ] **Step 4: Dispatch.** In the new-route block, before `/ota/status`, add in this order: `/bus/scan/status`, `/bus/scan`, `/bus/status`, `/bus/polldone`.
- [ ] **Step 5: Version.** `FW_VERSION` "1.20"; capabilities `["version","ota","bus"]`; `wpm_ota_busy()` returns `wpm_bus_scan_active()`. Update `test_version_json.cpp` expectations. Run `make -C Firmware/arduino/test` → 0 failures.
- [ ] **Step 6: Build.** `bash Firmware/arduino/build.sh 2Wireless --profile wpm` compiles clean (`--warnings default`), prints the stamped image, loader unchanged (BFF0ADDF). Check `grep -c $'\r$' 2Wireless.ino` equals its line count.
- [ ] **Step 7: Commit** `feat(bus): /bus/scan, /bus/scan/status, /bus/status, /bus/polldone; firmware 1.20`

### Task 3: Bench script and device bench

**Files:**
- Create: `Firmware/arduino/bench/bus_scan.py` (start a scan over a range, poll `/bus/scan/status` every 2 s until `done`, print a table `addr version ver_valid`, print elapsed; all sockets bound to `--iface en1`; exit 0 on done, 1 on error/busy/timeout; `--first/--last` default 08/77; `--status-only`).
- Modify: `Firmware/arduino/bench/RESULTS.md` (new "PR 1" section)

- [ ] **Step 1:** Write `bus_scan.py` reusing `ota_push.py`'s bound-connect and request helpers (copy the two functions; no shared module yet).
- [ ] **Step 2 (controller + device; owner not needed unless the unit is off):** flash the Task 2 build over USB, `wifi_rejoin.sh`, then:
  1. `/version` → 1.20, capabilities include "bus".
  2. `/bus/status` → `wpm_present:true`, `last_error:0`; note `bus_quiet` and `dialect`.
  3. `python3 bench/bus_scan.py --first 28 --last 28` → found 0x28 with a 7-char version (259e), elapsed < 2 s.
  4. `bus_scan.py --first 5c --last 5c` → found 0x5c (251e).
  5. Full sweep `bus_scan.py` (08..77) → expected about 50 s; the found list is the module table of Oren's case: record it.
  6. `/bus/scan` while a sweep runs → `busy`; `/ota/commit?len=2&crc32=0` while a sweep runs → `busy` (first time PR 0's busy path is observable).
  7. `/bus/scan?first=00&last=05` → `args`; `/bus/scan?first=30&last=28` → `ok`, status `done` with `count:0` right away.
  8. `/bus/polldone` → `{"ok":true}`; the 259e still answers `/currentpreset` and a `/getpresets?addr=28` capture equals cap1 (non-destructive equivalence check as in PR 0).
  9. Stock routes unchanged: `/ssid`, `/currentpreset`, `/setup` "Firmware v1.20", `/getpresets?addr=76` empty.
  10. Scan during traffic: start a full sweep and, while it runs, poll `/currentpreset` every second for 20 s: every poll answers and the sweep completes with the same found list as step 5.
- [ ] **Step 3:** Record every result in `RESULTS.md`; commit `bench(bus): scan script and PR 1 device results`.

### Task 4: Docs, spec freeze, release 1.20

**Files:**
- Create: `docs/pr/PR1.md` (summary, routes with exact JSON, contract statement, what was bench-tested, limitations, how to reproduce).
- Modify: `Firmware/arduino/README.md` (routes table/section for `/bus/*`, bench script usage), the spec §6 PR 1 (freeze the keys as implemented: error codes `busy|args|unsupported|send_timeout`, `idx`/`count` meaning, `/bus/polldone`, the mailbox/stray rule), `Firmware/arduino/2Wireless/bin/` (`2Wireless-1.20.ino.bin`, `2Wireless-1.20.ota.bin`, `SHA256SUMS` gains two lines; the 1.19 pair stays).
- [ ] **Step 1:** Docs as above; the Mac-app session has been sent the key proposals (controller does this; incorporate any objection it raised).
- [ ] **Step 2:** `bash Firmware/arduino/build.sh 2Wireless --profile wpm` (untagged) → copy to the `-1.20` names, regenerate `SHA256SUMS` (all five files), flash from `bin/`, rerun bench steps 1, 3, 8 and 9 from the committed file, append to RESULTS.md.
- [ ] **Step 3: Commit** `release: 2Wireless 1.20 binaries (bus scan and status)`.
