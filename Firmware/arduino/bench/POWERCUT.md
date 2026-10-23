# OTA robustness bench: power cuts, corruption, recovery (Task 10)

Run with the owner at the case. Everything below uses `W=Firmware/arduino/bench/wpm.sh` (curl bound to en1; `$W get <path>`, `$W post <path> <body>`);
set it once: `W=Firmware/arduino/bench/wpm.sh`.

## Facts that govern the procedure

- The WPM is bus-powered from the 200e case. A USB data cable back-feeds the MKR1000, so a case power
  cycle only resets it with **USB unplugged**. Every bossac flash ends in a software reset.
- After any reset the Mac must join BUCHLA200E once: `bash Firmware/arduino/bench/wifi_rejoin.sh`
  (60 s wait, join, polls up to 90 s). "Up" below means this script exited 0.
- LED during a loader run: solid on from the moment the loader starts the copy until it jumps to the
  app (about 1 s after reset plus the copy). A 5 Hz blink for 3 s followed by LED off means no valid
  app: the loader hands the unit to the SAM-BA bootloader (USB port "Arduino MKR1000" appears if USB is
  attached; each power-up repeats blink + hand-off).
  Slow loader copy time = 445 rows x 50 ms, about 22 s. The CRC passes and the record writes are
  milliseconds each.
- OTA record: 18 bytes at FRAM 0x02BE50 (`"OTA1"`, len LE32, crc32 LE32, flags, last_result, app_crc32 LE32).
- Variants: `A=Firmware/arduino/build/variants/a/2Wireless.ota.bin` (build tag `a`),
  `B=.../b/2Wireless.ota.bin` (tag `b`). Roundtrip timings: push 47 s, verify 19 s, commit reply 2 s.

## Step 0: set up (no owner needed)

```
bash Firmware/arduino/build.sh FramLoader --slow 50      # prints the bench-artefact warning
bash Firmware/arduino/build.sh 2Wireless --profile wpm --tag a   # prints "embedded loader: 8104 bytes crc32 431B705E"
# USB plugged in, no serial monitor open:
bash Firmware/arduino/flash.sh Firmware/arduino/build/2Wireless/2Wireless.ino.bin
bash Firmware/arduino/bench/wifi_rejoin.sh; $W get /version        # build "a"
git checkout Firmware/arduino/2Wireless/src/wpm_loader_blob.cpp    # never commit the slow blob
```

Then unplug the USB data cable. Check with `$W get /ota/status` that it answers
(`{"pending":false,"last":...,"app_crc32":...}`).

Helper used throughout (print the commit URL; ota_push never commits):

```
stage() { python3 Firmware/arduino/bench/ota_push.py "$1" --iface en1; }   # last line: commit URL
```

## Step 1: mid-copy power cut (3 repetitions: 5 s, 10 s, 20 s)

Per repetition, with T the variant not currently running (rep 1: A running, stage B):

1. `stage <T.ota.bin>`; `curl --interface en1 '<commit URL>'`. The reply (`ok`) is "t=0".
2. Owner starts a stopwatch at the reply. LED goes solid within about 1-2 s. At the cut time (5 / 10 /
   20 s) owner switches the case off for 5 s, then back on.
3. Owner watches the LED after power-on: expect solid again, for the full ~22 s (copy restarts from
   row 0), then off (app up). Note the duration.
4. `wifi_rejoin.sh`, then `$W get /ota/status` and `$W get /version`.

Expected: `last:"ok"`, `pending:false`, `/version` build = T's tag, `app_crc32` = T's CRC (B 57429505,
A 661CCA5F per RESULTS.md B6). Record anything else (blinking = the failure we are looking for).

## Step 2: cut after the copy, before the record is cleared

The post-copy window (flash CRC pass, record write, magic clear) is milliseconds, so it cannot be hit
by eye. Repeat Step 1 with cut times 21, 22 and 23 s (the last row finishes at about 22 s from loader
start, ~23-24 s after the reply), 3 attempts, and report the LED duration of the post-cut run:
about 22 s means the record was still pending (re-copy); a short LED / immediate app means the magic
had already been cleared. Expected either way: app T runs, `last:"ok"`, no blink.

## Step 3: corrupt staged image

```
stage <B.ota.bin>                       # note len N and crc C (original) from the commit URL
# flip one byte at FRAM 0x1000 (first byte of the 64-byte element):
D=$($W get '/readmemory?addr=0x1000&length=64' | python3 -c 'import sys,json,re;t=sys.stdin.read();d=re.findall(r"\"data\":\s*\"([0-9a-fA-F]+)\"",t)[0][:128];print("%02x"%(int(d[:2],16)^0xff)+d[2:])')
$W post /writememory "[{\"addr\":\"1000\",\"data\":\"$D\"}]"
curl --interface en1 "http://192.168.0.1/ota/commit?len=N&crc32=C"     # commit with the ORIGINAL CRC
```

Expected: `"error":"crc"` (commit-time catch), `/ota/status` pending:false. Then bypass commit by writing the
record by hand with the original (now wrong for the corrupted FRAM) CRC:

```
python3 - N C <<'PY'     # N decimal, C hex; prints the 18-byte record as hex
import struct,sys
n,c=int(sys.argv[1]),int(sys.argv[2],16)
print((b"OTA1"+struct.pack("<II",n,c)+bytes(2)+bytes(4)).hex())
PY
$W post /writememory '[{"addr":"2be50","data":"<hex printed above>"}]'
```

`$W get /ota/status` should now show `pending:true`. Owner power-cycles (5 s off, USB out), LED
solid for under 1 s (reject before copy), then app up; `wifi_rejoin.sh`.
Expected: `last:"crc_mismatch"`, `pending:false`, `/version` build = the previously running tag
(old app still runs), flash not touched. Then `curl --interface en1 http://192.168.0.1/ota/abort`.

## Step 4: oversize, broken head and stock head, after a real reboot

Power-cycle first (owner, 5 s off, USB out), `wifi_rejoin.sh`, then
`curl --interface en1 'http://192.168.0.1/ota/commit?len=L&crc32=00000000'` for each:
`len=0`, `len=1001`, `len=179482` (expected `"error":"len"`); `len=1000` with FRAM 0 holding the preset
bank (nothing staged) (expected `"error":"image"`); stage a valid image and commit with its own `len`
(the marker length; any other even len is refused as `image`) and `crc32=DEADBEEF`
(expected `"error":"crc"`); `/ota/abort` (expected `ok`). After each, `/ota/status` must show
`pending:false`; the app keeps running (build tag unchanged), no reboot.

Two more checks for the app marker (the stock image has no marker at 0x1C):

(a) Stock head at commit. `ota_push.py` refuses the stock bin
(`error: not a WPM app image: no valid marker at 0x1C (stage only a build.sh 2Wireless.ota.bin; a stock or full .ino.bin is refused)`),
so write only the stock image's first 64 bytes (hex from
`python3 -c "print(open('Firmware/arduino/2Wireless/bin/2Wireless.ino.bin','rb').read()[:64].hex())"`):

```
$W post /writememory '[{"addr":"0","data":"0080002005cb0000edca0000edca000000000000000000000000000000000000000000000000000000000000edca00000000000000000000edca000059cb0000"}]'
curl --interface en1 'http://192.168.0.1/ota/commit?len=108612&crc32=00000000'
```
Expected `"error":"image"` (the head check runs before the CRC pass; 108612 is a valid len). No reboot.

(b) Loader-side guard. Hand-write a record that names the stock head (`OTA1`, len 108612 = 0x1A844 LE
= `44a80100`, crc 0, flags 0, last 0, app_crc 0):

```
$W post /writememory '[{"addr":"2be50","data":"4f54413144a8010000000000000000000000"}]'
```
`$W get /ota/status` shows `pending:true`. Owner power-cycles (USB out). Expected: LED solid under 1 s
(reject before any erase), app up, `last:"image"`, `pending:false`, `/version` build tag unchanged. Then
`curl --interface en1 http://192.168.0.1/ota/abort`.

## Step 5: recovery drill (brick and reflash)

The zero-image route is now refused before the copy (no marker), so brick the unit over USB instead.
Craft `Firmware/arduino/build/brick.ino.bin` = the variant A full image with bytes 0x4000..0x40FF
(the app's vector table) zeroed:

```
python3 -c "
d=bytearray(open('Firmware/arduino/build/variants/a/2Wireless.ino.bin','rb').read())
d[0x4000:0x4100]=bytes(0x100)
open('Firmware/arduino/build/brick.ino.bin','wb').write(d)"
```
USB plugged, no serial reader: `bash Firmware/arduino/flash.sh Firmware/arduino/build/brick.ino.bin`.
Expected: after bossac's reset the loader finds no plausible app (SP word 0 fails) and the LED
**blinks at 5 Hz for 3 s**, then the loader hands off to SAM-BA: the "Arduino MKR1000" bootloader
port (0x2341) stays enumerated (`ls /dev/cu.usbmodem*`) and HTTP never answers. Owner's only job:
confirm the 3 s blink.
Recover: `bash Firmware/arduino/flash.sh Firmware/arduino/build/variants/a/2Wireless.ino.bin`
(flash.sh finds the 0x2341 port, no touch), then `wifi_rejoin.sh`; `/version` build "a". If USB was
unplugged during the hand-off, plug it in and power-cycle once (POR clears the magic; the loader
blinks 3 s and hands off again).

## Results (controller fills in)

| Step | Rep | Cut time (s after commit reply) | LED observed | last / app_crc32 / build | Pass/fail |
|------|-----|---------------------------------|--------------|--------------------------|-----------|
| 1 | 1 | unknown (owner cut "randomly", not synced to the LED) | not observed | ok / 0A53CC0E / b, reset_cause POR (12:28 commit of B) | PASS |
| 1 | 2 | owner counted ~12 s from his "go", but the commit reply was 12:32:43 and the result says the copy was already complete: a re-copy ends in the loader's own reset (SYST) and this boot reports POR | not observed (LED not located) | ok / 0A47FCE2 / a, POR, after power-on (the owner left the case off for 5 min first; no harm). Counted as a post-commit cut, not mid-copy | PASS |
| 1 | 3 | ~18 s after "go" (commit reply 12:51:17): mid-copy | solid ~20 s after power-on, then off (full re-copy) | ok / 0A53CC0E / b, reset_cause SYST (the loader's own reset after the copy) | PASS |
| 2 | 1 | ~24 s after "go" (commit reply 12:55:00): late in the copy | owner saw no LED after power-on; the device says it re-copied (SYST), so the LED was most likely missed | ok / 0A47FCE2 / a, reset_cause SYST (re-copied after power-on) | PASS |
| 2 | 2 | ~28 s after "go" (commit reply 13:00:01): after the copy and record | LED never lit after power-on (direct jump) | ok / 0A53CC0E / b, reset_cause POR | PASS |
| 2 | 3 | ~26 s after "go" (commit reply 13:04:05): after the copy and record | LED never lit after power-on (direct jump) | ok / 0A47FCE2 / a, reset_cause POR | PASS |
| 3 | commit | n/a | app stayed up | byte at 0x1000 flipped, commit with the original CRC -> `{"ok":false,"error":"crc"}`; byte restored, read-back matches; status none / unknown / a (2026-10-22 12:10) | PASS |
| 3 | hand record | power cycle | on for under 1 s (reject before copy) | B staged, byte at 0x1000 flipped (38->C7), record OTA1/113900/0A53CC0E written by hand, pending:true; after the cycle: crc_mismatch / unknown / a, reset_cause SYST (13:12) | PASS |
| 4 | len=0 / 1001 / 179482 | n/a | app stayed up | all three `{"ok":false,"error":"len"}` (2026-10-22 12:05; after the 09:20 bossac reset, not a power cycle) | PASS |
| 4 | len=1000 image / DEADBEEF crc | n/a | app stayed up | len=1000 over the staged B -> `image` (marker length mismatch); len=113900 crc32=DEADBEEF -> `crc`; abort -> ok; status none / unknown / a | PASS |
| 4 | stock head commit | n/a | app stayed up | `{"ok":false,"error":"image"}`; status none / unknown / a (2026-10-22 09:20, before Step 1, no reboot) | PASS |
| 4 | stock record power cycle | power cycle | none seen (reject within ms) | stock v1.18 head at FRAM 0, record OTA1/108612/0 written by hand, pending:true; after the cycle: image / unknown / a, reset_cause SYST (13:18); abort -> none | PASS |
| 5 | brick (USB) | n/a | not watched (owner away); hand-off seen from the Mac | brick.ino.bin (bench-slow-a, app row 0 zeroed) flashed 12:16:11; from 12:16:17 the unit enumerated as "Arduino MKR1000" 0x2341:0x004E and stayed; HTTP never came up | PASS |
| 5 | recovery | n/a | app up | flash.sh bench-slow-a found the bootloader port (no touch), verify OK; app answered on build a after a second join (wifi_rejoin timed out once at 162 s); status none; staged B marker intact | PASS |

## Step 6 result: production loader round trip (2026-10-22 13:14-13:22)

Production build (untagged, loader 8192 B crc32 FCF09B1B, image 130276 B crc32 27DF365F, ota 113892 B crc32 27D00D24) flashed over USB; `/version` shows no build tag. OTA to variant b: commit 13:17:27, app up at the first poll after rejoin, `last:"ok"`, app_crc32 0A53CC0E, build b. OTA back to the production image: commit 13:20:01, `last:"ok"`, app_crc32 27D00D24, no build tag. Copy time at full speed is about 1 s (445 rows). PASS.

## Re-bench after the row-order fix (loader writes row 0 last; 0db4b6e), 2026-10-22 13:52-14:00

Bench image: tag a on the slow loader built from 0db4b6e (loader 8200 B crc32 CCF6C4CD, image crc32 AAB15993; the app images are unchanged, variants a/b 0A47FCE2 / 0A53CC0E).

| Check | Result |
|-------|--------|
| OTA a -> b (commit 13:55:07) | `last:"ok"`, app_crc32 0A53CC0E, build b, app up at the first poll after rejoin. PASS |
| USB brick drill (brick2.ino.bin, app row 0 zeroed, flashed 13:56:54) | "Arduino MKR1000" 0x2341:0x004E resident from 13:56:59 and stayed; recovery upload found the bootloader port, verify OK, app up on build a. PASS |
| Early mid-copy cuts (~5 s and ~10 s into the copy) | see the rows below (owner) |
| Early cut 1: owner cut ~3 s after reading "accepted" (commit reply 14:13:41), i.e. ~5 s into the copy | LED solid ~20 s after power-on (full re-copy), then off; `last:"ok"`, app_crc32 0A47FCE2, build a, reset_cause SYST. PASS |
| Early cut 2: owner cut ~8 s after reading "accepted" (commit reply 14:17:07), i.e. ~10 s into the copy | LED solid ~20 s after power-on (full re-copy), then off; `last:"ok"`, app_crc32 0A53CC0E, build b, reset_cause SYST. PASS |

Mid-copy coverage after the fix: cuts at ~5 s and ~10 s (this section) plus the earlier ~18 s and ~24 s cuts all re-copied from row 0 on the next boot; the ~26 s and ~28 s cuts found the record already committed.
