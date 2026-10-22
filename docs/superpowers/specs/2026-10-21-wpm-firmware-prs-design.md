# 2WIRELESS firmware pull requests — design (2026-10-21)

Owner: Oren Levy. Written by the 2WIRELESS Claude session from the bench work of 2026-10-21 and the
agreements reached with the "200e Bus Buccaneer" app session (its spec: `Buchla_FW/docs/superpowers/
specs/2026-10-20-200e-updater-app-design.md` §7a/§7b; bus command semantics:
`Xenomorphicle/docs/bus200e-sysex-fw.md`).

## 1. Intent and why

The Studio H Wireless Preset Manager (WPM) is the only bus-bridge most Buchla 200e owners will ever
own. Its HTTP API (firmware v1.18, `Firmware/arduino/2Wireless/2Wireless.ino`) can write a firmware
card image into FRAM and dump presets, but it cannot scan the bus, cannot capture presets from slow
modules (251e), cannot report what a module read, and cannot update itself. The Bus Buccaneer desktop
app wants one bridge model with two transports (Xenomorpher over USB, WPM over Wi-Fi), so the WPM's
API is to **mirror the Xenomorphicle bus-bridge command set one for one in meaning and reply fields**
(not byte formats), delivered as a series of pull requests to `studiohsoftware/2WIRELESS`.

Oren's rulings that shape everything:

- "Make sure they don't affect the current usability of the module forcing people to change their
  ways." → the **compatibility contract** (§3).
- "Normally, the user won't have the USB path, so don't bank on it. WiFi or gtfo." → over-the-air
  update of the WPM itself is the centrepiece of PR 0; USB is the recovery path, not the owner path.
- Every module must be supported, 251e included → byte-counting capture (PR 2) and the quiet-SPI boot
  deferral (PR 3).

## 2. Goals, non-goals, anti-goals

**Goals.** (a) A tree that builds from source with `arduino-cli` and reproduces the shipped binary's
behaviour. (b) OTA firmware update of the WPM over its own Wi-Fi, brownout-safe, recoverable over USB.
(c) `/version` as the single capability probe. (d) Bus scan, byte-counting capture, card-read
visibility, restore-with-status, mirroring Xenomorphicle. (e) Every PR independently mergeable and
useful, reviewed and bench-tested on Oren's WPM before it is opened.

**Non-goals.** Changing any existing page or route; authentication; raw frame injection beyond the
existing `/sendmidibytes`; broadcast enumerate 0x1B; writing module presets outside the mirrored
RESTORE path; Photon firmware (`Firmware/photon`, obsolete hardware).

**Anti-goals (failure even if it works).** A stock v1.18 owner noticing anything different; a change
to the FRAM settings layout or the `DONALD` init marker; a WPM that can be bricked by a power loss
during OTA; boot timing changes when no card image is staged; a 200-with-no-effect reply that the app
mistakes for success.

## 3. The compatibility contract (every PR)

1. Every existing route keeps its URL, parameter order, reply shape and default behaviour. Parsing is
   positional today; new parameters are **optional and trailing**.
2. The stock pages (`PresetManager.html`, `GetPresets.html`, `SetPresets.html`, `WriteFirmware.html`,
   the Setup page, the embedded home page) keep working unchanged.
3. FRAM layout (`Firmware/fram_map.txt`) is untouched. New firmware state lives only in the
   **reserved** range `0x02BE4D–0x02BE7F` (51 bytes) and is zero on stock units; the loader's pending
   record and OTA status live there.
4. Boot timing is unchanged unless a card image is staged (PR 3).
5. New behaviour arrives only through new routes. `/version` is the only way a client learns that a
   build is new; stock answers it with the HTML home page (HTTP 200), which clients treat as "stock".
6. Refusals on new routes answer **HTTP 200 with a JSON body** `{"ok":false,"error":"<code>"}`, the
   way stock routes answer, never 4xx/5xx.
7. A `v2/` path prefix keeps its stock meaning (pre-Primo framing) on every route, old and new.

## 4. Facts the design rests on (all verified on Oren's WPM, 2026-10-21)

Hardware and boot:
- Arduino MKR1000 (SAMD21G18A, WINC1500 Wi-Fi on the shared SERCOM SPI) on a carrier with an
  MB85RS2MTA 256 KB SPI FRAM (CS pin 4) and an I²C isolator to the 200e bus. Bus-powered from the
  case's +5 V into the MKR's 5 V pin; FRAM and isolator B side run from the MKR's 3.3 V.
- The unit has the **stock SAM-BA bootloader** (v2.0 "[Arduino:XYZ] Dec 20 2016", 8 KB at 0x0000,
  app region 248 KB from 0x2000, no lock regions, no security bit). The shipped application is
  byte-identical to `Firmware/arduino/2Wireless/bin/2Wireless.ino.bin` (108,612 bytes, linked with the
  core's `flash_with_bootloader.ld`, vectors at 0x2000).
- In USB Device mode with a data cable the WPM enumerates as "Studio H Software USB Midi"
  (VID 0x1209, PID 0xFBA0, bcdDevice 0x0100) with a CDC port. The Arduino 1200-baud touch works and
  **erases the application's first flash row by design** (core `Reset.cpp`), so a touch without an
  upload leaves the WPM in the bootloader until `bossac` writes something. Upload:
  `arduino-cli upload -b arduino:samd:mkr1000 -p /dev/cu.usbmodemNNN -i <app>.bin` (bossac
  `-U true -i -e -w -v -R`, offset 0x2000). In USB Host mode nothing enumerates.
- The supplied USB adapter blocks VBUS. With a plain cable the Mac back-feeds the MKR1000, so a case
  power cycle does not reset the WPM; partial resets of this kind leave the WINC beaconing with the
  app halted (pingable, port 80 closed).
- Power-on to HTTP: with USB unplugged, a 60 s quiet wait and one association, `/currentpreset`
  answers 7–11 s after the join. Associating within seconds of power-on, or association churn, has
  left port 80 closed until the next power cycle; `loop()` calls `server.begin()` on every
  WL_AP_CONNECTED transition and never stops the server (WiFi101 socket leak, WINC has 7 TCP sockets).
  The WINC1500 also drops the listening socket on station re-association while WiFi101 still flags it
  listening, so the AP listener must be re-created on every WL_AP_CONNECTED transition and refreshed
  after 10 s idle with a station connected (bench B2).

Firmware (v1.18 source):
- `setup()` order: pins, `SPI.begin`, `Wire.begin(0x50, generalCall)` + slave callbacks, WiFi101 init
  via `WiFi.status()`, `WiFi.config(192.168.0.1)`, `initializeFram()`, SSID/password from FRAM,
  `beginAP`, USB mode, optional poll-on-start. The I²C slave is live within ~20 ms of a cold boot (POR
  skips the bootloader's 500 ms double-tap wait).
- `requestEvent` is called **per requested byte** and serves `FRAM[pointer + n]` for 1-, 2- or 3-byte
  pointers (18-bit mask in `framRead`), bypassing the Wire tx buffer with `sercom2.sendDataSlaveWIRE`.
  This depends on a **modified Wire.cpp that is not in the repo** (stock SAMD Wire calls `onRequest`
  once per transaction and serializes from `txBuffer`).
- `receiveEvent` ignores frames of ≤ 4 bytes unless `write_i2c_to_fram` is set (then every byte is
  cached to FRAM at `write_counter++`); 5+-byte frames are decoded as commands (MIDI clock to USB,
  DISPLAY_EVENT 0x13 → `setDisplayMessage` at 0x02C0FE: `[addr][7 version chars]`).
- `/getpresets?addr=XX` sets `write_i2c_to_fram`, sends BACKUP, **then** zeroes `write_counter`
  (ordering bug), ends on 1 s with no `Wire.available()` seen by the main loop (races the ISR), never
  clears FRAM, replies chunked `{"module_address":"0x..","data":"<hex>"}`. A silent module → `"data":""`
  after 1.06 s.
- `/writememory` (POST) streams the JSON body token by token (quoted strings only, 256-char token cap,
  500 ms idle ends the body), writes each `"data"` at the last `"addr"` (strtol base 16), and sends
  `HTTP/1.1 200` **before parsing**. No body size limit (21 KB verified). GET form
  `/writememory?addr=&data=` writes one run.
- `/readmemory?addr=&length=` has no length cap (16 KB verified, ~6 KB/s), 128-byte elements,
  lower-case unpadded hex addresses; a missing second parameter means length 1; `addr` must be first.
- `/displaymessage` builds its JSON from the C buffer at 0x02C0FE and returns malformed JSON when the
  cache is zero. The cache survives reboots (FRAM) and no module rewrites it at boot.
- Master-side helpers exist for everything PR 1–4 need: `switchToMaster/Slave`, `masterBeginTransmission`,
  `sendQuery(addr)` (0x1A), `sendBackupPresets`, `sendRestorePresets`, `sendPollingComplete`,
  `sendRemoteEnable/Disable`; `i2c_guard` blocks the slave ISR while mastering.
- Flash budget: app 108,612 bytes of 248 KB. FRAM scratch `0x000000–0x02BD18` (179,481 bytes) holds a
  staged card image or an OTA image; both fit (largest card image 151,984 bytes; app ≤ ~120 KB).

## 5. Decisions (how it gets made)

Decided by Oren: PRs to the 2WIRELESS repo, stock default behaviour, mirror Xenomorphicle, OTA first.
Decided with the app session: route shapes in §6. The rest are the builder-side calls below, marked
**(call)**.

- **(call) Language and layout.** Stay in the Arduino sketch (C++17 as the SAMD core compiles it).
  Split the sketch into `2Wireless.ino` (setup/loop, unchanged shape) plus sketch-folder sources
  `src/wpm_*.cpp/.h` by concern (fram, http, bus_master, capture, card, ota, version). No RTOS, no
  heap in ISRs, `String` only where stock already uses it.
- **(call) Wire modification vendored.** A copy of the core's `Wire.cpp/.h` (SAMD core 1.8.14) lives in
  the sketch as `src/WireByte.{cpp,h}` with the class renamed `TwoWireByte` and the global `Wire`
  defined there; the only functional change is that `onService()` calls the request callback on every
  data-requested interrupt instead of serving `txBuffer`. The sketch includes `"src/WireByte.h"` and
  never `<Wire.h>`; neither WiFi101, MIDIUSB nor the USB Host library include Wire, so the core's
  `Wire.cpp` (and its `SERCOM2_Handler`) is not compiled in and nothing collides. The diff against the
  stock file is checked in as `src/WireByte.diff` with a comment block explaining why. Whether the
  per-byte callback is the **only** difference in Studio H's build cannot be proven from the binary
  (different compiler, no symbols); it is verified behaviourally on the bench: card serving to a module
  (a 2- and a 3-byte-pointer read), preset backup and restore, MIDI, with the shipped app
  (`bin/2Wireless.ino.bin`, byte-identical to Oren's unit) as the reference. That bench pass is the
  PR 0 definition of "reproduces the shipped behaviour".
- **(call) Build.** `arduino-cli compile --fqbn arduino:samd:mkr1000` with a committed
  `Firmware/arduino/2Wireless/sketch.yaml` pinning core 1.8.14 and libraries WiFi101, MIDIUSB,
  "USB Host Library SAMD". The USB identity ("Studio H Software USB Midi", 0x1209:0xFBA0) is
  reproduced via a `boards.local.txt` overlay committed under `Firmware/arduino/` and documented;
  until Studio H confirms their exact flags, builds carry the stock MKR1000 VID/PID and the README says
  so. A `Makefile`/script `Firmware/arduino/build.sh` produces `bin/2Wireless.ino.bin`, prints its
  CRC32, and `flash.sh` uploads with the 1200-baud touch.
- **(call) Loader placement.** Keep SAM-BA. The FRAM loader is a separate tiny project
  `Firmware/arduino/FramLoader/` producing a ≤ 16 KB binary embedded into the sketch as the
  `.sketch_boot` section (the core's `flash_with_bootloader.ld` has `KEEP(*(.sketch_boot))`; the SDU
  library is the precedent). Layout: SAM-BA 0x0000, loader 0x2000–0x5FFF, app 0x6000+. The loader
  never erases itself, so a brownout at any point re-runs the copy on the next boot; USB (bossac)
  remains the recovery path for everything. **OTA images exclude the loader**: the sketch build
  produces one bin (loader section at 0x2000–0x5FFF + app from 0x6000); `build.sh` also emits
  `2Wireless.ota.bin` = the same bin with its first 0x4000 bytes stripped, and that is what OTA
  transfers and what the loader writes at **0x6000**. The loader is updated only over USB (bossac
  writes the whole bin). `app_crc32` covers flash `0x6000 .. 0x6000+len`.
- **(call) Pending-OTA record** at FRAM `0x02BE50`: `magic "OTA1"(4) len(4 LE) crc32(4 LE) flags(1)
  last_result(1) app_crc32(4)`; the loader verifies CRC32 (zlib polynomial, as `Buchla200eCrc32`) over
  FRAM `[0..len)`, erases/writes flash from **0x6000** in 256-byte rows (row 0, vectors and marker,
  erased first and written last; a failed flash verify erases row 0 again before the retry, so an
  unverified image is never jumped into and retry exhaustion ends in the hand-off below), verifies the
  flash CRC, clears the magic, sets `last_result`, jumps. Non-matching CRC → clears the magic, sets
  `last_result = crc_mismatch` (2), boots the existing app. Before any erase the loader also checks
  `len` and the staged head (below); a failure clears the magic, sets `last_result = image` (3) and
  boots the existing app. No plausible app in flash (head fails the check below) → the loader blinks the LED at 5 Hz for 3 s, then writes the SAM-BA double-tap
  magic (0x07738135 at 0x20007FFC) and resets, so the stock bootloader stays resident with its USB
  port and one bossac upload recovers the unit without reaching the reset button. **Maximum `len` = 0x2BD18** (179,480 bytes):
  the FRAM scratch area, so a staged image can never reach the SSID/password, settings or the record
  itself (it is also below the flash app region's 0x3A000). The image head check
  (the first 44 bytes of an app image) is: word 0 in 0x20000000..0x20008000, word 1 odd and inside
  0x6000..0x40000, and a 16-byte app marker at offset 0x1C (vector entries 7..10, reserved on ARMv6-M):
  `"WPMAPP1\0"`, link base LE32 = 0x6000, image length LE32. `build.sh` stamps it (full `.ino.bin`
  offset 0x401C; the `.ota.bin` is the stamped bin minus 0x4000). `/ota/commit` and the loader require
  marker length == `len`; the loader's flash check at boot has no record and passes `len` 0, which
  skips the length comparison. This makes a stock or full image impossible to commit.
- **(call) Loader boot cost.** On every boot the loader initialises SERCOM1 SPI, reads the 18-byte
  record and, if no magic, de-initialises SPI (`SPI.end()`; the pin mux on 8/9/10 is left as the core leaves it and the app's `SPI.begin()` re-muxes them) and jumps: well
  under 1 ms, so the ~20 ms I²C-slave availability and the "boot timing unchanged" contract hold. The
  CRC pass runs only when a record is pending. The WINC1500 is not initialised at that point, so the
  loader owning the shared SPI briefly is safe; it must not touch the WINC chip-select/reset pins.
- **(call) OTA staging vs a staged card image.** Both live at FRAM 0. An OTA transfer through the
  unchanged `/writememory` route destroys a staged card image, exactly as a card write would destroy
  a staged OTA image; stock behaviour is unchanged and the client (the app) sequences the two. PR 0
  adds no guard; PR 3's `"error":"staged"` guard covers only `/getpresets` and `/dump`.
- **(call) Capture (PR 2) keys on `write_counter`**, not `Wire.available()`: a byte arrived iff the
  counter changed. First-byte wait and expected byte count come from the query. The card-side receive
  path applies each 2-byte offset frame into FRAM instead of recording it, giving a flat bank image;
  the completion frame `04 22 <addr> 0A 20` ends the job early.
- **(call) Jobs run in `loop()`**, one at a time, with a small state machine mirroring
  `Bus200eBridgeState`/`Bus200eMasterState`; routes start a job and return immediately; `/…/status`
  routes report it. `/ota/commit` and bus jobs are mutually exclusive.
- **(call) Quiet-SPI deferral (PR 3)**: in `setup()`, after `Wire.begin` and before `WiFi.status()`,
  read FRAM 0..7; if `#!200EFU` or `#$200EFU`, `delay(3000)` with no SPI user other than the slave ISR.
- **(call) Testing.** Desktop unit tests (plain C++ with a tiny harness under `Firmware/arduino/test/`)
  for CRC32, the pending record, capture counting and JSON formatting; bench tests on Oren's WPM over
  Wi-Fi with curl pinned to `en1`, recorded as reproducible scripts under `Firmware/arduino/bench/`.
  Each PR's README section lists the bench steps that were run.

## 6. The pull requests

Each PR is a branch off `master`, independently mergeable, with its own README section
(`Firmware/arduino/README.md`) and a line in `/version`'s capability list.

### PR 0 — buildable tree, `/version`, server fix, OTA

- Tree builds reproducibly (§5). README: how to build, how to flash over USB (Device mode, data cable,
  no double-tap, the 1200-touch erase warning, the SAM-BA version string "SAM-BA v2.0 Dec 20 2016"),
  how OTA works, and the owner truth: stock v1.18 units need one USB upload to get OTA.
- The AP listener is re-created on every WL_AP_CONNECTED transition and refreshed after 10 s idle with a
  station connected (the WINC1500 drops the listening socket on station re-association while WiFi101
  still flags it listening; bench B2); on client disconnect only `client.stop()`.
- `/getpresets`: `write_counter = fram_address = 0` **before** `sendBackupPresets` (behaviour otherwise
  unchanged).
- `/version` → `{"version":"1.19","board":"mkr1000","loader":"fram-loader-1","getpresets_wait":false,
  ...}` — **every PR bumps `version`** (1.19, 1.20, …) and appends to `capabilities`; clients key on
  `capabilities`, never on the number. Full shape: `{"version":"1.19","board":"mkr1000","loader":"fram-loader-1","getpresets_wait":false,
  "capabilities":["version","ota"],"usbMode":1,"pollOnStart":0,"reset_cause":"POR|BOD|EXT|SYST|WDT",
  "uptime_s":N,"rssi":-NN,"tcp_sockets_free":null,"app_crc32":"HEX"}`. `loader` absent when the loader
  section is not present (dev builds). `tcp_sockets_free` is kept for clients but PR 0 cannot read the
  WINC's socket table, so it is always `null`; a later PR may fill it.
- OTA: transfer via existing `/writememory` into scratch from 0; `/ota/commit?len=N&crc32=HEX` →
  checks `busy` first (job running, `"error":"busy"`; never returned by PR 0, which has no background job), then in order `len` (0, odd, > 0x2BD18 (see §5) → `"error":"len"`), image head (word 0 in
  0x20000000..0x20008000, word 1 odd and inside 0x6000..0x40000, app marker at 0x1C with base 0x6000
  and length == `len` → `"error":"image"`), CRC over FRAM
  (`"error":"crc"`); on success writes the record, replies
  `{"ok":true,"pending":{"len":N,"crc32":"HEX"}}`, then `NVIC_SystemReset()` after the reply is flushed.
  `/ota/status` → `{"loader":"fram-loader-1","pending":false,"last":"ok"|"crc_mismatch"|"image"|"none",
  "app_crc32":"HEX"}` (`last` persists until the next commit or abort). `/ota/abort` → `{"ok":true}`.

### PR 1 — bus scan and status (SCAN, BUS_STATUS, POLL_DONE)

- `/bus/scan?first=08&last=77` → `{"ok":true}` or `"busy"`; sweep in `loop()`, one QUERY 0x1A per
  address, 300 ms reply window, 150 ms gap; found list with the module's 0x13 version frame if seen.
  `/bus/scan/status` → `{"state":"idle|scanning|done","idx":N,"count":N,"found":[{"addr":"28",
  "ver_valid":true,"version":"31.0   "}]}`.
- `/bus/status` → `{"wpm_present":true,"bus_quiet":b,"dialect":0|1|2,"last_error":0..5}`
  (`wpm_present` is always true: the WPM is the card).
- `/bus/poll_done` → sends `04 00 22 14 FF` (or `0x14` in v2 framing) when the bus is quiet; reply
  after it went out, `"error":"send_timeout"` after 2 s, `"busy"` during a job.

### PR 2 — byte-counting capture (GET_DUMP)

- `/dump?addr=XX&len=N&first_wait_ms=M` (defaults: `len` unknown → silence mode as stock,
  `first_wait_ms` 5000) → starts the job; `/dump/status` → `{"state":...,"received":N,"expected":N,
  "complete_frame":b,"error":...}`; data is read with `/readmemory` as today. Offsets applied, flat
  bank from 0. `/getpresets` itself is unchanged.

### PR 3 — card-read visibility (FW_STATUS) and quiet boot

- `/card/status` → `{"staged":b,"kind":0|1|2,"used":N,"served":N,"age_s":N|null,"ver_addr":"5E",
  "ver_valid":b,"version":"31.0   ","ver_gen":N}`; `served` counts bytes handed out by `requestEvent`
  since boot; `ver_gen` increments on every 0x13 frame.
- Boot deferral when a card magic is staged (§5). `/getpresets` and `/dump` refuse (`"error":"staged"`)
  while a card magic is at 0 unless `force=1`.

### PR 4 — restore with status (PUT_DUMP/STATUS)

- `/restore?addr=XX&len=N` (image already in FRAM at 0) → job; `/dump/status` reports it with
  `is_restore:true`, states and error codes mirroring `Bus200eMasterState`/`Bus200eMasterError`
  (idle, finding card, sending, wait activity, transferring, done, failed; none, busy, bad args, no
  free card, send timeout, no response).

### After PR 4

- `/mem/crc32?addr=HEX&len=N` (on-device CRC32 of an FRAM range).
- `/bus/log` (ring buffer of the last N decoded frames with millisecond timestamps).

## 6a. Field meanings mirrored from Xenomorphicle (reference, readable on this Mac)

Source documents: `~/Documents/GitHub/Xenomorphicle/docs/bus200e-sysex-fw.md` (FW_STATUS_R, SCAN_R,
BUS_STATUS_R, STATUS_R, NAK codes) and `~/Documents/GitHub/Buchla_FW/docs/superpowers/specs/
2026-10-20-200e-updater-app-design.md` §7, §7a, §7b. Summary of the fields used above:

| field | meaning |
|---|---|
| `kind` | card image on FRAM: 0 none, 1 v1 (`#!200EFU`, 64 KB, 2-byte pointer), 2 v2 (`#$200EFU`, 3-byte pointer) |
| `used` | image length from its TOC (v1: big-endian u16 at 0x1D; v2 per `card_v2_format.md`) |
| `served` | bytes handed to a reading module since boot (count of `requestEvent` calls) |
| `age_s` | seconds since the module's first read since boot; `null` = no read yet |
| `ver_addr`, `ver_valid`, `version` | module address and 7 ASCII bytes of the last 0x13 frame; valid only if one was seen |
| `ver_gen` | count of 0x13 frames seen since boot (identical re-announcements included) |
| `dialect` | framing last seen: 0 none, 1 long/PRIMO (v1), 2 short/V2 |
| `bus_quiet` | the bus has been idle long enough to master a frame (`checkBus` condition) |
| `last_error` | 0 none, 1 busy, 2 bad args, 3 no free card, 4 send timeout, 5 no response |
| `getpresets_wait` | true only if `/getpresets` accepts a trailing `wait` parameter (it does not in this track; PR 2's `/dump` replaces the idea; kept so the app's earlier probe stays valid) |
| job `state` | idle, finding card, sending, wait activity, transferring, done, failed |

## 7. What is left to the builder

- Exact JSON key spellings beyond those frozen with the app session (§6 PR 0 are frozen; PR 1–4 keys
  are proposals the app will adapt to; confirm with the app session before freezing each).
- The loader's flash-write routine (NVMCTRL row erase + page writes, running from RAM or from the
  loader's own flash region; must not touch 0x2000–0x5FFF).
- The `boards.local.txt` USB identity once Studio H confirms their build flags. **(call)** Bench builds
  flashed onto Oren's unit reproduce the shipped identity ("Studio H Software USB Midi", 0x1209:0xFBA0)
  so his DAW's device mapping does not change; the PR documents the flags and leaves the choice to
  Studio H.
- Bench scripts and the README prose.

## 8. Risks and open questions

- **Bench policy (Oren, 2026-10-21): "test the OTA loader hard and make it robust. If it's hung, you
  can reflash the stock binary and keep trying and iterating and learning."** So PR 0's acceptance
  includes, on Oren's unit: the first loader flash over USB; a full OTA round trip; a deliberate case
  power cut mid-copy (and mid-CRC, and between record-write and reset) with recovery on the next boot;
  a corrupted staged image (CRC mismatch → old app keeps running, `last:"crc_mismatch"`); an
  oversize `len`; an OTA of a deliberately broken app (bad vectors refused at commit); and a staged stock/full image
  (refused at commit and by the loader). A hung unit
  is restored with `arduino-cli upload … -i bin/2Wireless.ino.bin` (byte-identical to the shipped app)
  and the attempt is written up before the next iteration. The USB touch is issued only immediately
  before an upload.
- `requestEvent` does a full SPI transaction per byte inside the I²C ISR; PR 3's `served` counter must
  not add latency there.
- Studio H has not been contacted; the PRs may sit. The compatibility contract exists so that merging
  them is a no-risk decision for them.
