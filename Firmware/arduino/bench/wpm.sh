#!/bin/bash
# Reproducible WPM HTTP calls, bound to the Wi-Fi interface (the home LAN also
# uses 192.168.0.1).   wpm.sh get <path>   |   wpm.sh post <path> <body>
IFACE="${WPM_IFACE:-en1}"
BASE="${WPM_BASE:-http://192.168.0.1}"
case "${1:-}" in
  get)  [ $# -eq 2 ] || { echo "usage: $0 get <path>" >&2; exit 2; }
        curl -s --interface "$IFACE" "$BASE/${2#/}" ;;
  post) [ $# -eq 3 ] || { echo "usage: $0 post <path> <body>" >&2; exit 2; }
        curl -s --interface "$IFACE" -X POST \
          -H 'Content-Type: application/x-www-form-urlencoded' --data-binary "$3" "$BASE/${2#/}" ;;
  *)    echo "usage: $0 get <path> | post <path> <body>" >&2; exit 2 ;;
esac
