#!/usr/bin/env python3
"""Run a WPM byte-counting capture (firmware 1.21+, capability "dump") and read the bank back.

Usage: dump.py --addr HH [--len N] [--first-wait MS] [--silence MS] [--out FILE] [--compare FILE]
               [--polldone] [--remoteenable] [--base http://192.168.0.1] [--iface en1] [--timeout S] [--status-only]

Starts GET /dump?addr=..[&len=..][&first_wait_ms=..][&silence_ms=..], polls /dump/status every
0.5 s until "done" or "failed", prints the final status, then reads `extent` bytes from FRAM 0
with /readmemory (128-byte elements, 8 KB per request) and writes them to --out; --compare FILE
compares them byte for byte with a flat-bank file (a Xenomorpher GET_DUMP bank or
bench/ref/259e-cap1-flat.bin). Module gates: --polldone sends /bus/polldone first (the 272e only
writes the card after a bus POLL_DONE since its power-up); --remoteenable sends the stock
/remoteenable first (the 251e's BACKUP handler is gated on the remote-enable flag, bus 0x16,
which stock's poll-on-start also sends; without it the 251e answers with an immediate completion
frame and no data). POLL_DONE is harmless to repeat; REMOTE_EN is a general-call frame that arms
remote control on EVERY module on the bus, including ones the owner switched off at the panel, so
send it only when the owner wants that (the firmware never sends it on its own).
Every socket is bound to the Wi-Fi interface IPv4 because the home LAN also uses 192.168.0.1.
Exit codes: 0 done (and match, if --compare), 1 refused/failed/mismatch/network error/timeout.
"""
import argparse
import http.client
import json
import re
import socket
import subprocess
import sys
import time


def die(msg, code=1):
    print("error: " + msg, file=sys.stderr)
    sys.exit(code)


def iface_ip(iface):
    try:
        out = subprocess.run(["ipconfig", "getifaddr", iface], capture_output=True,
                             text=True, timeout=5).stdout.strip()
    except (OSError, subprocess.SubprocessError) as e:
        die("cannot run ipconfig for %s: %s" % (iface, e))
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", out):
        die("interface %s has no IPv4 address (not joined to the WPM Wi-Fi?)" % iface)
    return out


def install_bound_connect(ip):
    real = socket.create_connection

    def bound(address, timeout=socket._GLOBAL_DEFAULT_TIMEOUT, source_address=None):
        return real(address, timeout, source_address=(ip, 0))

    socket.create_connection = bound


def get(host, port, path, timeout=30, tries=4):
    for attempt in range(tries):
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        try:
            conn.request("GET", path)
            resp = conn.getresponse()
            return resp.status, resp.read().decode("latin-1")
        except (ConnectionRefusedError, ConnectionResetError):
            if attempt == tries - 1:
                raise
            time.sleep(0.5)
        finally:
            conn.close()


def parse_json(text, what):
    try:
        return json.loads(text)
    except ValueError:
        die("%s is not JSON: %r" % (what, text[:200]))


def read_fram(host, port, length):
    out = b""
    off = 0
    while off < length:
        n = min(8192, length - off)
        _, body = get(host, port, "/readmemory?addr=0x%x&length=%d" % (off, n))
        try:
            elems = json.loads(body)
        except ValueError:
            elems = [{"data": m} for m in re.findall(r'"data":\s*"([0-9a-fA-F]*)"', body)]
        chunk = b"".join(bytes.fromhex(e["data"]) for e in elems)
        if len(chunk) != n:
            die("/readmemory returned %d of %d bytes at 0x%x" % (len(chunk), n, off))
        out += chunk
        off += n
    return out


def fmt_status(st):
    return ("state=%s addr=%s expected=%s received=%s extent=%s lines=%s dropped=%s "
            "complete_frame=%s early_done=%s error=%s elapsed_ms=%s addr_log=%r" % (
                st.get("state"), st.get("addr"), st.get("expected"), st.get("received"),
                st.get("extent"), st.get("lines"), st.get("dropped"), st.get("complete_frame"),
                st.get("early_done"), st.get("error"), st.get("elapsed_ms"), st.get("addr_log")))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--addr", help="module address, 2 hex digits")
    ap.add_argument("--len", type=int, default=None, help="expected data bytes (omit = silence mode)")
    ap.add_argument("--first-wait", type=int, default=None, help="first_wait_ms (default 5000)")
    ap.add_argument("--silence", type=int, default=None, help="silence_ms (default 1000)")
    ap.add_argument("--out", default=None, help="write the captured flat bank here")
    ap.add_argument("--compare", default=None, help="flat-bank file to compare with")
    ap.add_argument("--polldone", action="store_true", help="send /bus/polldone first (272e gate)")
    ap.add_argument("--remoteenable", action="store_true",
                    help="send the stock /remoteenable first (251e gate: its BACKUP needs remote-enable)")
    ap.add_argument("--base", default="http://192.168.0.1")
    ap.add_argument("--iface", default="en1")
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--status-only", action="store_true")
    a = ap.parse_args()

    m = re.fullmatch(r"http://([^/:]+)(?::(\d+))?/?", a.base)
    if not m:
        die("--base must look like http://host[:port]")
    host, port = m.group(1), int(m.group(2) or 80)
    install_bound_connect(iface_ip(a.iface))

    if a.status_only:
        _, body = get(host, port, "/dump/status")
        print(fmt_status(parse_json(body, "/dump/status")))
        return 0
    if not a.addr:
        die("--addr is required")

    if a.polldone:
        _, body = get(host, port, "/bus/polldone")
        r = parse_json(body, "/bus/polldone")
        if not r.get("ok"):
            die("/bus/polldone refused: %s" % r.get("error"))
        print("polldone sent")
        time.sleep(0.5)

    if a.remoteenable:
        get(host, port, "/remoteenable")   # stock route; bus 0x16 REMOTE_EN, no JSON reply
        print("remoteenable sent")
        time.sleep(0.5)

    path = "/dump?addr=%s" % a.addr
    if a.len is not None:
        path += "&len=%d" % a.len
    if a.first_wait is not None:
        path += "&first_wait_ms=%d" % a.first_wait
    if a.silence is not None:
        path += "&silence_ms=%d" % a.silence
    t0 = time.time()
    _, body = get(host, port, path)
    r = parse_json(body, "/dump")
    if not r.get("ok"):
        die("/dump refused: %s" % r.get("error"))
    print("dump started: %s" % path)

    while True:
        time.sleep(0.5)
        _, body = get(host, port, "/dump/status")
        st = parse_json(body, "/dump/status")
        if st.get("state") in ("done", "failed"):
            break
        if time.time() - t0 > a.timeout:
            die("dump did not finish within %.0f s: %s" % (a.timeout, fmt_status(st)))
    print(fmt_status(st))
    print("wall time %.1f s" % (time.time() - t0))
    if st.get("state") != "done":
        die("dump failed (error %s)" % st.get("error"))

    extent = int(st.get("extent") or 0)
    data = read_fram(host, port, extent) if extent else b""
    print("read back %d bytes from FRAM 0" % len(data))
    if a.out:
        open(a.out, "wb").write(data)
        print("written to %s" % a.out)
    if a.compare:
        ref = open(a.compare, "rb").read()
        if data == ref:
            print("MATCH: identical to %s (%d bytes)" % (a.compare, len(ref)))
        else:
            n = next((k for k in range(min(len(data), len(ref))) if data[k] != ref[k]),
                     min(len(data), len(ref)))
            die("MISMATCH vs %s: lengths %d/%d, first difference at offset 0x%x" % (
                a.compare, len(data), len(ref), n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
