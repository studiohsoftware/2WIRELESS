# PR 2: byte-counting capture (firmware 1.21)

Stacks on PR 1 (#6: bus scan and status) and PR 0 (#5: buildable tree, `/version`, server fix, FRAM-staged
OTA). This description lists only PR 2's own changes.

## Summary

- Three new routes: `GET /dump`, `GET /dump/status`, `GET /dump/abort`.
- A job masters a BACKUP to one module, then counts the bytes the module writes to the card (I2C 0x50) as
  `[offset_hi][offset_lo][data...]` lines, applying each at its offset in the FRAM scratch area. It knows
  when the bank is complete (completion frame, expected length reached, or silence) instead of guessing with
  the stock 1 s idle timer. The result is a flat bank from FRAM 0, read back with `/readmemory`.
- `FW_VERSION` is "1.21"; `/version` `capabilities` is `["version","ota","bus","dump"]`.
- The job logic is a pure C++ state machine, `libraries/WpmCommon/src/wpm_dump.{h,cpp}`, with desktop tests
  (`make -C Firmware/arduino/test`); the routes and the I2C receive hook are in
  `2Wireless/src/wpm_dump_dev.{h,cpp}`, wired into `2Wireless.ino`.
- Release files: `Firmware/arduino/2Wireless/bin/2Wireless-1.21.ino.bin` (full image, 138316 B, crc32
  01282445, sha256 `668da24ed850771c535b4f4776130d60195235a13264dbcdacd26f0df3aab1ae`) and
  `2Wireless-1.21.ota.bin` (app only, 121932 B, crc32 88C9BB14, sha256
  `52f045369e7904f6db156c74f086443c3f2f1bf930b6916c1acd78b80b197e77`), both listed in `bin/SHA256SUMS`. A unit
  already on 1.20 updates over Wi-Fi with `/ota/commit?len=121932&crc32=88C9BB14` after staging the
  `.ota.bin`.

## Routes

All replies are HTTP 200 with a JSON body. Refusals are `{"ok":false,"error":"<code>"}`.

### `GET /dump?addr=HH[&len=N][&first_wait_ms=M][&silence_ms=S]`

Starts a job. Success `{"ok":true}`; refusal `{"ok":false,"error":"busy"}` or `{"ok":false,"error":"args"}`.

- `addr`: required, exactly two hex digits, either case, in `08..77`.
- `len`: expected byte count, decimal; default 0 = silence mode (the end is the completion frame or silence).
  At most `0x2BD18` (the FRAM scratch area).
- `first_wait_ms`: how long to wait for the first data byte; default 5000, clamped to `200..60000`.
- `silence_ms`: silence after the last byte that ends the job; default 1000, clamped to `200..20000`.
- `args`: a missing `addr`, an unknown key, an empty or non-decimal value, `addr` out of range, or `len`
  above `0x2BD18`.
- `busy`: a bus scan or another dump is running, or a module read happened within the last 1 s.

### `GET /dump/status`

    {"state":"transferring","addr":"28","expected":990,"received":528,"extent":528,"lines":8,"dropped":0,"complete_frame":false,"early_done":0,"error":0,"elapsed_ms":1234,"addr_log":"50 50 50"}

Keys, in this order:

- `state`: `idle|sending|waiting|transferring|done|failed`.
- `addr`: the module, two lower-case hex digits. `expected`: the `len` given.
- `received`: data bytes counted (written to FRAM); `extent`: the highest `offset + n` seen; `lines`: data
  lines counted; `dropped`: bytes beyond the scratch area, counted but not written.
- `complete_frame`: a completion frame ended the job. `early_done`: completion frames from the module that
  arrived before any data.
- `error`: 0 none, 4 send failed, 5 no first byte. `elapsed_ms`: since start, frozen when the job ends.
- `addr_log`: diagnostic. The matched I2C addresses (hex, space separated) of the first 8 transactions the
  job's receive hook saw, `""` when none. Clients must not rely on it.

Counters stay readable after the job ends until the next start.

### `GET /dump/abort`

Replies `{"ok":true}` and returns the job to `idle` (counters are kept for the status until the next start).

### End rules

- A completion frame from the module after data has arrived: `done`.
- `received >= len` (when `len` is not 0): `done`.
- No new byte for `silence_ms` after data has started: `done`.
- No first byte within `first_wait_ms`: `failed`, error 5.
- The BACKUP could not be sent (master send failed, or refused because module reads kept the bus busy for
  the whole `first_wait_ms`): `failed`, error 4.
- A completion frame before any data is counted in `early_done` and does not end the job: a 251e
  acknowledges the BACKUP with a completion frame seconds before it streams, and a gated module answers with
  one and nothing else (see the module gates).

### What a data line is

The module writes its bank to the card, I2C address 0x50, as `[offset_hi][offset_lo][data...]`. Each line is
written to FRAM at its offset, so the scratch area holds a flat bank from FRAM 0 with the same layout (benched on the 272e: same bank size and record grid, only edited records differ)
as a Xenomorpher GET_DUMP of the same module. If `extent != received` the bank has holes (lines missing or
overlapping); the bytes between are whatever the scratch area held before.

### Destination filter

The receive hook uses the matched I2C address (read from the SERCOM at address match). Data is accepted only
from address 0x50 and the completion frame only from general call (0x00), and only if it is the 5-byte
`04 .. <addr> 0A ..` frame. A 5-byte general-call frame that is not a completion (MIDI clock, a 0x13 display
frame) is handed to the stock decoder, which was refactored into a shared function with identical behaviour,
so those keep reaching USB and the display cache. Any other general-call frame is left unread for the stock
receive path.

### Busy matrix

While a dump runs (`sending`, `waiting`, `transferring`):

- Answer `{"ok":false,"error":"busy"}`: `/bus/scan`, `/bus/polldone`, `/getpresets?addr=...`,
  `/setpresets?addr=...`, `/ota/commit`, another `/dump`, `/remoteenable`, `/remotedisable`, `/savepreset`,
  `/recallpreset`, `/sendmidibytes`, and the seven direct MIDI routes (`/midinoteon`, `/midinoteoff`,
  `/midiclockstart`, `/midiclockstop`, `/midiclock`, `/midifinetune`, `/midibend`).
- Every stock master send is refused inside `masterBeginTransmission` while a bus job (scan or dump) runs;
  only the job's own sends get through. USB-MIDI sends and poll-on-start sends are dropped during a job.
- `/readmemory` and `/writememory` stay available, but the ISR writes FRAM concurrently, so a read returns an
  in-progress (or stale) bank and a write into the capture area is overwritten by the lines (the bytes are not corrupted): the client must wait for `state` `done` before reading the bank.
- The same busy answers and sender refusals also apply during a bus scan, not only a dump (USB MIDI is dropped
  for the whole sweep), and for a 50 ms cool-down after the last transaction addressed to the card
  (0x50): a finished or aborted dump's module may still be streaming lines or sending its completion.
  `/bus/scan` and `/bus/polldone` additionally keep the 1 s module-read guard.
- FRAM atomicity: while a dump runs the I2C ISR writes FRAM. Every FRAM primitive in the sketch
  (`framRead`, `framWrite`, `framWriteHexString`) is a critical section in thread mode (PRIMASK saved and
  restored; a no-op inside the ISR), so `/currentpreset`, `/version`, `/bus/status` and the settings routes
  are safe during a dump. The I2C slave stretches SCL while the ISR is masked (longest span about 150 us: one FRAM burst of at most 128 bytes; `/writememory` hex is decoded before the lock is taken).
  `/readmemory` of the capture area during a dump still returns an in-progress bank: read it after `done`.
- All other routes (`/currentpreset`, `/version`, `/bus/status`, `/dump/status`, ...) keep answering.

### Module gates

The firmware never arms remote control or sends POLL_DONE on its own; the client decides. The 272e writes the card only after a bus POLL_DONE since its power-up (`/bus/polldone`, harmless to repeat). The 251e's BACKUP handler is gated on its remote-enable flag (bus 0x16, the stock `/remoteenable` route, also sent by stock poll-on-start); without it the module answers with an immediate completion frame and no data (`early_done:1`, `received:0`). 0x16 is a general-call frame that arms remote control on every module on the bus, including ones the owner switched off at the panel, so a client should send it only when the owner wants that.

A module answers a BACKUP with its 0x0A completion frame even when its gate is closed (251e: immediately, 0
bytes). That is why a completion frame before any data does not end the job; with a closed gate the job fails
with error 5 after `first_wait_ms` and `early_done:1`.

### `/version`

`{"version":"1.21", ... "capabilities":["version","ota","bus","dump"]}`.

## Compatibility contract

- Nothing stock changes when no job runs: existing routes keep URL, parameter order, reply shape and default
  behaviour. `/getpresets` is unchanged (the stock capture on 1.21 was benched identical to the earlier one).
- FRAM: a dump uses only the scratch area `0..0x2BD18` (the same area `/getpresets` and OTA staging use);
  settings and the OTA record are not touched.
- New behaviour only through the new routes. The edits to existing paths are busy refusals that can be reached
  only while a bus job runs, the master-send guard (a no-op when no job runs), and the receive-path hook, which
  acts only while a dump runs. The stock `receiveEvent` decode of >4-byte general-call frames was moved into
  a shared function with identical behaviour.
- USB identity, AP name and the I2C protocol are unchanged.

## What was bench-tested

Desktop tests pass. The device bench ran on Oren's WPM on 2026-10-22 (259e at 0x28, 272e at 0x5E, 251e at
0x5C, plus 0x0E, 0x11, 0x20, 0x23, 0x3C); the tables are the "PR 2" section of
[`Firmware/arduino/bench/RESULTS.md`](../../Firmware/arduino/bench/RESULTS.md), all through `bench/dump.py`.
The final build (fcf7c0e) was flashed over USB.

- `/version` reported 1.21 with `capabilities` `["version","ota","bus","dump"]`.
- 259e `len=990`: 990 B in 37 lines, done in 147 ms, FRAM 0..989 identical to the stock-derived bank
  (`bench/ref/259e-cap1-flat.bin`). Silence mode finished on the completion frame (144 ms on an earlier
  build), same bytes.
- 272e `len=750` after `/bus/polldone`: 750 B in 35 lines, 134 ms, a repeat identical. Against the
  2026-10-17 Xenomorpher bank the layout is identical; only the records edited since differ.
- 251e `len=63120` after the stock `/remoteenable` (`first_wait_ms=20000`, `silence_ms=3000`): 63,120 B in
  1013 lines, 8.07 s, `complete_frame:true`, a repeat identical. The stock `/getpresets` could never capture
  this module.
- Without the remote enable the 251e gave a completion frame and no data: `early_done:1`, then `failed` error 5
  after the first wait.
- An absent module (`addr=76`, `first_wait_ms=3000`): `failed`, error 5, 3021 ms.
- Busy matrix during a waiting dump (0x76, 20 s first wait): `/bus/scan`, `/bus/polldone`,
  `/getpresets?addr`, `/setpresets?addr`, `/ota/commit`, a second `/dump`, `/remoteenable`,
  `/savepreset?preset=1`, `/midiclock` and `/midinoteon` all answered `busy`; `/readmemory` answered;
  `/dump/abort` gave `{"ok":true}` and state `idle`.
- Stock capture unchanged: `/getpresets?addr=28` afterwards was identical to the earlier capture, and a
  single-address `/bus/scan` found 0x28.
- The matched address at the SERCOM was confirmed on the bench: `addr_log` showed 50 on data lines and the
  completion frame was recognised at 0x00.

## Known limitations

- A MIDI clock or 0x13 frame arriving mid-dump was not exercised (no 252e clock source on the bench); the
  pass-through to the stock decoder is verified by reading the code only.
- `send_timeout` (error 4) was not exercised.
- The V2 bus was not tested (no V2 unit).
- `/readmemory` during a dump returns an in-progress (or stale) bank until `state` is `done`; the bytes themselves are not corrupted. Wait for `done`.
- Because every FRAM byte access now masks interrupts for the SPI burst, `/readmemory` is about 35% slower than
  on 1.20 (the read-back of a 122 KB OTA stage took 32 s instead of 20 s); `/writememory` throughput is unchanged
  (45 s for the same stage). Measured on the bench, not optimised.
- USB MIDI arriving during a job is dropped.
- The 251e needs the remote enable (see the module gates); the 272e needs POLL_DONE.
- A 5-byte general-call frame that is not a completion is forwarded to the stock decoder; a general-call
  frame of any other length during a dump still goes to the stock path unchanged.

## How to reproduce

    cd Firmware/arduino
    make -C test                                       # desktop tests
    bash build.sh 2Wireless --profile wpm              # build/2Wireless/2Wireless.ino.bin and .ota.bin
    ./flash.sh build/2Wireless/2Wireless.ino.bin       # USB, full image
    bash bench/wifi_rejoin.sh                          # after the reset: join and wait for HTTP

With the Mac joined to the WPM AP, from the repository root:

    python3 Firmware/arduino/bench/dump.py --addr 28 --len 990 --compare Firmware/arduino/bench/ref/259e-cap1-flat.bin
    python3 Firmware/arduino/bench/dump.py --addr 5e --len 750 --polldone
    python3 Firmware/arduino/bench/dump.py --addr 5c --len 63120 --first-wait 20000 --silence 3000 --remoteenable

OTA from 1.20: stage with `python3 bench/ota_push.py 2Wireless/bin/2Wireless-1.21.ota.bin`, then
`bench/wpm.sh get 'ota/commit?len=121932&crc32=88C9BB14'` and `bash bench/wifi_rejoin.sh`.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01TT8DVnxoAbrr5uYq3D89kJ
