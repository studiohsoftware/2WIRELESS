# PR 1: bus scan and status (firmware 1.20)

Stacks on PR 0 (#5: buildable tree, `/version`, server fix, FRAM-staged OTA). This description lists only
PR 1's own changes.

## Summary

- Four new routes: `GET /bus/scan`, `GET /bus/scan/status`, `GET /bus/status`, `GET /bus/polldone`.
- A scan runs from `loop()`: one QUERY (0x1A) per address in `first..last` (default `08..77`), a listen
  window per address (default 300 ms) and a 150 ms gap. A module that answers is listed in `found`.
- `FW_VERSION` is "1.20"; `/version` `capabilities` is `["version","ota","bus"]`.
- The scan logic is a pure C++ state machine, `libraries/WpmCommon/src/wpm_bus_scan.{h,cpp}`, with desktop
  tests (`make -C Firmware/arduino/test`); the routes and the bus hooks are in
  `2Wireless/src/wpm_bus.{h,cpp}`, wired into `2Wireless.ino` and the I2C receive path.
- Release files: `Firmware/arduino/2Wireless/bin/2Wireless-1.20.ino.bin` (full image, 134532 B, crc32
  52BA34BE, sha256 `b3c277d8b2a515722572306c4093e3a42a8a0d907f071ff0fec2b46bdf124635`) and
  `2Wireless-1.20.ota.bin` (app only, 118148 B, crc32 5CD2BF86, sha256
  `be7191ede48dff6d819c78caea4477b36ab4a6537f5d715b21cdef1f26564bbb`), both listed in `bin/SHA256SUMS`
  next to the 1.19 pair. A unit already on 1.19 updates over Wi-Fi with
  `/ota/commit?len=118148&crc32=5CD2BF86` after staging the `.ota.bin`; a stock v1.18 unit needs one USB
  upload of a full image first (see PR 0).

## Routes

All replies are HTTP 200 with a JSON body. Refusals are `{"ok":false,"error":"<code>"}` with `<code>` one of
`busy`, `args`, `unsupported`, `send_timeout`.

### `GET /bus/scan?first=HH&last=HH&window=MS`

Starts a scan. Success: `{"ok":true}`.

- `first`, `last`: exactly two hex digits, either case, each in `08..77`; defaults `08` and `77`. Anything
  else (wrong length, non-hex, out of range such as `00`) gives `args`. `first > last` is accepted: the scan
  ends `done` at once with no entries.
- `window`: optional decimal milliseconds, the per-address listen window. Default 300; clamped to
  `100..2000`. Empty, longer than 6 characters or non-digit gives `args`.
- `unsupported`: the V2 pin (`V2VERSION_PIN`, D2) is set, i.e. the bus is V2; V2 scans are not implemented. A `v2/` request prefix does not change this.
- `busy`: a scan is already running.
- `busy` and `args` also set `last_error` (1 and 2).

### `GET /bus/scan/status`

Sent chunked (as `/getpresets` is). Shape:

    {"state":"idle|scanning|done","idx":3,"count":1,"found":[{"addr":"28","ver_valid":false,"version":"       ","ver_src":"none"}]}

- `idx`: number of addresses completed (queried and past their gap) in this scan; `count`: number of entries in `found`
  (`found` is in address order, at most 112).
- `addr`: two lower-case hex digits. `version`: exactly 7 characters; backslash and quote are escaped, bytes
  below 0x20 or 0x7F and above become `.`.

`ver_valid` / `ver_src`:

- A module answers QUERY with a 5-byte identity ACK `04 22 <addr> 1C FF` (byte 1 may also be `00`). That
  ACK is what puts a module in `found`. It carries no version.
- A version comes only from a 0x13 version frame from that address seen inside its window
  (`"ver_src":"frame"`, `ver_valid:true`), or, failing that, from the stock display cache at FRAM
  0x02C0FE (`[addr][7 chars]`, one slot) when it names that address (`"ver_src":"cache"`,
  `ver_valid:true`). The cache is read when the status is served, so it can be older than the scan.
- Otherwise `ver_valid:false`, `version` is seven spaces and `ver_src` is `"none"`.
- Clients should treat `cache` as possibly stale and `frame` as fresh. 0x13 frames follow a front-panel hold
  on the module, not a QUERY.

### `GET /bus/status`

    {"wpm_present":true,"bus_quiet":true,"dialect":0,"last_error":0}

- `wpm_present`: always true (the WPM is the card).
- `bus_quiet`: true when no slave frame of more than 4 bytes arrived in the last 50 ms (or none ever).
- `dialect`: 0 until a slave frame has been seen since boot, then 2 if the V2 pin is set, else 1 (v1, long framing). It reads the pin, not the framing last seen, and a `v2/` prefix does not change it.
- `bus_quiet`: true when no slave frame and no module read (`requestEvent`) has been seen for 50 ms.
- `last_error`: 0 none, 1 busy, 2 bad args, 4 send timeout. It persists until the next accepted
  `/bus/scan` clears it, so it can show an earlier refusal.

### `GET /bus/polldone`

Sends the POLL_DONE frame on the bus. Success `{"ok":true}` after the frame went out;
`{"ok":false,"error":"send_timeout"}` (and `last_error` 4) if the master send fails; `busy` during a scan.

### Mutual exclusion

Read guard: `/bus/scan` and `/bus/polldone` also answer `busy` (and set `last_error` 1) within 1 s of the last module read
from the WPM (after `/setpresets` or a card read), and a running sweep defers its next QUERY until reads have been idle
for 50 ms (the reply window and the gap are wall-clock and keep running, so a sweep stretches only by the idle wait).
Benched: `/setpresets?addr=28` followed at once by `/bus/scan` gave `busy` with `bus_quiet:false`; the same scan 3 s
later found the module, and a recapture afterwards equalled the stock reference. Known limitation: a module read that
starts during the ~1 ms the slave is offline while a QUERY goes out can still be NAKed; that race was not benched.

While a scan is running, `/bus/scan`, `/ota/commit`, `/getpresets?addr=...`, `/setpresets?addr=...` and
`/bus/polldone` answer `{"ok":false,"error":"busy"}`. Stock clients never see this state, because only the
new routes start a scan. All other routes (`/currentpreset`, `/version`, `/bus/status`, `/ota/status`, ...)
keep answering during a scan.

### Dispatch order

Dispatch in `2Wireless.ino` is first-match `indexOf` over the request line: `/bus/scan/status` is tested
before `/bus/scan`, and `/bus/status` and `/bus/polldone` are distinct from both.

## Compatibility contract

- Nothing stock changes: existing routes keep URL, parameter order, reply shape and default behaviour, and
  the stock pages work unchanged (the Setup page shows "Firmware v1.20").
- FRAM is untouched: PR 1 stores nothing in FRAM. It only reads the stock display cache at 0x02C0FE.
- New behaviour only through new routes. The only edits to existing paths are a busy refusal on
  `/getpresets` and `/setpresets` that can be reached only while a scan runs, and the receive-path hooks
  that feed the scan.
- USB identity, AP name and the I2C protocol are unchanged.

## What was bench-tested

Desktop tests pass. The device bench ran on Oren's WPM on 2026-10-22 (259e at 0x28, 251e at 0x5c, six other
modules); the table is the "PR 1" section of
[`Firmware/arduino/bench/RESULTS.md`](../../Firmware/arduino/bench/RESULTS.md). The final build (04a0d61)
was flashed over USB. In order:

- `/version` reported 1.20 with `capabilities` `["version","ota","bus"]`; `/bus/status` before any frame
  gave `dialect` 0 and `dialect` 1 after the first scan.
- The first build (fcc0e73) found nothing: the modules answered QUERY with the 5-byte `1C` identity ACK, not a
  0x13 frame, and the display cache did not change. That was fixed in efd9117 (found is driven by the ACK).
- Single-address scans of 0x28 and 0x5c found the module with `ver_valid:false`, `ver_src:"none"` in 2.1 to
  2.3 s; upper-case `5C` was accepted; `window=2000` took 4.1 s instead of 2.1 s.
- Full sweep `08..77`: 51.2 s (efd9117) and 52.4 s (04a0d61); 8 modules found both times (0x0e, 0x11, 0x20,
  0x23, 0x28, 0x3c, 0x5c, 0x5e). 0x20 returned `FBA30.0` with `ver_src:"cache"`.
- During a sweep: `/bus/scan`, `/ota/commit`, `/bus/polldone` and `/getpresets?addr=28` all answered `busy`,
  and `/currentpreset` answered 20/20 polls at 1 s on both builds.
- Arguments: `first=00&last=05` gave `args`; `first=30&last=28` gave `ok` then `done`, `count:0`.
- The chunked status reply on the wire was `71` CRLF body CRLF `0` CRLF CRLF.
- `/bus/polldone` on an idle bus gave `{"ok":true}`, and a `/getpresets?addr=28` capture afterwards was
  identical to the earlier one (1069 bytes).
- Stock routes `/ssid`, `/currentpreset`, `/setup` and `/getpresets?addr=76` (empty) behaved as before.

## Known limitations

- V2 bus scans answer `unsupported` and were not tested (no V2 unit).
- `send_timeout` was not exercised (it needs a bus fault).
- The chunked status writer was verified with 8 entries only.
- The display-cache overlay is read on the main loop and can race the ISR's FRAM write for one poll; at
  worst one stale or garbled `cache` string, harmless.
- A reply that arrives after the window but before the loop notices the timeout is accepted; the overshoot
  is bounded by main-loop latency.
- An address that answers with only the identity ACK costs the full window plus gap, like a silent one (the
  window is waited out so a later 0x13 frame can still fill the entry), so a scan is deterministic: about
  450 ms per address at the default window.
- `bus_quiet` is not updated during a `/getpresets` capture (frames are cached to FRAM and do not reach the hook).
- `last_error` persists until the next accepted scan.

## How to reproduce

    cd Firmware/arduino
    make -C test                                       # desktop tests
    bash build.sh 2Wireless --profile wpm              # build/2Wireless/2Wireless.ino.bin and .ota.bin
    ./flash.sh build/2Wireless/2Wireless.ino.bin       # USB, full image
    bash bench/wifi_rejoin.sh                          # after the reset: join and wait for HTTP
    python3 bench/bus_scan.py [--first HH --last HH --window MS]

Other routes, with the Mac joined to the WPM AP:

    bench/wpm.sh get version
    bench/wpm.sh get bus/status
    bench/wpm.sh get 'bus/scan?first=28&last=28&window=500'
    bench/wpm.sh get bus/scan/status
    bench/wpm.sh get bus/polldone

OTA from 1.19: stage with `python3 bench/ota_push.py 2Wireless/bin/2Wireless-1.20.ota.bin`, then
`bench/wpm.sh get 'ota/commit?len=118148&crc32=5CD2BF86'` and `bash bench/wifi_rejoin.sh`.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01TT8DVnxoAbrr5uYq3D89kJ
