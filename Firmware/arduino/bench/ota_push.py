#!/usr/bin/env python3
"""Stage an app-only .ota.bin into the WPM's FRAM at 0 via /writememory, verify
it with /readmemory and print the ready /ota/commit URL (never calls it).

Usage: ota_push.py <file.ota.bin> [--base http://192.168.0.1] [--iface en1]
                   [--no-verify] [--dry-run]

The image must carry the app marker build.sh stamps at 0x1C ("WPMAPP1\\0", base 0x6000,
length == file length); a stock or full .ino.bin is refused before any network (also with
--dry-run).

Device facts encoded here: /writememory takes a JSON array of
{"addr":"<hex>","data":"<hex>"}, Content-Type application/x-www-form-urlencoded,
and answers 200 before parsing (only read-back proves a write); the tokenizer
caps strings at 256 chars, so data elements are 64 bytes (128 hex chars); parts
are <= 15 KB, 500 ms apart. /readmemory answers 128-byte elements.
Every socket is bound to the Wi-Fi interface IPv4 because the home LAN also
uses 192.168.0.1. Exit codes: 0 ok, 1 usage/IO/network error, 2 verify mismatch.
"""
import argparse
import http.client
import json
import re
import socket
import subprocess
import sys
import time
import zlib

MAX_IMAGE = 0x2BD18
ELEM = 64               # bytes per JSON element (128 hex chars)
PART_MAX = 15 * 1024    # max chars of POST body (so data per part is well under 15 KB)
PART_GAP_S = 0.5
READ_CHUNK = 8192


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
    """Make every socket (http.client uses socket.create_connection) bind to ip."""
    real = socket.create_connection

    def bound(address, timeout=socket._GLOBAL_DEFAULT_TIMEOUT, source_address=None):
        return real(address, timeout, source_address=(ip, 0))

    socket.create_connection = bound


def split_parts(data):
    """Return a list of parts; each part is a list of (addr, chunk) elements."""
    parts, cur, cur_chars = [], [], 2
    for off in range(0, len(data), ELEM):
        chunk = data[off:off + ELEM]
        # '{"addr":"<hex>","data":"<hex>"},' = 28 chars + addr + data
        cost = 28 + len("%x" % off) + 2 * len(chunk)
        if cur and cur_chars + cost > PART_MAX:
            parts.append(cur)
            cur, cur_chars = [], 2
        cur.append((off, chunk))
        cur_chars += cost
    if cur:
        parts.append(cur)
    return parts


def part_body(part):
    return json.dumps([{"addr": "%x" % a, "data": c.hex()} for a, c in part],
                      separators=(",", ":"))


def request(host, port, method, path, body=None, timeout=30, tries=4):
    """One HTTP request. A refused/reset connection is retried (the WPM refreshes its
    listening socket now and then, refusing connections for a few milliseconds); any other
    error, and a reply that was already being read, are not retried. Retrying a refused or reset
    connection is safe because every /writememory element carries its absolute address; the
    retry exists for the WPM's listener refresh."""
    headers = {}
    if body is not None:
        body = body.encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    for attempt in range(tries):
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        try:
            conn.request(method, path, body=body, headers=headers)
            resp = conn.getresponse()
            return resp.status, resp.read()
        except (ConnectionRefusedError, ConnectionResetError):
            if attempt == tries - 1:
                raise
            time.sleep(0.5)
        finally:
            conn.close()


def read_back(host, port, addr, length):
    status, raw = request(host, port, "GET", "/readmemory?addr=0x%x&length=%d" % (addr, length))
    if status != 200:
        die("/readmemory HTTP %d at 0x%x" % (status, addr))
    text = raw.decode("latin-1")
    try:
        elems = json.loads(text)
    except ValueError:
        # Tolerate slightly malformed device JSON.
        elems = [{"data": m} for m in re.findall(r'"data":\s*"([0-9a-fA-F]*)"', text)]
    return b"".join(bytes.fromhex(e["data"]) for e in elems)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("file")
    ap.add_argument("--base", default="http://192.168.0.1")
    ap.add_argument("--iface", default="en1")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="build the plan, no network")
    a = ap.parse_args()

    try:
        data = open(a.file, "rb").read()
    except OSError as e:
        die(str(e))
    if not data:
        die("file is empty")
    if len(data) % 2:
        die("image length %d is odd" % len(data))
    if len(data) > MAX_IMAGE:
        die("image length %d exceeds 0x%X" % (len(data), MAX_IMAGE))
    if (len(data) < 0x2C or data[0x1C:0x24] != b"WPMAPP1\0"
            or int.from_bytes(data[0x24:0x28], "little") != 0x6000
            or int.from_bytes(data[0x28:0x2C], "little") != len(data)):
        die("not a WPM app image: no valid marker at 0x1C (stage only a build.sh "
            "2Wireless.ota.bin; a stock or full .ino.bin is refused)")
    crc = "%08X" % (zlib.crc32(data) & 0xFFFFFFFF)
    print("image %s: %d bytes crc32 %s" % (a.file, len(data), crc))

    parts = split_parts(data)
    bodies = [part_body(p) for p in parts]
    print("plan: %d parts, %d elements of %d bytes, <= %d body chars/part, %.1f s gaps" % (
        len(parts), sum(len(p) for p in parts), ELEM, PART_MAX, PART_GAP_S))
    if a.dry_run:
        for i, (p, b) in enumerate(zip(parts, bodies), 1):
            print("  part %d/%d: addr 0x%x..0x%x, %d bytes, body %d chars" % (
                i, len(parts), p[0][0], p[-1][0] + len(p[-1][1]) - 1,
                sum(len(c) for _, c in p), len(b)))
        print("commit URL (not called): %s/ota/commit?len=%d&crc32=%s" % (a.base, len(data), crc))
        return 0

    m = re.fullmatch(r"http://([^/:]+)(?::(\d+))?/?", a.base)
    if not m:
        die("--base must look like http://host[:port]")
    host, port = m.group(1), int(m.group(2) or 80)
    install_bound_connect(iface_ip(a.iface))

    t0 = time.time()
    for i, (p, b) in enumerate(zip(parts, bodies), 1):
        try:
            status, _ = request(host, port, "POST", "/writememory", b)
        except OSError as e:
            die("part %d/%d failed: %s" % (i, len(parts), e))
        if status != 200:
            die("part %d/%d: HTTP %d" % (i, len(parts), status))
        print("part %d/%d sent (%d bytes, offset 0x%x) t=%.1fs" % (
            i, len(parts), sum(len(c) for _, c in p), p[0][0], time.time() - t0), flush=True)
        if i < len(parts):
            time.sleep(PART_GAP_S)
    print("push time %.1f s" % (time.time() - t0))

    if not a.no_verify:
        t1 = time.time()
        for off in range(0, len(data), READ_CHUNK):
            want = data[off:off + READ_CHUNK]
            try:
                got = read_back(host, port, off, len(want))
            except OSError as e:
                die("read-back at 0x%x failed: %s" % (off, e))
            if got != want:
                n = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]),
                         min(len(got), len(want)))
                print("VERIFY MISMATCH: first differing offset 0x%x (got %d of %d bytes in chunk)" % (
                    off + n, len(got), len(want)), file=sys.stderr)
                return 2
            print("verified 0x%x..0x%x" % (off, off + len(want) - 1), flush=True)
        print("verify ok (%.1f s)" % (time.time() - t1))
    else:
        print("verify skipped (--no-verify)")

    print("ready: %s/ota/commit?len=%d&crc32=%s" % (a.base, len(data), crc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
