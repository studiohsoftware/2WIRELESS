# PR 2: Byte-Counting Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A preset capture that works for every module, slow ones included: `/dump?addr=XX[&len=N][&first_wait_ms=M][&silence_ms=S]` runs a BACKUP as a background job that waits generously for the first byte, applies each line's 2-byte card offset into FRAM (a clean flat bank from 0), counts bytes, recognises the module's completion frame, and reports through `/dump/status`; firmware 1.21 with capability `dump`. `/getpresets` is untouched.

**Architecture:** A pure, desktop-tested job state machine in `WpmCommon` (`wpm_dump`) mirrors Xenomorphicle's master capture states (idle, sending, waiting, transferring, done, failed) and its error codes. The slave ISR, when a dump is active, parses each received I2C frame instead of the stock "cache every byte" path: a 5-byte `04 xx <addr> 0A xx` is the completion frame; anything else is `[offset hi][offset lo][data…]` written to FRAM at the offset. The main loop advances the job on time and on the ISR's counters (a byte arrived iff the counter changed). A thin device layer owns the routes, the loop hook and the guarded BACKUP send, exactly as PR 1 did for the scan.

**Tech Stack:** Arduino SAMD core 1.8.14 (C++17), WiFi101, vendored `WireByte` (onReceive fires once per I2C transaction with the whole frame); desktop tests with `Firmware/arduino/test/` minitest (clang++, `-Werror`); bench over Wi-Fi with curl bound to `en1`.

**Spec:** `docs/superpowers/specs/2026-10-21-wpm-firmware-prs-design.md` §3, §4 (`receiveEvent` FRAM mode; `/getpresets` ordering bug and 1 s silence race; the module→manager completion frame), §5 "(call) Capture (PR 2) keys on write_counter" and "(call) Jobs run in loop()", §6 "PR 2", §6a. Branch `worktree-pr2-dump`, stacked on PR 1 (`worktree-pr1-bus-scan`).

## Global Constraints

- Compatibility contract (spec §3): stock routes, pages, FRAM layout, boot timing unchanged; new behaviour only through new routes; refusals HTTP 200 `{"ok":false,"error":"<code>"}`; `v2/` prefix keeps its meaning. PR 2 stores nothing new in FRAM beyond the captured data in the scratch area `0..0x2BD18` that `/getpresets` already uses.
- `2Wireless.ino` is CRLF; every edit is byte-preserving. Route dispatch is first-match `indexOf`: `/dump/status` and `/dump/abort` are matched before `/dump`, and all three before `/ota/status`; no stock route contains "/dump".
- ISR rules: the dump frame handler runs inside `receiveEvent` (I2C ISR); it may call `framWrite` (stock does) and write volatiles; no allocation, no `String`, bounded loops over `howMany` (≤ 256, the ring buffer).
- Frame facts (spec §4, RESULTS.md, Xenomorphicle 200e-conformance.md): a BACKUP line is the module writing to the card: an I2C transaction addressed to the card (slave address 0x50) of the form `[offset_hi][offset_lo][data…]` (big-endian u16 offset, data up to 66 bytes on the 259e); commands, MIDI clock, display frames and the 5-byte completion frame `04 22 <addr> 0A <x>` (the 259e uses `04 00 28 0A 28`) are general-call transactions (address 0x00). Match the completion frame with `howMany == 5 && b0 == 0x04 && b3 == 0x0A && b2 == job addr`. The 259e's flat bank is 990 bytes (cap1); the 272e at 0x5E has a 750-byte Xenomorpher reference bank (`~/Documents/GitHub/200e_bus_protocol/modules/272e-bank-2026-10-17-reference.bin`); the 251e's is larger (2104-byte slots) and slow to start.
- **Layout guarantee (agreed with the app session 18:20):** the flat bank at FRAM 0 is byte-identical to a Xenomorpher GET_DUMP of the same module; `extent != received` means holes and the app refuses the backup. Benched once against the 272e reference (Task 3) and stated in PR2.md.
- **Matched-address filter (experiment, Task 2):** the SERCOM I2C slave matches both 0x50 and the general call; if the vendored Wire can expose which address matched for the current transaction (SAMD21 I2CS: the address byte is in `DATA` when `AMATCH` is set), the dump handler applies data lines only from transactions addressed to 0x50 and leaves general-call frames to the stock MIDI/DISPLAY path (plus completion detection); the exposure to clock/display frames then disappears. If the bench shows the address is not readable, fall back to shape parsing and, when `len` is known, count lines whose offset lands at or beyond `len` as `dropped` instead of writing them. PR2.md states which path shipped.
- Timings and limits: `first_wait_ms` default 5000, clamp 200..60000; `silence_ms` default 1000 (stock behaviour), clamp 200..20000; `len` (expected bytes) default 0 = unknown (silence mode), else 1..0x2BD18; data beyond FRAM `0x2BD17` is dropped and counted in `dropped`.
- Job end rules: `done` when the completion frame from the job's address arrives, or when `len` is known and `received >= len`, or when the silence window elapses after the first byte (silence mode and known-len alike: the client compares `received` with what it expected); `failed` with `no_response` (5) when no byte arrives within `first_wait_ms`; `failed` with `send_timeout` (4) when the BACKUP could not be mastered.
- Mutual exclusion: while a dump runs, `/bus/scan`, `/bus/polldone`, `/getpresets?addr`, `/setpresets?addr` and `/ota/commit` answer `busy`; `/dump` answers `busy` while a scan or another dump runs; `/readmemory`, `/writememory` stay available (the client reads the bank with `/readmemory`). `wpm_ota_busy()` = scan active || dump active.
- Error codes (§6a): 0 none, 1 busy, 2 bad args, 4 send timeout, 5 no response (3 never).
- Version: `FW_VERSION` "1.21"; capabilities `["version","ota","bus","dump"]`.
- Commit trailers: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01TT8DVnxoAbrr5uYq3D89kJ`.

## Review Focus

1. A MIDI-clock or display frame arriving mid-dump (a 252e sending clock) is indistinguishable from a data line by shape and would be written at a bogus offset; stock has the same exposure (it caches every byte). Pinned by documenting it and by the Task 1 test `short_frame_ignored` (frames of fewer than 3 bytes are ignored; 5-byte non-completion frames are treated as data because a 3-byte line is legal).
2. The first-byte wait must not be ended by a stray frame from another module (the window logic counts only data written for this job). Pinned by Task 1 `first_wait_counts_only_data`.
3. `received` must be computed from the ISR counters snapshot taken in `step()`, never from `Wire.available()` (spec §4's race). Pinned by Task 1 `activity_is_counter_change`.
4. A `/getpresets` capture and a `/dump` must never run together (two writers to FRAM 0). Pinned by the Task 2 busy guards and bench step 7.
5. The offset write must be bounded to the scratch area so a corrupt offset cannot reach the settings or the OTA record. Pinned by Task 1 `offset_out_of_range_dropped` and the ISR bound check in Task 2.

---

### Task 1: `WpmCommon` dump job state machine and JSON (desktop-tested)

**Files:**
- Create: `Firmware/arduino/libraries/WpmCommon/src/wpm_dump.h`, `src/wpm_dump.cpp`
- Create: `Firmware/arduino/test/test_dump.cpp`

**Interfaces:**
```
constexpr uint32_t WPM_DUMP_SCRATCH_END = 0x2BD18;      // exclusive; same as WPM_OTA_MAX_LEN
constexpr uint32_t WPM_DUMP_FIRST_WAIT_DEFAULT_MS = 5000, WPM_DUMP_FIRST_WAIT_MIN_MS = 200, WPM_DUMP_FIRST_WAIT_MAX_MS = 60000;
constexpr uint32_t WPM_DUMP_SILENCE_DEFAULT_MS = 1000, WPM_DUMP_SILENCE_MIN_MS = 200, WPM_DUMP_SILENCE_MAX_MS = 20000;

enum WpmDumpState { WPM_DUMP_IDLE, WPM_DUMP_SENDING, WPM_DUMP_WAITING, WPM_DUMP_TRANSFERRING, WPM_DUMP_DONE, WPM_DUMP_FAILED };
enum WpmDumpAction { WPM_DUMP_ACT_NONE, WPM_DUMP_ACT_SEND_BACKUP };

class WpmDumpJob {
 public:
  // BUSY while sending/waiting/transferring; ARGS for addr outside 0x08..0x77, expected > WPM_DUMP_SCRATCH_END,
  // first_wait or silence outside their clamps (the route clamps; the class refuses).
  WpmBusError start(uint8_t addr, uint32_t expected, uint32_t first_wait_ms, uint32_t silence_ms, uint32_t now_ms);
  WpmDumpAction step(uint32_t now_ms);          // SEND_BACKUP exactly once, right after start
  void send_failed(uint32_t now_ms);             // -> FAILED, error SEND_TIMEOUT
  void abort(uint32_t now_ms);                   // any state -> IDLE (counters kept for status until the next start)
  // ISR side, volatile writes only:
  // A data line landed: n bytes written at offset (after the caller bounded it). Updates received, extent, lines.
  void on_data(uint32_t offset, uint32_t n);
  void on_dropped(uint32_t n);                   // bytes beyond the scratch area, not written
  void on_complete_frame(uint8_t from_addr);     // counted as stray if from another address
  // Accessors: state(), addr(), expected(), received(), extent() (max offset+n), lines(), dropped(),
  // complete_frame(), error() (WpmBusError), elapsed_ms(now) (since start), stray_frames().
};
const char* wpm_dump_state_str(WpmDumpState s);  // "idle"|"sending"|"waiting"|"transferring"|"done"|"failed"
// {"state":"transferring","addr":"28","expected":990,"received":528,"extent":528,"lines":8,"dropped":0,
//  "complete_frame":false,"error":0,"elapsed_ms":1234}
size_t wpm_dump_status_json(const WpmDumpJob& j, uint32_t now_ms, char* buf, size_t n);   // buf >= 192
```
`step()` rules: SENDING → returns SEND_BACKUP and enters WAITING with `t_start = now`; WAITING: if `received` changed since the last step → TRANSFERRING with `t_last = now`; else if `now - t_start >= first_wait` → FAILED(NO_RESPONSE); TRANSFERRING: if the completion flag is set → DONE; else if `expected && received >= expected` → DONE; else if `received` changed → `t_last = now`; else if `now - t_last >= silence` → DONE. The completion flag is also honoured in WAITING (a module with an empty bank). `error()` is NONE on DONE.

- [ ] **Step 1: Tests** (`test_dump.cpp`, minitest): `start_rejects_args` (addr 0x07, expected 0x2BD19, first_wait 100, silence 30000); `busy_while_running`; `send_then_wait_then_fail_no_response` (start at 0 with first_wait 5000: step(0)==SEND_BACKUP, step(4999) WAITING, step(5000) FAILED, error 5); `send_failed_sets_timeout`; `data_starts_transfer` (on_data(0,66) at 300 → step(300) TRANSFERRING, received 66, extent 66, lines 1); `completion_frame_ends_done` (on_complete_frame(0x28) → next step DONE, complete_frame true, error 0); `stray_completion_ignored` (from 0x44 → stray_frames 1, still transferring); `expected_reached_ends_done` (expected 990: lines summing to 990 → DONE without a completion frame); `silence_ends_done` (silence 1000: last data at 2000, step(2999) TRANSFERRING, step(3000) DONE, complete_frame false); `activity_is_counter_change` (two steps with no counter change do not refresh t_last); `first_wait_counts_only_data` (a stray completion during WAITING does not end the wait); `offset_out_of_range_dropped` is a device concern but the class test `dropped_counts` checks on_dropped; `extent_vs_received` (lines at 0x100 then 0x000 → received 132, extent 0x142); `abort_returns_idle`; `millis_wraparound` (start at 0xFFFFFF00); `status_json_exact` for the example above and for idle (`{"state":"idle","addr":"00","expected":0,"received":0,"extent":0,"lines":0,"dropped":0,"complete_frame":false,"error":0,"elapsed_ms":0}`); `restart_clears_counters`.
- [ ] **Step 2:** `make -C Firmware/arduino/test` fails to compile. **Step 3:** implement. **Step 4:** all suites 0 failures. **Step 5: Commit** `feat(common): dump job state machine and status JSON with desktop tests`.

### Task 2: Device: ISR dump path, routes, loop hook, version 1.21

**Files:**
- Create: `Firmware/arduino/2Wireless/src/wpm_dump_dev.h`, `src/wpm_dump_dev.cpp` (routes `/dump`, `/dump/status`, `/dump/abort`; `wpm_dump_loop()`; `wpm_dump_active()`; `wpm_dump_isr_frame(int howMany)`).
- Modify: `2Wireless.ino` (CRLF): `receiveEvent` gets, before the stock `if (write_i2c_to_fram)` branch, `if (wpm_dump_active()) { wpm_dump_isr_frame(howMany); return; }`; a sketch helper `bool wpm_dump_master_backup(uint8_t addr)` mirroring `sendBackupPresets` byte for byte through `masterBeginTransmission(0)` and returning its success; `loop()` calls `wpm_dump_loop()` after `wpm_bus_loop()`; dispatch `/dump/status`, `/dump/abort`, `/dump` before `/bus/scan/status`; busy guards: `/bus/scan` and `/bus/polldone` (in `wpm_bus.cpp`) and the stock `/getpresets?addr`/`/setpresets?addr` guards extended to `wpm_bus_scan_active() || wpm_dump_active()`.
- Modify: `src/wpm_ota.cpp` (`wpm_ota_busy()` = scan || dump), `src/wpm_version.h` (1.21), `WpmCommon/src/wpm_version_format.cpp` (+"dump"), `test/test_version_json.cpp`.

**Matched address (WireByte):** in `src/WireByte.cpp` `onService()`, in the `isAddressMatch()` branch before the ACK command, capture `_lastAddress` from the raw register `sercom->getSercom()->I2CS.DATA.reg` (not through the core's `readDataWIRE()`, which has smart-mode side effects), and expose `uint8_t TwoWireByte::lastAddress() const` (7-bit, R/W bit stripped). Record the change in `src/WireByte.diff` and `WPM-CHANGES`-style comment. Bench step 2 decides whether it is reliable (data lines report 0x50, the completion frame 0x00); if not, the handler ignores it (compile-time `WPM_DUMP_ADDR_FILTER 0`) and uses the shape path.

**ISR frame handler** (`wpm_dump_isr_frame(howMany)`): read all `howMany` bytes with `Wire.read()` into a local `uint8_t f[256]`; `a = Wire.lastAddress()`. If `a == 0x00` (general call) or the filter is off: if `howMany == 5 && f[0] == 0x04 && f[3] == 0x0A` → `job.on_complete_frame(f[2])`; with the filter on, any other general-call frame is handed to the stock path (return to `receiveEvent`'s normal branch so MIDI clock and 0x13 frames keep working; implement by returning `false` from the handler and letting the stock code run with the bytes still available: so read the bytes only after deciding, i.e. peek `f[0..3]` via `Wire.peek()` is not available: instead, with the filter on, decide by address first: `a == 0x50` → data line; `a == 0x00` → read 5 bytes only if `howMany == 5` to test for the completion frame, otherwise do not consume and return `false`). Data line (`a == 0x50`, or filter off and not a completion frame): if `howMany >= 3` → `offset = (f[0] << 8) | f[1]`, `n = howMany - 2`; if `len` is known and `offset >= len` (filter off only) → `on_dropped(n)`; else write bytes `i` with `offset + i < WPM_DUMP_SCRATCH_END` via `framWrite`, count the rest as dropped; `job.on_data(offset, written)`; frames shorter than 3 bytes are ignored. No other work.

**Routes:** `/dump?addr=XX[&len=N][&first_wait_ms=M][&silence_ms=S]` (addr 2 hex digits either case, required; `len` decimal default 0; waits decimal, clamped to the constants; unknown/garbled → `args`); refusals: `busy` when a scan or dump runs; `send_timeout` is reported through status, not the start reply; success `{"ok":true}`. `/dump/status` → the JSON above via `wpm_send_json` (≤ 192 bytes). `/dump/abort` → `{"ok":true}` always.

- [ ] Steps: implement; `make -C Firmware/arduino/test` 0 failures; `bash Firmware/arduino/build.sh 2Wireless --profile wpm` 0 warnings; CRLF check; commit `feat(dump): /dump, /dump/status, /dump/abort byte-counting capture; firmware 1.21`.

### Task 3: Bench script and device bench

**Files:** Create `Firmware/arduino/bench/dump.py` (start `/dump` with the given args, poll `/dump/status` every 0.5 s until done/failed, print the status line, then read `/readmemory` for `extent` bytes and write them to `--out FILE`; `--compare FILE` compares byte-exact with a flat-bank file; exit 0 done+match, 1 otherwise). Modify `bench/RESULTS.md`.
- Reference flat bank for the 259e: produce `bench/ref/259e-cap1-flat.bin` from the stock capture cap1 with the de-interleave used by the restore script (990 bytes); commit it (it is Oren's module's preset bank; he owns it).
- [ ] Device steps (flash the Task 2 build; USB plugged; no owner hands needed):
  1. `/version` 1.21 with "dump".
  2. `dump.py --addr 28 --len 990 --compare bench/ref/259e-cap1-flat.bin` → done, received 990, extent 990, lines 37, complete_frame true, bytes identical to the stock reference. Record elapsed. Then the address experiment: a debug counter pair in `/dump/status` is not part of the contract; instead run the dump with a diagnostic build (`-DWPM_DUMP_ADDR_LOG`) that records the first eight matched addresses in RAM and prints them through `/dump/status` as `"addr_log":"50 50 50 00"` (diag build only), and confirm data lines show 0x50 and the completion frame 0x00. Decide the filter (Global Constraints) from this.
  2b. Layout guarantee: `dump.py --addr 5e --len 750 --compare ~/Documents/GitHub/200e_bus_protocol/modules/272e-bank-2026-10-17-reference.bin` → done, 750 bytes, byte-identical (sha256 equal). The 272e only writes the card after it has seen a bus POLL_DONE (0x14) since its power-up (`272e-preset-record-format.md`: otherwise it sends the completion frame with 0 bytes); Oren's WPM has poll-on-start off, so send `curl .../bus/polldone` (PR 1) before this dump and say so in PR2.md and in `dump.py --help`. A module runs the whole transfer with interrupts off and its bus master has no timeouts, so nothing may master the bus during a dump (the busy rules guarantee it).
  3. `dump.py --addr 28` (silence mode, no len) → same bytes; done by silence or completion frame; record which.
  4. `dump.py --addr 5c --first-wait 20000 --silence 3000` (251e, the module stock cannot capture) → record state, received, extent, lines, complete_frame, elapsed; read back and keep the file (`bench/ref/251e-dump.bin` is NOT committed unless Oren asks; keep it in the scratchpad). Repeat once; compare the two reads byte-exact.
  5. `dump.py --addr 76` (absent module) → failed, error 5, after first_wait.
  6. Busy rules: start a dump on 0x5c, then `/bus/scan`, `/getpresets?addr=28`, `/ota/commit?len=2&crc32=0`, a second `/dump` → all busy; `/dump/status` and `/readmemory` answer; `/dump/abort` → ok, status idle.
  7. Stock `/getpresets?addr=28` after all of this → capture identical to cap1 (nothing stock changed).
  8. `/bus/scan` single address still works after a dump (job hand-off clean).
- [ ] Commit `bench(dump): capture script, 259e reference bank, PR 2 device results`.

### Task 4: Docs, spec freeze, release 1.21

- `docs/pr/PR2.md`, README section, spec §6 PR 2 bullets frozen as built (confirm keys with the app session first: controller), `bin/2Wireless-1.21.{ino,ota}.bin` + SHA256SUMS; rerun bench steps 1, 2 and 7 from the committed file. Commit `release: 2Wireless 1.21 binaries (byte-counting capture)`.
