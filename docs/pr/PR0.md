# PR 0: buildable firmware tree, `/version`, server fix, FRAM-staged OTA

## Summary

- The WPM firmware (Arduino MKR1000 sketch) now builds reproducibly from source with
  `Firmware/arduino/build.sh` (arduino-cli, core `arduino:samd@1.8.14`, `WiFi101@0.16.1`, vendored
  MIDIUSB and USB_Host_Library_SAMD, a `boards.local.txt` overlay for the Studio H USB identity), and
  `flash.sh` uploads over USB with bossac.
- Vendored `WireByte` (per-byte I2C slave serving); `/getpresets` capture data is served from FRAM per
  byte. Bench B1 (2026-10-22): on the same 259e bank it captures the same 1069 bytes stock v1.18 did.
- New `GET /version` capability route. `FW_VERSION` is now "1.19".
- The AP server is re-begun on each association (as stock does) plus a 10 s idle refresh, because the
  WINC1500 drops its AP listening socket on station re-association while WiFi101 still reports it
  listening (found on the bench). The capture counter is reset before each `sendBackupPresets`.
- Wi-Fi bring-up retries three times (`WiFi.end()`, 500 ms, reconfigure) before the stock halt, so a
  software reset with USB attached comes back on Wi-Fi.
- FRAM-staged OTA: a small loader at flash 0x2000 copies a staged, CRC-checked image from FRAM into the
  app region at 0x6000. New routes `/ota/status`, `/ota/abort`, `/ota/commit`.

Flash layout: SAM-BA bootloader 0x0000-0x1FFF (stock, kept for USB recovery); FRAM loader 0x2000-0x5FFF
(0x4000-byte blob built from `FramLoader`, embedded in the app build); app from 0x6000. USB flashing
uses the full `.ino.bin`; OTA uses the app-only `.ota.bin`.

Stock v1.18 units need one USB upload of the full 1.19 `.ino.bin` to get OTA; after that, updates can go
over Wi-Fi. The shipped v1.18 binary `Firmware/arduino/2Wireless/bin/2Wireless.ino.bin` is the recovery
image and is not modified.

## Compatibility contract

Nothing existing changes:

- All stock routes keep URL, parameter order, JSON shapes and default behaviour; stock pages work
  unchanged.
- FRAM map untouched. New state only in the reserved range 0x02BE4D-0x02BE7F (the OTA record is 18 bytes
  at 0x02BE50, magic "OTA1"). Staged OTA data goes through the existing `/writememory` from FRAM address
  0, so staging overwrites the preset transfer buffer but never the stored settings.
- USB identity 0x1209:0xFBA0 "Studio H Software USB Midi", AP BUCHLA200E and the bus protocol unchanged.
- New behaviour only through new routes; refusals are HTTP 200 with
  `{"ok":false,"error":"len|image|crc|busy"}`.
- `FW_VERSION` is "1.19".

## App image marker and the 1200-baud touch

App image marker: every valid app image carries a 16-byte marker at app-image offset 0x1C
(vector-table entries 7..10, reserved on ARMv6-M and never fetched, so the Arduino core leaves
them zero): `"WPMAPP1\0"`, link base LE32 0x6000, image length LE32 (the `.ota.bin` length).
In the full `.ino.bin` it sits at file offset 0x401C. `build.sh` stamps it and refuses to build
if the slot is not empty. The "head" is the first 44 bytes of an app image; `/ota/commit` and the
loader both check it (vectors plus marker, marker length == record len), so a stock or full image
is refused before anything is erased. `build.sh` also removes `2Wireless.ino.hex` (it is unstamped;
`flash.sh` takes the `.bin`).

The vector ranges alone cannot tell a 0x2000-linked binary (for example the stock v1.18
`bin/2Wireless.ino.bin`) from a 0x6000-linked one; the marker can, and it is the only guard against
staging such an image. `/ota/status` `last` is one of `ok`, `crc_mismatch`, `image`, `none`.
`app_crc32` is the CRC of the last OTA-installed image: after a USB (bossac) flash the record still names that
image, so `app_crc32` describes the running app only after an OTA; clients must not treat it as the running
image's identity after a USB flash.

The 1200-baud touch (done by `flash.sh` and `arduino-cli upload`, not by bossac itself) erases the flash
row at 0x2000 before resetting into SAM-BA (Arduino core `Reset.cpp`). By design SAM-BA then stays
resident, because the vector at 0x2000 is invalid, and the upload rewrites the whole image, so USB
recovery is unchanged (the erase itself was not separately inspected on the bench). `flash.sh` is the
only sanctioned flashing path; never do the touch without an upload following it.

## What was bench-tested

Desktop tests (`make -C Firmware/arduino/test`) pass and the tree compiles clean. The device bench ran on
Oren's WPM in its 200e case on 2026-10-22; tables are in
[`Firmware/arduino/bench/RESULTS.md`](../../Firmware/arduino/bench/RESULTS.md) (B1-B6) and
[`Firmware/arduino/bench/POWERCUT.md`](../../Firmware/arduino/bench/POWERCUT.md). In order:

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

Not covered by the bench / still true:

- `busy` is never returned by PR 0 (`wpm_ota_busy()` is hard-coded false; no bus jobs exist yet).
- A raw `/getpresets` -> `/setpresets` round trip shifts a bank by design (the capture stream is
  `[addr16][data]` per bus line and a restore feeds FRAM verbatim); the Get/Set pages de-interleave.
- The 1200-baud touch erases the loader's first row, so `flash.sh` (touch followed by upload) is the only
  sanctioned flashing path.
- `build.sh FramLoader --slow <ms>` is a bench artefact and is never committed.
- A stock unit's `/version` answer and the `v2/` prefix behaviour are spec statements, not device-checked.
- Main-loop FRAM transactions (including the commit CRC pass, ~2 s) are not guarded against the I2C ISR's
  own SPI transactions. This is pre-existing; mangled data is caught by the commit CRC and the loader's
  checks. A critical section is an option for PR 1+.
- Boot timing with no OTA pending is unmeasured. The loader runs the core's SystemInit, including
  XOSC32K/DFLL start-up, and the app then restarts them; B6 suggests a small overhead. Measure
  POR-to-first-I2C-ACK before relying on it, or build the loader CRYSTALLESS.
- `/version` can take up to ~1 s, because `WiFi.RSSI()` waits for a callback in AP mode.
- Residual loader window, two independent faults needed: a power cut inside row 0's four page writes
  (or between row 0 and the flash verify, tens of ms) followed by a misread of the FRAM record's magic
  on the next boot could jump into an image that was written but never CRC-verified. With the magic read
  correctly the copy simply re-runs. Writing row 0's pages in reverse order would narrow it further; not
  done in PR 0 (the loader bytes were frozen after the re-bench).

## How to reproduce

    cd Firmware/arduino
    make -C test                                  # desktop tests (CRC32, OTA record, loader logic, /version)
    ./build.sh FramLoader                         # loader blob (only when the loader changes)
    ./build.sh 2Wireless --profile wpm            # build/2Wireless/2Wireless.ino.bin + 2Wireless.ota.bin
    ./flash.sh build/2Wireless/2Wireless.ino.bin  # USB, full image (first time on a stock unit)

OTA, with the Mac joined to the WPM AP (BUCHLA200E):

    cd bench
    ./wpm.sh get version
    python3 ./ota_push.py ../build/2Wireless/2Wireless.ota.bin   # stages via /writememory, verifies, prints the commit URL
    ./wpm.sh get 'ota/commit?len=<N>&crc32=<HEX>'
    ./wifi_rejoin.sh
    ./wpm.sh get version
    ./wpm.sh get ota/status

The A to B to A results are in `Firmware/arduino/bench/RESULTS.md` (B6) and the power-cut procedure in
`Firmware/arduino/bench/POWERCUT.md`.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01TT8DVnxoAbrr5uYq3D89kJ
