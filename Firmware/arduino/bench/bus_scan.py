#!/usr/bin/env python3
"""Run a WPM bus scan (firmware 1.20+, capability "bus") and print the module table.

Usage: bus_scan.py [--first 08] [--last 77] [--window MS] [--base http://192.168.0.1]
                   [--iface en1] [--status-only] [--timeout S]

Starts GET /bus/scan?first=..&last=..[&window=..], then polls /bus/scan/status every
2 s until "done", printing one line per found module (addr, version, ver_valid) and
the elapsed time. --status-only prints the current /bus/scan/status and exits.
Every socket is bound to the Wi-Fi interface IPv4 because the home LAN also uses
192.168.0.1. Exit codes: 0 done, 1 refused (busy/args/unsupported), network error or
timeout.
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


def get(host, port, path, timeout=10, tries=4):
    """GET with a retry on a refused/reset connection (the WPM refreshes its listener)."""
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


def print_status(st):
    print("state=%s idx=%s count=%s" % (st.get("state"), st.get("idx"), st.get("count")))
    for f in st.get("found", []):
        print("  addr 0x%s  version %r  ver_valid %s  ver_src %s" % (
            f.get("addr"), f.get("version"), f.get("ver_valid"), f.get("ver_src")))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--first", default="08")
    ap.add_argument("--last", default="77")
    ap.add_argument("--window", type=int, default=None, help="reply window per address, ms")
    ap.add_argument("--base", default="http://192.168.0.1")
    ap.add_argument("--iface", default="en1")
    ap.add_argument("--status-only", action="store_true")
    ap.add_argument("--timeout", type=float, default=180.0, help="overall wait for done, s")
    a = ap.parse_args()

    m = re.fullmatch(r"http://([^/:]+)(?::(\d+))?/?", a.base)
    if not m:
        die("--base must look like http://host[:port]")
    host, port = m.group(1), int(m.group(2) or 80)
    install_bound_connect(iface_ip(a.iface))

    if a.status_only:
        _, body = get(host, port, "/bus/scan/status")
        print_status(parse_json(body, "/bus/scan/status"))
        return 0

    path = "/bus/scan?first=%s&last=%s" % (a.first, a.last)
    if a.window is not None:
        path += "&window=%d" % a.window
    t0 = time.time()
    _, body = get(host, port, path)
    reply = parse_json(body, "/bus/scan")
    if not reply.get("ok"):
        die("/bus/scan refused: %s" % reply.get("error"))
    print("scan started: %s" % path)

    while True:
        time.sleep(2.0)
        _, body = get(host, port, "/bus/scan/status")
        st = parse_json(body, "/bus/scan/status")
        if st.get("state") == "done":
            print_status(st)
            print("done in %.1f s, %d found" % (time.time() - t0, len(st.get("found", []))))
            return 0
        print("  ... %s idx=%s count=%s (%.0f s)" % (st.get("state"), st.get("idx"),
                                                     st.get("count"), time.time() - t0))
        if time.time() - t0 > a.timeout:
            die("scan did not finish within %.0f s" % a.timeout)


if __name__ == "__main__":
    sys.exit(main())
