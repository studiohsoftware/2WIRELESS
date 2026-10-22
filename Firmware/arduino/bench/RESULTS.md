# Bench results

## Task 1 baseline (2026-10-21)

Untouched sketch, `wpm` profile, flashed to Oren's WPM (in its 200e case).

    ./build.sh 2Wireless --profile mkr1000   -> size 109924 crc32 5D0CB530  (soft target 108,612 +-3%: +1.2%)
    ./build.sh 2Wireless --profile wpm       -> size 109940 crc32 AC31B388
    strings build/2Wireless/2Wireless.ino.bin | grep -c "Studio H Software USB Midi"   -> 1
    ./flash.sh build/2Wireless/2Wireless.ino.bin
        (touch on /dev/cu.usbmodem83344203, bootloader /dev/cu.usbmodem83344201; "Verify successful", CPU reset)

bossac log (H19 reference): `Firmware/arduino/bench/bossac-20261021-225213.log`

After reset: the first `networksetup -setairportnetwork en1 BUCHLA200E BUCHLA200E` at 60 s printed
"Could not find network" (no association made); a second attempt about 90 s later joined.

    curl --interface en1 http://192.168.0.1/currentpreset   -> {"currentpreset": "1"}
    curl --interface en1 http://192.168.0.1/ssid            -> {"ssid": "BUCHLA200E"}
    curl --interface en1 'http://192.168.0.1/getpresets?addr=76' -> "module_address": "0x76", "data": ""
    ioreg: "Studio H Software USB Midi" present (VID 4617 = 0x1209), CDC port /dev/cu.usbmodem83344203

## Task 2 (WireByte), 2026-10-21

    ./build.sh 2Wireless --profile wpm --verbose  -> size 109940 crc32 C494FF32 (stock-source build: 109940 AC31B388)
    grep -c "libraries/Wire/" <verbose log>        -> 0 ; WireByte.cpp compiled and linked, no duplicate symbols

Bench, new build (module 259e, addr 0x28, joined after ~60 s):
- /getpresets?addr=28 returned 1069 bytes of data (2138 hex chars); /readmemory?addr=0x0&length=1069 returned data.
- /setpresets?addr=28 returned an empty body; after 10 s a second /getpresets differed from the first:
  second capture has extra leading bytes (`00000000fff06ea0...` vs `0000fff06ea0...`), i.e. shifted, 1483 of
  2138 hex chars differ. NOT byte-identical. Not yet known whether stock v1.18 behaves the same.
- Reference run with stock v1.18 NOT completed: after flashing it the AP was never joinable within the
  3-join limit (en1 stayed on the 192.168.0.x LAN, whose 192.168.0.1 answers "Not Found" to everything).
  The new build re-flashed afterwards was also not joinable within 3 joins (60 s + 3 attempts).
  Needs Oren: AP check / case power cycle, then rerun the stock reference and the new build.
Final device state: new build (crc C494FF32) flashed.
Post-analysis (controller, 2026-10-21 23:00): the shift is by design, not a WireByte fault. The capture
stream is `[addr16][data]` per bus line and a restore (`/setpresets`) feeds FRAM verbatim, so a raw
getpresets->setpresets round trip re-sends the 2-byte line addresses as data (GetPresets.html/SetPresets.html
de-interleave first). The second capture was compared with `/readmemory` and equals the FRAM content
byte-for-byte, which is what the per-byte serving path must deliver. Equivalence to v1.18 remains to be
shown by B1 (restore the 259e from the de-interleaved cap1, expect re-capture == cap1; then the stock
reference run).

## B1 (Task 2 bench), 2026-10-22 — 259e restore and reference capture

Device: T2-era build (WireByte, crc C494FF32) after an Oren case power cycle, USB unplugged; Mac joined
BUCHLA200E (en1 192.168.0.100), WPM answered /currentpreset and /ssid.

    restore_259e.py new-cap1.json   (cap1 = the original capture taken on stock v1.18 before any flashing)
    cap1: 1069 bytes, 37 lines, flat bank 0x0000-0x03DE (990 bytes), trailing frame 0400280a28
    4 POSTs to /writememory (10 lines each) -> HTTP 200 x4 ; /readmemory read-back == flat bank: True
    /setpresets?addr=28 -> empty 200 ; after 10 s /getpresets?addr=28 -> 1069 bytes, identical to cap1: True

Result: the 259e holds its original presets again, and the WireByte build captures the same 1069 bytes
that stock v1.18 captured from the same bank (cap1), so the per-byte serving path is equivalent on this
bank. The separate "re-flash stock v1.18 and capture" step is unnecessary: cap1 is that reference.
Note: the first POST of part 2 was closed by the WPM without a response when sent 0.5 s after part 1
(the stock server is still consuming the previous body); 1.5 s spacing + retry fixed it.

## B2–B6 (Tasks 5–9 bench), 2026-10-22

Device: USB data cable attached throughout (every flash ends in a bossac software reset, reset_cause SYST).

B2 first loader flash (loader build at a73c7bd, 130156 B): verify successful, app re-enumerated as the
Studio H CDC device, AP up. HTTP refused on port 80 after my first join → root-caused with a WPM_DIAG
build (bring-up log over USB CDC): shield=0 ap=7 (WL_AP_LISTENING) srv listening=1, i.e. the app was
running and the driver believed the listener alive, yet TCP 80 was refused after one station
re-association. The WINC1500 drops its AP-mode listening socket on (re)association; stock v1.18 masked
it with server.begin() per WL_AP_CONNECTED transition, which bf047fa had reduced to one begin() in
setup(). Fixed in a8b191e/7f0c14a (re-begin per association + idle 10 s refresh).

B3 (build diag2 → a): Setup page shows "Firmware v1.19"; /ssid, /, /readmemory, /currentpreset
unchanged. Churn: 5 join cycles → HTTP back after 6 s each (Mac association time); 30/30 polls over
60 s with periodic refreshes running.

B4 refusals: status {"pending":false,"last":"none","app_crc32":"unknown"}; commit len=0 → len; len=1001
→ len; len=179482 → len; len=1000 over the preset bank → image; staged valid image + crc32=DEADBEEF →
crc; abort → ok; after abort last:"none". busy not reachable (no background job).

B5 software-reset bring-up with USB attached: 6 flash resets today (loader build, diag, diag2, a ×3),
AP up every time; HTTP within 0–1 s of the first poll once the Mac had rejoined (the Mac roams to the
home LAN while the AP restarts, so one join is still needed).

B6 OTA round trip (variants built from 7f0c14a: a = 661CCA5F, b = 57429505, 113,868 B each):
    A → B: push 46.6 s (19 POSTs of 6080 B), verify 18.8 s, commit reply 2 s, app up ≈ 2 s after the
           commit (uptime 74 s when read 76 s later), wifi_rejoin 66 s (60 s fixed wait), /version
           build "b" app_crc32 57429505, /ota/status last "ok".
    B → A: push 47.2 s, verify 18.7 s, commit reply 2 s, rejoin 67 s, build "a" app_crc32 661CCA5F,
           last "ok"; /ota/abort → ok, status last "none" app_crc32 "unknown"; /currentpreset answers.
First A → B attempt (before 7f0c14a) failed at the read-back with "Connection refused": the 10 s
listener refresh hit the back-to-back reads; now refreshed only when idle, and ota_push retries.

Bench hazard: a process holding the CDC port with DTR asserted defeats the 1200-baud touch (the
firmware resets only on 1200 baud with DTR dropped); flash.sh then reports "bootloader port did not
appear within 10 s" and nothing is erased. Close serial readers before flashing.

## Task 12: release binaries, checked from the committed files (2026-10-22 13:24-13:33)

`bin/2Wireless-1.19.ino.bin` (130276 B, crc32 27DF365F) and `bin/2Wireless-1.19.ota.bin` (113892 B, crc32 27D00D24) are the untagged `build.sh 2Wireless --profile wpm` outputs with the committed production loader (8192 B, crc32 FCF09B1B); byte-identical to the pair used for the POWERCUT Step 6 round trip. `bin/SHA256SUMS` covers both plus the shipped v1.18 image.

Flashed `bin/2Wireless-1.19.ino.bin` over USB (bossac-20261022-132451.log), then from the committed files:

- `/version`: version 1.19, loader fram-loader-1, capabilities [version, ota]; `/setup` shows "Firmware v1.19"; `/currentpreset` {"currentpreset": "1"}; `/ssid` {"ssid": "BUCHLA200E"}; `/getpresets?addr=76` returns `"data": ""` (Task 1 Step 5 / Task 6 Step 8).
- USB: enumerates as "Studio H Software" / "Studio H Software USB Midi".
- Association churn: five re-joins, `/currentpreset` answered after every one (5/5).
- 259e (0x28) capture on the release build is byte-identical to cap1, the capture taken on stock v1.18 (1069 bytes). No /setpresets issued (a raw round trip shifts a bank by design; see the Task 2 post-analysis), so this is the WireByte equivalence check without touching the module.
- OTA round trip from the committed images: release -> variant b (push 47.6 s, verify 20.7 s, commit 13:29:11, reply 2 s, `last:"ok"`, app_crc32 0A53CC0E, build b) -> `bin/2Wireless-1.19.ota.bin` (push 47.0 s, verify 20.4 s, commit 13:31:49, `last:"ok"`, app_crc32 27D00D24, no build tag). App answered at the first poll after each rejoin.

Final device state: `bin/2Wireless-1.19` running, installed by its own loader over the air; 259e bank equal to cap1.

### Release refreshed after the loader row-order fix (0db4b6e), 2026-10-22 14:20-14:28

The final review (I1) changed the loader, so the full image was rebuilt: `bin/2Wireless-1.19.ino.bin` is now 130276 B, crc32 839D4C36 (loader 8192 B, crc32 BFF0ADDF, built on demand by build.sh because the loader bin was absent; the regenerated blob is byte-identical to the committed one). `bin/2Wireless-1.19.ota.bin` is unchanged (113892 B, crc32 27D00D24). SHA256SUMS rewritten.

Checked again from the committed files: flashed over USB (bossac-20261022-142036.log); `/version` 1.19 + loader, `/setup` "Firmware v1.19", `/currentpreset`, `/ssid`, `/getpresets?addr=76` empty, USB name "Studio H Software USB Midi" (after the USB flash `app_crc32` still named the previous OTA image, 0A53CC0E: the documented caveat). OTA release -> variant b (commit 14:23:29, `last:"ok"`, 0A53CC0E, build b) -> `bin/2Wireless-1.19.ota.bin` (commit 14:25:57, `last:"ok"`, 27D00D24, no build tag). The churn and 259e-capture checks were not repeated: the app image is byte-identical to the one checked at 13:28. The slow-loader re-bench of the fix (round trip, brick drill, cuts at ~5 s and ~10 s) is in POWERCUT.md.
