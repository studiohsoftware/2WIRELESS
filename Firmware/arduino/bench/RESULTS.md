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

# PR 1: bus scan and status (firmware 1.20), 2026-10-22 16:50-17:35

Builds: fcc0e73 (first 1.20), efd9117 (QUERY reply = 0x1C ACK), 04a0d61 (final: chunked status, busy error, V2 latch; image 134548 B crc32 3CFA9168, ota 118164 B crc32 6BE0802E, loader unchanged BFF0ADDF). All flashed over USB; AP up 65-68 s after reset each time. Bus: 259e at 0x28, 251e at 0x5c plus six other modules.

| Check | Result |
|---|---|
| `/version` | 1.20, `capabilities` `["version","ota","bus"]`, loader present. PASS |
| `/bus/status` before any frame | `{"wpm_present":true,"bus_quiet":true,"dialect":0,"last_error":0}`; `dialect` 1 after the first scan. PASS |
| First single-address scans (fcc0e73) | 0x28 and 0x5c: `done`, count 0, while `dialect` flipped to 1 and the 0x13 display cache (FRAM 0x02C0FE) stayed unchanged: the reply arrived but was not a 0x13 frame. Root cause: a module answers QUERY with the 5-byte identity ACK `04 22 <addr> 1C FF` (Xenomorphicle `try_query_reply`, 200e-conformance.md), which stock `receiveEvent` discards. FAIL, fixed in efd9117 |
| Single-address scans (efd9117, 04a0d61) | `bus_scan.py --first 28 --last 28`: found 0x28, `ver_valid:false`, `ver_src:"none"`, 2.1-2.3 s; same for 0x5c; upper-case `5C` accepted. PASS |
| `window=2000` | one-address scan 4.1 s instead of 2.1 s. PASS |
| Full sweep 08..77 | 51.2 s (efd9117) and 52.4 s (04a0d61), 8 found both times: 0x0e, 0x11, 0x20 (`FBA30.0`, `ver_valid:true`, `ver_src:"cache"` from the stock display cache), 0x23, 0x28, 0x3c, 0x5c, 0x5e. PASS |
| Concurrency during a sweep | `/bus/scan` -> busy; `/ota/commit?len=2&crc32=0` -> busy; `/bus/polldone` -> busy; `/getpresets?addr=28` -> busy; `/currentpreset` answered 20/20 polls at 1 s (both builds). PASS |
| Arguments | `first=00&last=05` -> `args`; `first=30&last=28` -> ok, status `done`, `count:0`, `idx:0`. PASS |
| Chunked `/bus/scan/status` (04a0d61) | raw reply: `71` CRLF `{...one entry...}` CRLF `0` CRLF CRLF. PASS |
| `/bus/polldone` idle | `{"ok":true}`; `/bus/status.last_error` showed 1 afterwards (the busy refusals during the earlier sweep; it persists until the next accepted scan). PASS |
| 259e after POLL_DONE | `/getpresets?addr=28` capture identical to cap1 (1069 bytes). PASS |
| Stock routes | `/ssid`, `/currentpreset`, `/setup` "Firmware v1.20", `/getpresets?addr=76` empty. PASS |

Not tested: a scan on a V2 bus (`unsupported`; no V2 unit); a `send_timeout` (needs a bus fault); more than ~26 found entries (the chunked writer is verified only with 8 entries and by reading the stock pattern it mirrors).

### Fix round 3 (304afbd): the read guard, 2026-10-22

Build 304afbd/34e3bdc (image 134532 B crc32 52BA34BE, ota 118148 B crc32 5CD2BF86) flashed. With the 259e flat bank written to FRAM (restore_259e.py --no-restore, read-back True): `/setpresets?addr=28` then an immediate `/bus/scan?first=28&last=28` -> `{"ok":false,"error":"busy"}`, `/bus/status` `bus_quiet:false`, `last_error:1`; the same scan 3 s later -> ok, status done with 0x28 found; a `/getpresets?addr=28` recapture afterwards is identical to cap1 (1069 bytes). PASS. Regression on the same build: see the rows below.

| Regression on 304afbd | full sweep 08..77: 50.8 s, the same 8 modules; `/bus/polldone` ok afterwards. PASS |

# PR 2: byte-counting capture (firmware 1.21), 2026-10-22 evening

Builds: db3a590 (first 1.21), f9bf14e (early-completion rule), 5f3394b (review fixes: general-call pass-through, master guards). Bus: 259e 0x28 (990 B bank, 37 lines), 272e 0x5E (750 B, 35 lines), 251e 0x5C (63,120 B, 1013 lines), plus 0x0E MARF, 0x11 257e, 0x20 CSR, 0x23, 0x3C. All device runs via `bench/dump.py`.

| Check | Result |
|---|---|
| `/version` | 1.21, capabilities `["version","ota","bus","dump"]`. PASS |
| 259e `/dump?addr=28&len=990` | done in 162 ms, received 990, extent 990, lines 37, dropped 0; FRAM 0..989 byte-identical to `bench/ref/259e-cap1-flat.bin` (the stock capture de-interleaved). PASS |
| 259e silence mode (`/dump?addr=28`) | done by the completion frame (`complete_frame:true`), 144 ms, same 990 bytes. PASS |
| Matched-address experiment | `addr_log` "50 50 50 …" on data lines; the general-call completion frame recognised at address 0x00: `I2CS.DATA` holds the matched address at AMATCH on the SAMD21. The destination filter stays on (`WPM_DUMP_ADDR_FILTER 1`). PASS |
| 272e `/dump?addr=5e&len=750` after `/bus/polldone` | done 143 ms, 750 B, 35 lines; repeat byte-identical; vs the Xenomorpher reference bank of 2026-10-17: 710/750 bytes equal at shift 0, only records 0-2 (25-byte slots) differ (presets edited since, confirmed by the app session). Layout guarantee met. PASS |
| 251e, first attempts (`first_wait` 20 s and 60 s, FRAM zeroed or not) | done at 26 ms with 0 bytes and a completion frame (db3a590), then, with the early-completion rule (f9bf14e), failed error 5 after the first wait with `early_done:1` and periodic card reads from the module (`addr_log` "00 50 50 …", n=0 transactions = master reads). Root cause: the 251e BACKUP handler is gated on its remote-enable flag (bus 0x16), which stock poll-on-start sends and this unit (poll-on-start off) had never received since power-up; the handler emits the 0x0A completion even when gated. Documented, not a firmware defect |
| 251e after the stock `/remoteenable` | `/dump?addr=5c&len=63120&first_wait_ms=20000&silence_ms=3000` → done, received 63120, extent 63120, lines 1013, 8.0 s; repeat byte-identical (sha256 abeed4052172bc80c167fc18367c1a695214431e569a74ffe13fb3f1a0e4ce56). The module stock could never capture. PASS |
| Absent module `/dump?addr=76&first_wait_ms=3000` | failed, error 5, elapsed 3021 ms. PASS |
| Busy matrix during a waiting dump | `/bus/scan`, `/bus/polldone`, `/getpresets?addr`, `/setpresets?addr`, `/ota/commit`, a second `/dump` → busy; `/readmemory` answers; `/dump/abort` → ok, state idle. PASS |
| Stock `/getpresets?addr=28` on 1.21 | identical to cap1. PASS |
| `/bus/scan` single address after dumps | found 0x28. PASS |

### Final build fcf7c0e (image 138052 B crc32 5E7136DA, ota 121668 B crc32 57B917BF)

| Check | Result |
|---|---|
| 259e `len=990` | done 147 ms, 990 B, 37 lines, identical to `bench/ref/259e-cap1-flat.bin`. PASS |
| 272e `len=750` after `/bus/polldone` | done 134 ms, 750 B, 35 lines, identical to the earlier 1.21 dump. PASS |
| 251e `len=63120` after `/remoteenable` (first_wait 20 s, silence 3 s) | done 8.07 s, 63120 B, 1013 lines, `complete_frame:true`, identical to the earlier capture (sha256 abeed405…). PASS |
| Busy matrix during a waiting dump (0x76, 20 s first wait) | `/bus/scan`, `/bus/polldone`, `/getpresets?addr`, `/setpresets?addr`, `/ota/commit`, a second `/dump`, `/remoteenable`, `/savepreset?preset=1`, `/midiclock`, `/midinoteon` → `{"ok":false,"error":"busy"}`; `/readmemory` answers; `/dump/abort` → ok, state idle. PASS |
| Stock `/getpresets?addr=28` afterwards | identical to cap1. PASS |
| `/bus/scan` single address afterwards | found 0x28. PASS |

Not tested: a MIDI clock or 0x13 frame arriving mid-dump (no 252e clock source on the bench; the pass-through to the stock decoder is verified by reading); `send_timeout`; USB-MIDI arriving during a dump (dropped by design); the V2 bus.

### Fix round 4 build f55067e (FRAM primitives atomic against the ISR; 50 ms card-write cool-down)

| Check | Result |
|---|---|
| `/version` | 1.21, capabilities `["version","ota","bus","dump"]`. PASS |
| 251e dump while polling `/currentpreset` and `/version` every 0.5 s | capture done 8.05 s, 63120 B, 1013 lines, identical to the earlier capture; 48/48 polls answered. PASS (the scenario of review finding I1) |
| 259e `len=990` | 145 ms, identical to the reference. PASS |
| 272e `len=750` after `/bus/polldone` | 119 ms, identical to the earlier dump. PASS |

### Final build 6a7ccce = release 1.21 (image 138316 B crc32 01282445, ota 121932 B crc32 88C9BB14)

| Check | Result |
|---|---|
| `/version` | 1.21, capabilities `["version","ota","bus","dump"]`. PASS |
| `/writememory` under the new lock | the 259e flat bank (990 B, 4 POSTs of 64-byte elements) written and read back identical. PASS |
| OTA staging and loader with this image | `ota_push.py` of the 1.21 `.ota.bin`: push 45.0 s, verify 31.7 s (read-back is slower than on 1.20, ~20 s, because every `framRead` now masks interrupts briefly), `/ota/commit?len=121932&crc32=88C9BB14` → `last:"ok"`, `app_crc32` 88C9BB14, 1.21 back up after the rejoin. PASS |
| 259e `len=990` after the OTA | 142 ms, identical to the reference. PASS |
| Stock `/getpresets?addr=28` | identical to cap1. PASS |
| Short scan 08..0f with `/remoteenable` and `/midiclock` during it (fix round 4 build) | both → busy; scan found 0x0e; `bus_quiet` true afterwards. PASS |
