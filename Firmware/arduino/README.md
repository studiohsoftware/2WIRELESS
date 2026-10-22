# WPM firmware (Arduino MKR1000)

Firmware for the Buchla 200e Wi-Fi preset manager (WPM): an Arduino MKR1000 (SAMD21) with a 256 KB SPI
FRAM. This tree builds the app from source, serves `/version`, and can update itself over Wi-Fi (OTA).

Flash layout (flash addresses):

| Range | Contents |
| --- | --- |
| 0x0000-0x1FFF | SAM-BA bootloader, stock ("v2.0 [Arduino:XYZ] Dec 20 2016"); kept for USB recovery |
| 0x2000-0x5FFF | FRAM loader (0x4000-byte blob, built from `FramLoader` with the `wpm_loader` profile and embedded in the app build) |
| 0x6000- | the app |

## Build

Needs `arduino-cli` and `python3`. `build.sh` installs the pinned core `arduino:samd@1.8.14` and
`WiFi101@0.16.1`, and copies `boards.local.txt` next to the core's `boards.txt`.

    ./build.sh 2Wireless --profile wpm        # -> build/2Wireless/2Wireless.ino.bin and 2Wireless.ota.bin
    ./build.sh FramLoader                      # when the loader changes; regenerates src/wpm_loader_blob.cpp
    ./build.sh 2Wireless --profile wpm --tag b # same, with FW_BUILD_TAG "b" (reported as "build" by /version)

- The 2Wireless build compares against `build/FramLoader/FramLoader.ino.bin`; when that file is missing
  (a fresh clone) `build.sh` builds the loader automatically first.
- Profiles (`boards.local.txt`): `wpm` (Studio H USB identity 0x1209/0xFBA0, "Studio H Software USB
  Midi"), `wpm_loader` (no USB stack, used for `FramLoader`), `mkr1000` (stock core entry).
- `build.sh` prints size and CRC32 of the `.ino.bin`. For every 2Wireless profile (`wpm` and `mkr1000`) it also checks both vector
  tables (loader at offset 0, app at offset 0x4000), checks the embedded loader blob is current, stamps the app marker (below) into the `.ino.bin`, removes the unstamped `2Wireless.ino.hex`, and
  writes `2Wireless.ota.bin`: the `.ino.bin` without its first 0x4000 bytes (the app-only image,
  at most 0x2BD18 bytes, even length).
- MIDIUSB and USB_Host_Library_SAMD are vendored in `libraries/` with small patches; see each
  library's `WPM-CHANGES.md`. `libraries/WpmCommon` holds the CRC32, OTA record and `/version`
  formatter shared by the app, the loader and the desktop tests (`make -C test`).
- `2Wireless/src/WireByte.{h,cpp}` is the vendored per-byte I2C slave (class `TwoWireByte`, global
  `Wire`); `WireByte.diff` shows the change against the core's `Wire.cpp`. `/getpresets` capture data is
  served from FRAM per byte through it. Bench B1 (2026-10-22) showed it captures the same 1069 bytes as
  stock v1.18 from the same 259e bank; see Bench results.

## Flash over USB

- Put the WPM in USB Device mode and use a data cable (not charge-only). No double-tap of reset is
  needed (the touch resets into SAM-BA; every `flash.sh` upload on the bench used it).
- Flash the FULL `build/2Wireless/2Wireless.ino.bin` (loader + app). The `.ota.bin` is only for OTA.

      ./flash.sh build/2Wireless/2Wireless.ino.bin [port] [--dry-run]

- `flash.sh` does the Arduino 1200-baud touch (bossac itself does not) only when it is about to upload,
  then runs bossac with verify and reset, and logs the full debug output to
  `bench/bossac-<timestamp>.log` (ignored by version control). `flash.sh` refuses the port in
  `WPM_EXCLUDE_PORT` (default `/dev/cu.usbmodem192573201`, another board on the bench Mac; set the
  variable to override it).
- The 1200-baud touch (done by `flash.sh` and by `arduino-cli upload`) erases the flash row at 0x2000
  before resetting into SAM-BA (the Arduino core's `Reset.cpp`). Under this layout that row is the
  loader's first row. By design, SAM-BA then stays resident on the next reset because the app vector
  is invalid, and the upload rewrites the whole image, so USB recovery is unchanged (the erase itself
  was not separately inspected on the bench). `flash.sh` is therefore the only sanctioned way to flash:
  never do the touch without an upload following it.
- The bootloader identifies as "v2.0 [Arduino:XYZ] Dec 20 2016".
- Recovery: the shipped v1.18 binary `2Wireless/bin/2Wireless.ino.bin` is the recovery image and is
  never modified. Put the board in the bootloader and
  `arduino-cli upload -b arduino:samd:mkr1000 -p <port> -i 2Wireless/bin/2Wireless.ino.bin`.
  (That image is linked at 0x2000; it overwrites the loader and the unit is a stock v1.18 unit again.)

## OTA

Stock v1.18 units need one USB upload (of the full 1.19 `.ino.bin`) to get OTA. After that, updates go
over Wi-Fi.

How it works: the image is `build/2Wireless/2Wireless.ota.bin`. Stage it in FRAM with the existing
`/writememory` route starting at FRAM address 0 (the capture/transfer area), then call `/ota/commit`.
Staging overwrites the preset transfer buffer but never the stored settings. The commit writes a pending
record to FRAM `0x02BE50` (18 bytes, magic "OTA1", inside the range 0x02BE4D-0x02BE7F that is
reserved for new state) and resets the WPM. On the next boot the loader at 0x2000 sees the record,
checks `len` and the staged head (vectors plus app marker), checks CRC32 over FRAM `[0..len)`, erases and writes flash from 0x6000 in 256-byte rows
(row 0, the vectors and the marker, is erased first and written last, so flash never looks like a
plausible app while a copy is incomplete), verifies the flash CRC, clears the magic, records the result
and jumps to the app. A bad CRC (`last_result` 2) or a refused len/head (3, `image`) is detected before
any erase: it clears the magic and boots the existing app. The loader never erases itself, so a power
cut re-runs the copy on the next boot; if the flash verify fails, row 0 is erased again and the copy is
retried at most 3 times, after which the magic is cleared and the next boot takes the hand-off path
below (never a jump into an unverified image). If flash holds no plausible app (the
head check fails: vectors or marker), the loader blinks the LED at 5 Hz for 3 s and then hands the unit to the SAM-BA
bootloader (double-tap magic + reset), so the "Arduino MKR1000" USB port appears and one `flash.sh`
upload recovers it without reaching the reset button; each power-up repeats the blink and hand-off.

`bench/ota_push.py <file.ota.bin>` stages and read-back-verifies an image and prints the commit URL; it
never calls `/ota/commit`.

App image marker: every valid app image carries a 16-byte marker at app-image offset 0x1C
(vector-table entries 7..10, reserved on ARMv6-M and never fetched, so the Arduino core leaves
them zero): `"WPMAPP1\0"`, link base LE32 0x6000, image length LE32 (the `.ota.bin` length).
In the full `.ino.bin` it sits at file offset 0x401C. `build.sh` stamps it and refuses to build
if the slot is not empty. The "head" is the first 44 bytes of an app image; `/ota/commit` and the
loader both check it (vectors plus marker, marker length == record len), so a stock or full image
is refused before anything is erased. `build.sh` also removes `2Wireless.ino.hex` (it is unstamped;
`flash.sh` takes the `.bin`).
`bench/ota_push.py` refuses a file without a valid marker before any network call.

Route contract (all replies HTTP 200 with a JSON body):

- `/ota/commit?len=N&crc32=HEX` checks in order job running (`"error":"busy"`), `len` (0, odd, or
  greater than 0x2BD18 gives `"error":"len"`), image head (word 0 in 0x20000000..0x20008000, word 1
  odd and inside 0x6000..0x40000, marker at 0x1C with base 0x6000 and length == `len` gives `"error":"image"`), CRC over FRAM (`"error":"crc"`). `busy` is
  never returned by this firmware today (`wpm_ota_busy()` is hard-coded false; no bus jobs exist yet).
  On success it writes the record, replies `{"ok":true,"pending":{"len":N,"crc32":"HEX"}}`,
  then calls `NVIC_SystemReset()` after the reply is flushed.
- `/ota/status` replies `{"loader":"fram-loader-1","pending":false,"last":"ok"|"crc_mismatch"|"image"|"none","app_crc32":"HEX"}`.
  `last` persists until the next commit or abort; a commit resets it to none so a stale ok can never
  be read as the new commit's outcome before the loader reports. `/ota/abort` clears the magic and
  sets `last` to none. `app_crc32` is kept in the record but only reported while `last` is "ok".
  `loader` is absent when the loader section is not present; `app_crc32` is `"unknown"` unless `last` is `"ok"`.
  After a USB (bossac) flash the record still names the last OTA-installed image, so `app_crc32` describes
  the running app only after an OTA; clients must not treat it as the running image's identity after a USB flash.
- `/ota/abort` replies `{"ok":true}`.
- Refusals are HTTP 200 with `{"ok":false,"error":"len|image|crc|busy"}`, never 4xx/5xx.

Other changes in this PR: Wi-Fi bring-up retries three times (`WiFi.end()`, 500 ms, reconfigure) before
the stock halt, so a software reset with USB attached comes back on Wi-Fi. The server is re-begun on
each AP association (as stock does) and also refreshed after 10 s idle: the WINC1500 drops its AP
listening socket on station re-association while WiFi101 still reports it listening, so a single
`begin()` in `setup()` left port 80 refused (found on the bench, 2026-10-22). The capture counter is
reset before each `sendBackupPresets`.

## /version

`GET /version` returns (one line; shape from `wpm_version_format`):

    {"version":"1.19","board":"mkr1000","loader":"fram-loader-1","capabilities":["version","ota"],
     "getpresets_wait":false,"usbMode":1,"pollOnStart":0,"reset_cause":"POR","uptime_s":123,"rssi":-45,
     "tcp_sockets_free":null,"app_crc32":"HEX"}

| Field | Meaning |
| --- | --- |
| `version` | `FW_VERSION`, now "1.19". Every PR bumps it; clients key on `capabilities`, not the number |
| `build` | present only in builds made with `build.sh --tag`; placed right after `version` |
| `board` | always "mkr1000" |
| `loader` | "fram-loader-1" when a plausible loader is at flash 0x2000; key omitted otherwise (dev builds) |
| `capabilities` | currently `["version","ota"]`; later PRs append |
| `getpresets_wait` | false |
| `usbMode` | the stored USB mode (1 = Device, 0 = Host) |
| `pollOnStart` | the stored "poll modules on startup" setting (0/1) |
| `reset_cause` | "POR", "BOD", "EXT", "WDT", "SYST" or "UNKNOWN", from the reset-cause register |
| `uptime_s` | seconds since boot |
| `rssi` | `WiFi.RSSI()` |
| `tcp_sockets_free` | currently always `null` (not measured in PR 0) |
| `app_crc32` | CRC32 of the last OTA-installed image as 8 hex digits when the last OTA result is ok; otherwise "unknown" |

Per the design spec, stock units answer `/version` with the HTML home page (HTTP 200), which clients
treat as "stock" (a spec statement, not yet checked on a device).

## Compatibility contract

Nothing existing changes (design spec section 3):

- Every existing route keeps its URL, parameter order, reply shape and default behaviour. New parameters
  are optional and trailing. The stock pages (PresetManager, GetPresets, SetPresets, WriteFirmware, Setup,
  home page) keep working; the Setup page shows "Firmware v1.19".
- The FRAM layout (`Firmware/fram_map.txt`) is untouched. New state lives only in the reserved range
  0x02BE4D-0x02BE7F and is zero on stock units.
- USB identity (0x1209:0xFBA0 "Studio H Software USB Midi"), AP name BUCHLA200E and the I2C bus
  protocol are unchanged by design. A `v2/` path prefix keeps its stock meaning on every route (spec
  statement, not yet checked on a device).
- Boot timing unchanged and "the loader adds well under 1 ms when no OTA is pending" are design targets
  from the spec, not measurements. The bench measured the OTA timings (push ~47 s, verify ~20 s, copy ~1 s
at full speed), see Bench results.
- New behaviour arrives only through new routes (`/version`, `/ota/*`). Refusals on new routes are
  HTTP 200 with `{"ok":false,"error":"<code>"}`.
- `FW_VERSION` is "1.19".

## Bench results

Hardware bench run on Oren's WPM (in its 200e case), 2026-10-22. Tables and raw numbers are in
[`bench/RESULTS.md`](bench/RESULTS.md) (B1-B6) and [`bench/POWERCUT.md`](bench/POWERCUT.md) (power cuts, corruption, recovery, production-loader
round trip). In order:

- **Reference capture (B1).** The 259e was restored from the original stock-v1.18 capture; the WireByte
  build then captured the same 1069 bytes, so the per-byte serving path is equivalent on that bank. The
  earlier "shifted" second capture was the raw `getpresets` -> `setpresets` round trip re-sending the
  2-byte line addresses as data (by design; the GetPresets/SetPresets pages de-interleave first), not a
  WireByte fault.
- **First loader flash over USB (B2).** The full image verified, the app re-enumerated as the Studio H
  CDC device and the AP came up.
- **AP-listener finding and fix.** After one station re-association TCP port 80 was refused although the
  app was running and WiFi101 still reported the AP listening (`WL_AP_LISTENING`, server listening). The
  WINC1500 drops its AP-mode listening socket when a station (re)associates while WiFi101 still flags it
  as listening; stock v1.18 masked this by calling `server.begin()` on each association. The earlier
  PR 0 change to a single `begin()` in `setup()` was therefore wrong. The fix re-begins the server per
  association, as stock does, plus a refresh after 10 s idle. After it, 5 join cycles recovered HTTP in
  about 6 s each (the Mac's association time) and 30/30 polls over 60 s succeeded; the Setup page shows
  "Firmware v1.19" and `/ssid`, `/`, `/readmemory`, `/currentpreset` are unchanged (B3).
- **Software-reset bring-up (B5).** Six flash resets with USB attached; the AP came up every time (one
  Mac join still needed after each reset; the full rejoin script took about 66-67 s in the round trip).
- **OTA round trip (B6).** A -> B -> A with 113,868-byte variants: push ~47 s (19 POSTs of 6080 B),
  verify ~19 s, commit reply 2 s, app back about 2 s after the commit, `/version` showed the new build
  tag and `app_crc32`, `/ota/status` `last` "ok", both directions.
- **Commit-time refusals (B4 and power-cut bench Step 4).** `len` 0, 1001 and 179482 gave `len`;
  `len=1000` over the preset bank or over a staged image gave `image` (marker length mismatch); the stock
  v1.18 head (first 64 bytes at FRAM 0) gave `image`; a valid staged image with `crc32=DEADBEEF` gave
  `crc`; a staged image with one flipped byte, committed with the original CRC, gave `crc`. The app kept
  running and `/ota/status` stayed `pending:false` each time.
- **Six timed power cuts (Steps 1-2).** All six passed: the new app ran and `last` was "ok". Two landed
  mid-copy (the loader re-copied from row 0 on the next boot, LED solid ~20 s after power-on with the 50 ms
  bench loader, reset cause SYST); three landed after the record was committed (direct jump, reset cause
  POR); one was unsynced. Those six ran on the loader before the row-order fix; the fixed loader was
  re-benched with two early mid-copy cuts (~5 s and ~10 s into the copy, both re-copied on the next
  boot), a round trip and the brick drill (POWERCUT.md, "Re-bench after the row-order fix").
  The bench loader copies at 50 ms per row (~22 s for 445 rows); the production loader takes about 1 s.
- **Hand-written records (Steps 3 and 4).** A record naming a staged image with a flipped byte gave
  `last` "crc_mismatch"; a record naming the stock v1.18 head gave `last` "image". Both were rejected
  before any erase (LED on for under 1 s at most), the old app kept running and `pending` cleared.
- **USB brick drill (Step 5).** With the first app row (the vector table) zeroed, the unit stayed in
  SAM-BA: it enumerated as "Arduino MKR1000" (0x2341:0x004E) and HTTP never came up (the 5 Hz blink
  for 3 s was not watched; the hand-off was seen from the Mac). One `flash.sh` upload recovered the unit,
  with no 1200-baud touch (it found the bootloader port directly); the staged image was intact.
- **Production-loader round trip, after the bench.** The untagged production build (loader 8192 B, ota
  113,892 B) was flashed over USB, updated OTA to variant b and back to the production image; both
  commits ended `last` "ok" with the expected `app_crc32`, and the production copy took about 1 s.

Not covered by the bench: `busy` is never returned (no background job exists yet); a stock unit's
`/version` answer (the HTML home page) and the `v2/` prefix behaviour remain spec statements.

### Rerunning the bench

Join the Mac to the WPM AP (BUCHLA200E, interface `en1` in the bench scripts), then from `Firmware/arduino`:

    python3 bench/ota_push.py build/2Wireless/2Wireless.ota.bin --iface en1   # stage + read-back verify, prints the commit URL (never commits)
    curl --interface en1 '<commit URL>'                                        # commit; the WPM resets
    bash bench/wifi_rejoin.sh                                                  # after any reset: wait, join, poll until HTTP answers (exit 0 = up)

`bench/wpm.sh get|post <path>` is a curl wrapper bound to `en1`. The cut, corruption and recovery
procedure is `bench/POWERCUT.md`. Close any serial monitor before `flash.sh`: a reader holding the CDC
port with DTR asserted defeats the 1200-baud touch and nothing is erased. A USB data cable back-feeds the
board, so a case power cycle only resets it with USB unplugged.

`./build.sh FramLoader --slow <ms>` builds a loader that waits `<ms>` per row (the power-cut bench used
50) and regenerates `src/wpm_loader_blob.cpp`. It is a bench artefact: restore the blob afterwards
(`git checkout 2Wireless/src/wpm_loader_blob.cpp`) and never commit the slow blob.
