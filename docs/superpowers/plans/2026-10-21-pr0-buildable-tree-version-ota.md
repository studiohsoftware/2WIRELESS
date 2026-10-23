# PR 0 — Buildable tree, `/version`, server fix, FRAM loader + OTA — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the 2WIRELESS firmware build reproducibly from this repo, add the `/version` capability
probe and two agreed fixes, and give the WPM a brownout-safe over-the-air firmware update driven by a
first-stage FRAM loader, verified hard on Oren's unit.

**Architecture:** The sketch stays one Arduino project (`Firmware/arduino/2Wireless`) with new
concerns split into `src/` files; shared pure code (CRC32, OTA record, image checks) lives in a local
library `Firmware/arduino/libraries/WpmCommon` used by both the app and a second tiny sketch,
`Firmware/arduino/FramLoader`, whose binary is embedded in the app's `.sketch_boot` section at flash
0x2000 (the core's linker script places it there and the app vectors at 0x6000). OTA = client writes
the app-only image into FRAM scratch with the stock `/writememory` route, `/ota/commit` validates and
writes a pending record in reserved FRAM, resets; the loader copies FRAM → flash 0x6000, verifies,
clears the record, resets again.

**Tech Stack:** arduino-cli 1.5.1, core `arduino:samd@1.8.14` (installed), libraries WiFi101,
MIDIUSB, gdsports USB_Host_Library_SAMD (git), bossac 1.7.0-arduino3 (installed with the core),
clang (desktop tests), python3 + curl (bench scripts, curl pinned with `--interface en1`).

**Spec:** `docs/superpowers/specs/2026-10-21-wpm-firmware-prs-design.md` (§3 contract, §4 facts, §5
calls, §6 PR 0, §8 bench policy).

## Global Constraints

- Compatibility contract (spec §3): every existing route/page/default unchanged; FRAM layout
  unchanged; new state only in reserved FRAM `0x02BE4D–0x02BE7F`; new behaviour only via new routes;
  refusals are HTTP 200 + `{"ok":false,"error":"<code>"}`.
- Flash layout: SAM-BA 0x0000–0x1FFF (never written), loader 0x2000–0x5FFF (written only by USB),
  app 0x6000+. Loader blob is exactly 0x4000 bytes (padded with 0xFF).
- OTA image = app-only bin (the full sketch bin minus its first 0x4000 bytes), staged at FRAM 0,
  `len` ≤ 0x2BD18 (179,480), even, > 0; sanity: word0 ∈ [0x20000000, 0x20008000], word1 odd and
  ∈ [0x6000, 0x40000). CRC32 = zlib polynomial 0xEDB88320, init/final XOR 0xFFFFFFFF.
- Pending record at FRAM `0x02BE50`, 18 bytes: `magic[4]="OTA1" len[4 LE] crc32[4 LE] flags[1]
  last_result[1] app_crc32[4 LE]`; `last_result`: 0 none, 1 ok, 2 crc_mismatch. Magic is written
  last and cleared (4×0x00) first.
- `/version` JSON keys exactly: `version, board, loader, capabilities, getpresets_wait, usbMode,
  pollOnStart, reset_cause, uptime_s, rssi, tcp_sockets_free, app_crc32`. `version` = `"1.19"`.
  Version string in the Setup page becomes `Firmware v1.19` through one `FW_VERSION` define.
- Bench rule: the USB 1200-baud touch is issued only immediately before an upload (it erases the
  app's first flash row). Recovery: `arduino-cli upload -b arduino:samd:mkr1000 -p <port>
  -i Firmware/arduino/2Wireless/bin/2Wireless.ino.bin` (the shipped v1.18, keep it untouched).
- Bench Wi-Fi protocol: after any WPM reset, wait 60 s, then one `networksetup -setairportnetwork
  en1 BUCHLA200E BUCHLA200E`; every curl uses `--interface en1`. USB cable unplugged for case power
  cycles (back-feed), plugged in only for uploads.
- Commits: one per task, Conventional-Commit style; never commit `bin/` outputs except the final
  release binaries in Task 12; never commit the stock `bin/2Wireless.ino.bin` as modified.

## Review Focus

1. A client POSTs an OTA image larger than the scratch area through the stock `/writememory` (the
   route cannot refuse): the SSID/password at 0x02BD19 get overwritten. Expected: `/ota/commit`
   refuses `len > 0x2BD18` (Task 4 test), and README tells clients to never stage beyond it.
2. Power cut between the record's `len/crc` write and the `magic` write: expected no pending OTA on
   next boot (magic written last; Task 4 serialization test + Task 10 bench).
3. Power cut after the copy but before the magic clear: expected the loader re-copies (idempotent)
   and the unit still boots (Task 10 bench).
4. A staged image with a valid CRC but vectors pointing outside the app region: expected refusal
   at commit (`"error":"image"`, Task 4/7) and, if forced in by hand, the loader refuses to jump and
   blinks (Task 5 bench).
5. The WPM resets by software (OTA) while the WINC1500 stays powered and keeps the old AP: expected
   the app re-initialises Wi-Fi and HTTP comes back without a case power cycle (Task 8 retry logic +
   Task 9 bench).

---

### Task 1: Reproducible build of the stock sketch

**Files:**
- Create: `Firmware/arduino/build.sh`, `Firmware/arduino/flash.sh`, `Firmware/arduino/boards.local.txt`,
  `Firmware/arduino/README.md` (skeleton), `Firmware/arduino/.gitignore` (`build/`)
- Modify: nothing in the sketch yet

**Interfaces:**
- Produces: `build.sh [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000]` → writes
  `Firmware/arduino/build/<sketch>/<sketch>.ino.bin` and prints size + CRC32 (python zlib).
  `flash.sh <bin> [port]` → finds the WPM port (prefers the Studio H CDC port, else the MKR1000
  bootloader port), touches 1200 baud only if the port is the app's, runs `bossac -i -d --port=… -U
  true -i -e -w -v <bin> -R`, and tees the full bossac debug output to `Firmware/arduino/bench/
  bossac-<timestamp>.log`.
- `boards.local.txt` entries (copied by `build.sh` into the installed core's directory):
  `wpm` = clone of `mkr1000` with `build.vid=0x1209 build.pid=0xFBA0 build.usb_manufacturer="Studio H
  Software" build.usb_product="Studio H Software USB Midi"`; `wpm_loader` = clone of `mkr1000` with
  `build.usb_flags=` (empty: no USB stack in the loader).

- [ ] **Step 1: Write `build.sh`** — installs/pins deps idempotently: `arduino-cli core install
  arduino:samd@1.8.14`; `arduino-cli lib install WiFi101@0.16.1 MIDIUSB@1.0.5`;
  `arduino-cli config set library.enable_unsafe_install true` then `arduino-cli lib install --git-url
  https://github.com/gdsports/USB_Host_Library_SAMD.git` (pin the commit hash you resolve today in the
  script as a comment and `git -C … checkout <hash>`); copies `boards.local.txt` next to the core's
  `boards.txt`; compiles with `arduino-cli compile --fqbn arduino:samd:<profile> --libraries
  Firmware/arduino/libraries --output-dir build/<sketch> --warnings default Firmware/arduino/<sketch>`;
  prints `size <bytes> crc32 <hex>` of the `.bin`.
- [ ] **Step 2: Run `build.sh 2Wireless --profile mkr1000`** on the untouched sketch.
  Expected: compiles; bin size within ±3% of 108,612 bytes (compiler drift is expected; exact match
  is not required).
- [ ] **Step 3: Run `build.sh 2Wireless --profile wpm`** and check the USB strings:
  `strings build/2Wireless/2Wireless.ino.bin | grep -c "Studio H Software USB Midi"` → `1`.
- [ ] **Step 4: Write `flash.sh`** as specified in Interfaces. Include a `--dry-run` that prints the
  bossac command and does not touch the port.
- [ ] **Step 5: Bench baseline — flash the `wpm`-profile build of the untouched sketch** to Oren's WPM
  with `flash.sh` (first real use of the touch+upload path). Then the Wi-Fi protocol; verify:
  `curl --interface en1 http://192.168.0.1/ssid` → `{"ssid": "BUCHLA200E"}`; the WPM enumerates as
  "Studio H Software USB Midi"; `/getpresets?addr=76` body is `"data": ""` after ~1 s. Record the
  bossac log path in `bench/RESULTS.md` (H19 reference for the app session).
- [ ] **Step 6: Commit** `build.sh flash.sh boards.local.txt README.md .gitignore bench/RESULTS.md`:
  `build: reproducible arduino-cli build and USB flash scripts for 2Wireless`

### Task 2: Vendored per-byte Wire (`WireByte`)

**Files:**
- Create: `Firmware/arduino/2Wireless/src/WireByte.h`, `src/WireByte.cpp`, `src/WireByte.diff`
- Modify: `Firmware/arduino/2Wireless/2Wireless.ino:13` (`#include <Wire.h>` → `#include "src/WireByte.h"`)

**Interfaces:**
- Produces: class `TwoWireByte` (API-identical to the core's `TwoWire`), global `TwoWireByte Wire`,
  `SERCOM2_Handler` defined in `WireByte.cpp`. Behavioural change, and the only one: in
  `TwoWireByte::onService()`, on every `isDataRequestedWIRE()` interrupt call `onRequestCallback()`
  (if set) and do **not** read from `txBuffer`; the callback is responsible for
  `sercom->sendDataSlaveWIRE()` (the sketch's `requestEvent` already does this).

- [ ] **Step 1: Copy** `~/Library/Arduino15/packages/arduino/hardware/samd/1.8.14/libraries/Wire/Wire.{h,cpp}`
  to `src/WireByte.{h,cpp}`; rename class to `TwoWireByte`, keep `extern TwoWireByte Wire;` and the
  `Wire` definition + `SERCOM2_Handler` for `PERIPH_WIRE`/`WIRE_IT_HANDLER` exactly as the core's
  variant defines them.
- [ ] **Step 2: Apply the per-byte change** in `onService()` and write `src/WireByte.diff` =
  `diff -u <core Wire.cpp> src/WireByte.cpp` with a header comment explaining why (spec §4).
- [ ] **Step 3: Build** `build.sh 2Wireless --profile wpm`. Expected: compiles; no duplicate-symbol
  link errors (proves the core's Wire is not linked). `nm` is not available on the bin; instead grep
  the verbose compile log for `libraries/Wire/Wire.cpp` → must be absent.
- [ ] **Step 4: Bench — preset round trip through the slave path.** Ask Oren for one module address
  on his bus (e.g. the 272e); run `/getpresets?addr=XX` (captures), read the capture with
  `/readmemory`, then `/setpresets?addr=XX` (the module reads presets back from the WPM through
  `requestEvent`), then `/getpresets?addr=XX` again and compare captures byte-for-byte. Expected:
  identical. Repeat the same sequence with the shipped v1.18 (`flash.sh bin/2Wireless.ino.bin`) once
  to prove the reference behaves the same; record both in `bench/RESULTS.md`.
- [ ] **Step 5: Commit** `feat(firmware): vendor per-byte Wire slave (WireByte) so the tree builds the shipped card-serving behaviour`

### Task 3: `WpmCommon` — CRC32 with desktop tests

**Files:**
- Create: `Firmware/arduino/libraries/WpmCommon/library.properties`,
  `libraries/WpmCommon/src/wpm_crc32.h`, `src/wpm_crc32.cpp`,
  `Firmware/arduino/test/Makefile`, `test/test_crc32.cpp`, `test/minitest.h`

**Interfaces:**
- Produces: `uint32_t wpm_crc32_update(uint32_t crc, const uint8_t* data, size_t len)` (bitwise, no
  table; `crc` starts at `0xFFFFFFFF`), `uint32_t wpm_crc32(const uint8_t* data, size_t len)` (= update
  then final XOR). Plain C++ with no Arduino includes (compiles on the desktop).
- `test/minitest.h`: `CHECK(expr)` macro counting failures; `main` returns the count.
  `make -C Firmware/arduino/test` builds and runs every `test_*.cpp` with `clang++ -std=c++17
  -I../libraries/WpmCommon/src`.

- [ ] **Step 1: Write `test/test_crc32.cpp`**:
  `CHECK(wpm_crc32((const uint8_t*)"123456789", 9) == 0xCBF43926)`;
  `CHECK(wpm_crc32(nullptr, 0) == 0x00000000)`;
  incremental: `u = wpm_crc32_update(0xFFFFFFFF, "1234", 4); u = wpm_crc32_update(u, "56789", 5);
  CHECK((u ^ 0xFFFFFFFF) == 0xCBF43926)`.
- [ ] **Step 2: Run `make -C Firmware/arduino/test`** → FAIL (undefined `wpm_crc32`).
- [ ] **Step 3: Implement** `wpm_crc32.{h,cpp}` and `library.properties` (`name=WpmCommon`,
  `architectures=*`).
- [ ] **Step 4: Run the tests** → `0 failures`.
- [ ] **Step 5: Commit** `feat(common): CRC32 (zlib polynomial) shared by loader and app, with desktop tests`

### Task 4: `WpmCommon` — OTA record and image checks with desktop tests

**Files:**
- Create: `libraries/WpmCommon/src/wpm_ota_record.h`, `src/wpm_ota_record.cpp`, `test/test_ota_record.cpp`

**Interfaces:**
- Produces (all pure, desktop-testable):
  - constants `WPM_OTA_RECORD_ADDR = 0x02BE50`, `WPM_OTA_RECORD_LEN = 18`, `WPM_OTA_MAX_LEN =
    0x2BD18`, `WPM_APP_FLASH_BASE = 0x6000`, `WPM_FLASH_END = 0x40000`, `WPM_LOADER_BLOB_LEN = 0x4000`;
  - `struct WpmOtaRecord { uint32_t len; uint32_t crc32; uint8_t flags; uint8_t last_result;
    uint32_t app_crc32; bool pending; }`;
  - `bool wpm_ota_record_parse(const uint8_t raw[18], WpmOtaRecord* out)` (pending = magic matches;
    other fields parsed regardless);
  - `void wpm_ota_record_serialize(const WpmOtaRecord* in, uint8_t raw[18])` (magic present iff
    `pending`);
  - `enum WpmOtaCheck { WPM_OTA_OK, WPM_OTA_ERR_LEN, WPM_OTA_ERR_IMAGE }`;
  - `WpmOtaCheck wpm_ota_check_len(uint32_t len)`;
  - `WpmOtaCheck wpm_ota_check_vectors(const uint8_t first8[8])` (word0/word1 rules from Global
    Constraints);
  - `const char* wpm_ota_last_result_str(uint8_t v)` → `"none"|"ok"|"crc_mismatch"`.

- [ ] **Step 1: Write `test/test_ota_record.cpp`** covering: round trip serialize→parse with
  `pending=true` (magic bytes `4F 54 41 31` at raw[0..3], `len` little-endian at raw[4..7]); `pending=false`
  serializes magic as `00 00 00 00` and parses as not pending with fields intact; all-0xFF and all-0x00
  raw → not pending, `last_result` reported through `wpm_ota_last_result_str` as `"none"` for 0 and
  for any value > 2; `wpm_ota_check_len`: 0 → ERR_LEN, 1 → ERR_LEN, 0x2BD18 → OK, 0x2BD1A → ERR_LEN,
  0x3A000 → ERR_LEN; `wpm_ota_check_vectors`: `00 80 00 20 05 CB 00 00` → ERR_IMAGE (reset 0xCB05 <
  0x6000), `00 80 00 20 05 6B 00 00` → OK (0x6B05), `00 80 00 20 04 6B 00 00` → ERR_IMAGE (even),
  `00 80 00 21 05 6B 00 00` → ERR_IMAGE (SP above RAM), `FF FF FF FF FF FF FF FF` → ERR_IMAGE.
- [ ] **Step 2: Run tests** → FAIL (undefined symbols).
- [ ] **Step 3: Implement** `wpm_ota_record.{h,cpp}`.
- [ ] **Step 4: Run tests** → `0 failures`.
- [ ] **Step 5: Commit** `feat(common): OTA pending record and image checks with desktop tests`

### Task 5: `FramLoader` sketch and its embedding in the app

**Files:**
- Create: `Firmware/arduino/FramLoader/FramLoader.ino`, `FramLoader/fram.h`, `FramLoader/nvm.h`,
  `Firmware/arduino/tools/blob2c.py`, `Firmware/arduino/2Wireless/src/wpm_loader_blob.cpp` (generated),
  `Firmware/arduino/2Wireless/src/wpm_loader_blob.h`
- Modify: `build.sh` (target `FramLoader` builds with `--profile wpm_loader`, asserts bin ≤ 0x4000,
  runs `blob2c.py` to regenerate `wpm_loader_blob.cpp`, then prints the loader CRC)

**Interfaces:**
- Loader behaviour (no USB, no Wi-Fi, no Wire): `setup()` → SPI FRAM read of 18 bytes at
  `WPM_OTA_RECORD_ADDR` → if not pending: `SPI.end()`, jump to `0x6000` if
  `wpm_ota_check_vectors(flash[0x6000..])` is OK, else blink `LED_BUILTIN` at 5 Hz forever. If
  pending: CRC32 over FRAM `[0..len)` in 256-byte chunks; mismatch → clear magic, set
  `last_result=2`, reset. Match → for each 256-byte row `r` in `[0x6000, 0x6000+len)`: NVM row erase
  at `r`, then 4 page writes of 64 bytes from FRAM (PBC, 16 word stores, WP, wait READY); then CRC32
  over flash `[0x6000, 0x6000+len)`; match → write record with `pending=false, last_result=1,
  app_crc32=crc`, then `NVIC_SystemReset()`; mismatch → leave the magic (so the copy re-runs on the
  next boot), reset. `NVMCTRL->CTRLB.bit.MANW = 1` before any NVM command.
- `fram.h`: `void fram_begin()`, `void fram_read(uint32_t addr, uint8_t* dst, size_t n)` (opcode
  0x03, 3 address bytes, CS pin 4, 12 MHz MODE0), `void fram_write(uint32_t addr, const uint8_t* src,
  size_t n)` (WREN 0x06 then WRITE 0x02), `void fram_end()`.
- `nvm.h`: `void nvm_erase_row(uint32_t addr)`, `void nvm_write_page(uint32_t addr, const uint8_t
  src[64])`.
- `wpm_loader_blob.h`: `extern const uint8_t wpm_loader_blob[0x4000] __attribute__((section(".sketch_boot")));`
  — `blob2c.py <bin> <out.cpp>` pads with 0xFF to 0x4000 and fails if the bin is larger.
- Jump: `__disable_irq(); SysTick->CTRL = 0; SCB->VTOR = 0x6000; __set_MSP(*(uint32_t*)0x6000);
  NVIC->ICER[0] = 0xFFFFFFFF; NVIC->ICPR[0] = 0xFFFFFFFF; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  __enable_irq(); ((void(*)())(*(uint32_t*)0x6004))();` (interrupts MUST be re-enabled before the
  branch: the SAMD core's startup never does it — review finding, 2026-10-22).

- [ ] **Step 1: Write `FramLoader.ino`, `fram.h`, `nvm.h`** per Interfaces, using `WpmCommon` for
  CRC and record handling.
- [ ] **Step 2: `build.sh FramLoader`** → expected: compiles under `wpm_loader`, bin ≤ 16,384 bytes
  (if over: `-Os` is default; drop `Serial`, keep only SPI + core), `wpm_loader_blob.cpp` regenerated.
- [ ] **Step 3: Add `#include "src/wpm_loader_blob.h"`** to `2Wireless.ino` and a reference that
  keeps the blob linked (the `KEEP` in the linker script suffices; verify with Step 4).
- [ ] **Step 4: `build.sh 2Wireless --profile wpm`** → expected: bin starts with the loader's vector
  table (`xxd -l 8` shows SP `0x20008000`, reset inside 0x2000..0x5FFF) and the app's vector table at
  file offset 0x4000 (reset ≥ 0x6000). `build.sh` now also writes `2Wireless.ota.bin` = bin minus the
  first 0x4000 bytes and prints its size and CRC32.
- [ ] **Step 5: Bench — first loader flash.** `flash.sh build/2Wireless/2Wireless.ino.bin`; after the
  Wi-Fi protocol, `/ssid` answers. If it does not: `flash.sh Firmware/arduino/2Wireless/bin/2Wireless.ino.bin`
  restores stock; note the failure in `bench/RESULTS.md` and iterate (Oren's bench policy).
- [ ] **Step 6: Bench — reject-to-jump path.** Stage a dummy image whose vectors are invalid: write
  `0000000000000000` at FRAM 0 and a hand-made pending record with `len=8`, matching CRC (compute with
  `python3 -c 'import zlib;print(hex(zlib.crc32(bytes(8))))'`) via `/writememory`; power-cycle with
  USB unplugged. Expected: the loader copies the 8 bytes, CRC matches, writes `last_result=1`, resets,
  then refuses to jump (vectors invalid) and blinks at 5 Hz; recovery = `flash.sh` of the Task 5 build.
  Record in `bench/RESULTS.md`.
- [ ] **Step 7: Commit** `feat(loader): FRAM first-stage loader embedded in the app's .sketch_boot section`

### Task 6: `/version` route, `FW_VERSION`, server fix, counter reset

**Files:**
- Create: `2Wireless/src/wpm_version.h`, `src/wpm_version.cpp`, `src/wpm_json.h`
- Modify: `2Wireless.ino` — `setupPageString` (line 198: `Firmware v1.18` → built from `FW_VERSION`),
  `loop()` (server.begin once), `/getpresets` handler (counter reset before `sendBackupPresets`),
  `handleWifiRequest` (new route dispatch **before** the default page branch)

**Interfaces:**
- `wpm_version.h`: `#define FW_VERSION "1.19"`; `String wpm_version_json(bool loaderPresent, uint8_t
  usbMode, uint8_t pollOnStart, uint32_t appCrc32)`; `const char* wpm_reset_cause_str()` (reads
  `PM->RCAUSE`: POR, BOD12/BOD33 → `"BOD"`, EXT, WDT, SYST); `bool wpm_loader_present()` = the four
  bytes at flash 0x2000 are not all 0xFF and `wpm_ota_check_vectors(flash 0x2000)` is OK with the
  app-base rule relaxed to ≥ 0x2000.
- `wpm_json.h`: `String wpm_json_err(const char* code)` → `{"ok":false,"error":"<code>"}`;
  `void wpm_send_json(WiFiClient& c, const String& body)` (writes the stock header via
  `writeHeader(client,"HTTP/1.1 200 OK","Content-type:application/json",body.length())` then the body).
- `app_crc32` source: the loader-recorded value from the record when `last_result==1`, else
  `"unknown"`. (Call: the app never computes it at boot; a USB-flashed app therefore reports
  `"unknown"` until its first OTA.)
- `tcp_sockets_free`: `null` (WiFi101 exposes no count); `rssi`: `WiFi.RSSI()`.
- `capabilities`: `["version","ota"]`.

- [ ] **Step 1: Write `test/test_version_json.cpp`** against a desktop-compilable formatter
  `size_t wpm_version_format(char* out, size_t cap, const WpmVersionInfo& info)` in `WpmCommon`
  (`struct WpmVersionInfo { const char* version; const char* board; const char* loader; const char*
  reset_cause; uint32_t uptime_s; int rssi; uint8_t usbMode; uint8_t pollOnStart; bool has_app_crc;
  uint32_t app_crc32; }`): expected exact string for one fixture:
  `{"version":"1.19","board":"mkr1000","loader":"fram-loader-1","capabilities":["version","ota"],"getpresets_wait":false,"usbMode":1,"pollOnStart":0,"reset_cause":"POR","uptime_s":42,"rssi":-50,"tcp_sockets_free":null,"app_crc32":"0A1B2C3D"}`
  and, with `loader=nullptr`/`has_app_crc=false`, the `loader` key absent and `"app_crc32":"unknown"`.
- [ ] **Step 2: Run tests** → FAIL.
- [ ] **Step 3: Implement** `wpm_version_format` in `WpmCommon/src/wpm_version_format.{h,cpp}` (uses
  `snprintf` only), then `wpm_version.{h,cpp}` in the sketch that fills `WpmVersionInfo` and calls it.
- [ ] **Step 4: Run tests** → `0 failures`.
- [ ] **Step 5: Wire the route** `else if (urlString.indexOf("/version") >= 0)` placed before the
  default-page branch and after any existing route that could match `"/version"` as a substring
  (there is none; keep it just above the default). Replace `Firmware v1.18` in the Setup page by
  splitting the literal: `"… <hr> Firmware v" FW_VERSION " </body> …"`.
- [ ] **Step 6: Server fix** — in `loop()`, remove `server.begin()` from the `WL_AP_CONNECTED`
  branch; call `server.begin()` once in `setup()` right after the `status != WL_AP_LISTENING` check.
  Keep `client.stop()` on disconnect.
- [ ] **Step 7: Counter reset** — in the `/getpresets?addr` branch move `write_counter=0;
  fram_address=0;` to just before `sendBackupPresets(address);` (keep `write_i2c_to_fram = true`
  before both).
- [ ] **Step 8: Build + bench**: flash; `curl --interface en1 http://192.168.0.1/version` → JSON with
  `"version":"1.19"` and `"loader":"fram-loader-1"`; `curl …/setup | grep -o "Firmware v1.19"`;
  `/currentpreset`, `/ssid`, `/getpresets?addr=76` behave as in Task 1 Step 5. Association churn
  test: toggle Wi-Fi on the Mac 5 times (join/leave BUCHLA200E), then `/currentpreset` still answers
  (stock would have died, spec §4).
- [ ] **Step 9: Commit** `feat(http): /version capability route, FW_VERSION 1.19, server.begin once, capture counter reset`

### Task 7: OTA routes — `/ota/status`, `/ota/abort`, `/ota/commit`

**Files:**
- Create: `2Wireless/src/wpm_ota.h`, `src/wpm_ota.cpp`
- Modify: `2Wireless.ino` (route dispatch; the three routes go before `/version`'s branch; note
  `indexOf("/ota/")` must be tested before any stock route — none contain `/ota/`)

**Interfaces:**
- `wpm_ota.h`: `void wpm_ota_handle_status(WiFiClient&)`, `void wpm_ota_handle_abort(WiFiClient&)`,
  `void wpm_ota_handle_commit(WiFiClient&, const String& urlString)` (parses `len=` and `crc32=`
  positionally after `?`: `/ota/commit?len=N&crc32=HEX`), `bool wpm_ota_busy()` (always false in PR 0;
  PR 1+ jobs hook it).
- Record access uses the sketch's `framRead/framWrite` (declare `extern`).
- `commit` order: busy → `"busy"`; `wpm_ota_check_len` → `"len"`; read FRAM[0..8) →
  `wpm_ota_check_vectors` → `"image"`; CRC over FRAM[0..len) in 256-byte chunks → `"crc"`; then write
  record (`len, crc32, flags=0, last_result unchanged, app_crc32 unchanged`, then magic), reply
  `{"ok":true,"pending":{"len":N,"crc32":"HEX"}}`, `client.stop()`, `delay(300)`, `NVIC_SystemReset()`.
- `status` reply: `{"loader":"fram-loader-1","pending":<bool>,"last":"<none|ok|crc_mismatch>","app_crc32":"<HEX|unknown>"}`
  (`"loader"` absent when `!wpm_loader_present()`).
- `abort`: clear magic only; `{"ok":true}`.
- HEX formatting: 8 upper-case hex digits, no `0x`.

- [ ] **Step 1: Write `test/test_ota_commit_logic.cpp`** for the pure decision function
  `WpmOtaCheck wpm_ota_commit_check(uint32_t len, const uint8_t first8[8], uint32_t crc_expected,
  uint32_t crc_actual, bool busy)` in `WpmCommon` returning the first failing check in the order above
  (add `WPM_OTA_ERR_CRC`, `WPM_OTA_ERR_BUSY` to the enum) and `const char* wpm_ota_check_str(WpmOtaCheck)`
  → `"len"|"image"|"crc"|"busy"`.
- [ ] **Step 2: Run tests** → FAIL. **Step 3: Implement.** **Step 4: Run** → `0 failures`.
- [ ] **Step 5: Implement the three handlers** in `wpm_ota.cpp` and dispatch them.
- [ ] **Step 6: Build + bench refusals** (no reboot expected): `/ota/status` → `pending:false,
  last:"none"`; `/ota/commit?len=0&crc32=00000000` → `"error":"len"`; write `FFFFFFFFFFFFFFFF` at FRAM 0,
  `/ota/commit?len=256&crc32=00000000` → `"error":"image"`; write the first 8 bytes of
  `2Wireless.ota.bin` at FRAM 0, `/ota/commit?len=256&crc32=00000000` → `"error":"crc"`;
  `/ota/abort` → `{"ok":true}`; `/ota/status` unchanged. Record in `bench/RESULTS.md`.
- [ ] **Step 7: Commit** `feat(ota): /ota/status, /ota/abort and /ota/commit with validated pending record`

### Task 8: Wi-Fi bring-up robustness after a software reset

**Files:**
- Modify: `2Wireless.ino` `setup()` Wi-Fi section

**Interfaces:**
- Replace the stock `if (status != WL_AP_LISTENING) { while (true); }` with a retry: up to 3 attempts
  of `WiFi.end(); delay(500); WiFi.begin-path (the existing `WiFi.config` + `beginAP`)`; after the
  third failure fall through to the stock `while(true)` (so stock behaviour is the last resort). Add
  the same treatment to the `WL_NO_SHIELD` check (3 × `WiFi.init` via `WiFi.status()` with 500 ms gaps).

- [ ] **Step 1: Implement** the retry loops.
- [ ] **Step 2: Build + bench — software reset.** `flash.sh` ends with a bossac-issued software reset
  (RCAUSE=SYST, WINC left powered), which is the same condition an OTA reset produces. After the Wi-Fi
  protocol, `/version` must report `"reset_cause":"SYST"` and HTTP must answer on the first try.
  Repeat 3 times. Record. (Task 9's OTA round trip exercises the same path again.)
- [ ] **Step 3: Commit** `fix(wifi): retry AP bring-up after a software reset instead of halting`

### Task 9: OTA round trip on the bench

**Files:**
- Create: `Firmware/arduino/bench/ota_push.py` (stages an `.ota.bin` through `/writememory` in
  64-byte elements, ≤ 15 KB parts, 500 ms apart, socket bound to en1's IP; then `/readmemory` verify
  in 8 KB chunks; prints the CRC32 and the ready `/ota/commit` URL), `bench/wifi_rejoin.sh` (the 60 s
  protocol), `bench/RESULTS.md` (append)

- [ ] **Step 1: Write `ota_push.py`** (`--base http://192.168.0.1 --iface en1 <file.ota.bin>`; refuses
  files > 0x2BD18 bytes; exits non-zero on verify mismatch).
- [ ] **Step 2: Build variant B**: add `const char* build` to `WpmVersionInfo` and a `"build":"<tag>"`
  key emitted only when non-empty (extend Task 6's formatter test with one tagged fixture); `build.sh
  --tag b` passes `--build-property "build.extra_flags=-DFW_BUILD_TAG=\"b\""` (merged with the
  profile's existing extra_flags); then `build.sh 2Wireless --profile wpm --tag b`.
- [ ] **Step 3: Bench**: with variant A (no tag) running, `ota_push.py` variant B's `.ota.bin`;
  `/ota/commit?len=<N>&crc32=<HEX>` → `{"ok":true,…}`; WPM resets; after `wifi_rejoin.sh`,
  `/version` shows `"build":"b"`, `/ota/status` → `last:"ok"`, `app_crc32` = the pushed CRC. Then OTA
  back to variant A the same way. Time each phase and record (push time, copy time = gap between
  commit reply and HTTP return).
- [ ] **Step 4: Commit** `bench(ota): push script and recorded OTA round trip`

### Task 10: Robustness bench — power cuts and bad images (Oren's policy)

**Files:**
- Create: `bench/POWERCUT.md` (procedure + results table)

- [ ] **Step 0: Slow bench loader.** Add a compile-time `LOADER_SLOW_COPY_MS` (default 0) to
  `FramLoader` that `delay()`s that many ms per 256-byte row; `build.sh FramLoader --slow 50` builds a
  bench loader (~22 s copy for a 110 KB image). Flash the app with the slow loader embedded once for
  this task.
- [ ] **Step 1: Mid-copy power cut.** Stage variant B, commit; after the reply, Oren cuts case power
  at roughly 5 s, 10 s and 20 s into the copy (three repetitions). Expected after each power-on: the
  loader re-runs the copy from the start, `last:"ok"`, variant B running.
- [ ] **Step 2: Cut after copy, before clear.** With the slow loader, cut power during the flash CRC
  pass (the LED pattern or timing from Step 1 tells when). Expected: re-copy on next boot, boots B.
- [ ] **Step 3: Corrupt staged image.** Stage B, flip one byte in FRAM at 0x1000 via `/writememory`,
  commit with the correct CRC of the *original* → `"error":"crc"` (commit-time catch). Then write the
  correct record by hand with a wrong CRC (bypassing commit) via `/writememory` at `0x02BE50`,
  power-cycle → `last:"crc_mismatch"`, old app still runs.
- [ ] **Step 4: Oversize and broken vectors** (repeat of Task 7 refusals after a real reboot).
- [ ] **Step 5: Recovery drill.** Deliberately brick: hand-write a record pointing at an all-zero
  image with a matching CRC and `len=0x1000`; power-cycle → loader copies zeros, refuses to jump,
  blinks. Recover with `flash.sh build/2Wireless/2Wireless.ino.bin`. Expected: back in one upload.
- [ ] **Step 6: Restore the production loader** (no slow delay), run Task 9 Step 3 once more.
- [ ] **Step 7: Commit** `bench(ota): power-cut, corruption and recovery results`

### Task 11: README and PR text

**Files:**
- Modify: `Firmware/arduino/README.md`; Create: `docs/pr/PR0.md`

- [ ] **Step 1: README sections**: Build (deps, profiles, `build.sh`), Flash over USB (Device mode,
  data cable, the 1200-touch erase warning, SAM-BA "v2.0 [Arduino:XYZ] Dec 20 2016" string, recovery
  with the stock bin), OTA (image = `.ota.bin`, stage via `/writememory`, `/ota/*` contract verbatim
  from the spec, "stock v1.18 units need one USB upload to get OTA"), `/version` fields, the
  compatibility contract, bench results summary with links.
- [ ] **Step 2: `docs/pr/PR0.md`**: title, summary, the contract, what was bench-tested, how to
  reproduce, the attribution line.
- [ ] **Step 3: Commit** `docs: PR 0 README and pull-request text`

### Task 12: Release binaries and branch finish

- [ ] **Step 1:** `build.sh 2Wireless --profile wpm` (production loader, no tag); copy
  `2Wireless.ino.bin` → `Firmware/arduino/2Wireless/bin/2Wireless-1.19.ino.bin` and `.ota.bin` →
  `bin/2Wireless-1.19.ota.bin`; leave `bin/2Wireless.ino.bin` (v1.18) untouched.
- [ ] **Step 2:** Flash the release build, run Task 1 Step 5, Task 2 Step 4, Task 6 Step 8, Task 9
  Step 3 once more from the committed binaries; append to `bench/RESULTS.md`.
- [ ] **Step 3: Commit** `release: 2Wireless 1.19 binaries (app + OTA image)`; then use
  superpowers:finishing-a-development-branch.
