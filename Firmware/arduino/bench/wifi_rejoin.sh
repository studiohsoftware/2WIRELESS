#!/bin/bash
# Wait for the WPM to reboot, rejoin its AP and wait until HTTP answers.
# Usage: wifi_rejoin.sh [ssid] [password]   (defaults BUCHLA200E / BUCHLA200E)
# Protocol: sleep 60 s, one networksetup join, then poll /currentpreset up to 90 s.
# Exit 0 when the JSON key appears, 1 otherwise. Prints elapsed seconds.
SSID="${1:-BUCHLA200E}"
PASS="${2:-BUCHLA200E}"
IFACE=en1
START=$SECONDS
echo "waiting 60 s for the WPM to restart..."
sleep 60
networksetup -setairportnetwork "$IFACE" "$SSID" "$PASS"
POLL_START=$SECONDS
while [ $((SECONDS - POLL_START)) -lt 90 ]; do
  if curl -s -m 2 --interface "$IFACE" http://192.168.0.1/currentpreset | grep -q '"currentpreset"'; then
    echo "WPM answering after $((SECONDS - START)) s"
    exit 0
  fi
  sleep 1
done
echo "WPM did not answer within $((SECONDS - START)) s" >&2
exit 1
