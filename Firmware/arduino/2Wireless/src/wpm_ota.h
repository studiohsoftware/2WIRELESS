#pragma once
// /ota/status, /ota/abort and /ota/commit routes. The staged image lives in FRAM
// at 0; the pending record sits at WPM_OTA_RECORD_ADDR and is consumed by the
// first-stage loader.
#include <Arduino.h>
#include <WiFi101.h>

void wpm_ota_handle_status(WiFiClient& client);
void wpm_ota_handle_abort(WiFiClient& client);
// urlString: "...GET /ota/commit?len=N&crc32=HEX ...".
void wpm_ota_handle_commit(WiFiClient& client, const String& urlString);

// True while a bus job (scan) runs: bus jobs and /ota/commit are mutually exclusive. A scan never
// touches FRAM staging.
bool wpm_ota_busy();
