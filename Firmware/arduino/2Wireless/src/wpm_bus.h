#pragma once
// /bus/scan, /bus/scan/status, /bus/status and /bus/polldone routes, plus the hooks that drive
// the bus-scan state machine (libraries/WpmCommon/src/wpm_bus_scan.h) from loop() and from the
// I2C receive ISR.
#include <Arduino.h>
#include <WiFi101.h>

void wpm_bus_loop();  // call once per loop() pass (main loop only)
// ISR: collects a 0x13 version frame (7 version bytes) for the scan mailbox.
void wpm_bus_on_version_frame(uint8_t addr, const uint8_t ver[7]);
// ISR: call for every >4-byte slave frame; feeds /bus/status bus_quiet and dialect.
void wpm_bus_note_frame();
// ISR: the module's 0x1C identity ACK (04 22|00 <addr> 1C FF), the real reply to QUERY.
void wpm_bus_on_query_reply(uint8_t addr);
bool wpm_bus_scan_active();  // scan state == SCANNING
// A bus job (scan or dump) owns the bus: masterBeginTransmission() refuses every other sender. The
// job loops (main loop only) bracket their own sends with begin/end so those get through.
void wpm_job_master_begin();
void wpm_job_master_end();
bool wpm_job_master_allowed();
bool wpm_bus_read_recent(uint32_t ms);
// A module wrote to the card address (0x50) within the last ms milliseconds (set in receiveEvent).
bool wpm_card_write_recent(uint32_t ms);
// The bus belongs to a job: a scan or dump runs, or a module wrote to the card within the 50 ms
// cool-down (a finished or aborted dump's module may still be streaming lines or sending its
// completion). Every stock master send is refused while this holds, except a job's own sends.
bool wpm_bus_job_busy();  // scan or dump running, or a card-addressed write within the last 50 ms

// urlString: "...GET /bus/scan?first=HH&last=HH&window=MS ...".
void wpm_bus_handle_scan(WiFiClient& client, const String& urlString);
void wpm_bus_handle_scan_status(WiFiClient& client);
void wpm_bus_handle_status(WiFiClient& client);
void wpm_bus_handle_polldone(WiFiClient& client);
