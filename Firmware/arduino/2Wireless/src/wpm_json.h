#pragma once
// Small helpers for JSON routes.
#include <Arduino.h>
#include <WiFi101.h>

// Defined in 2Wireless.ino.
void writeHeader(WiFiClient client, String statusString, String contentType, int contentLength);

inline String wpm_json_err(const char* code) {
  return String("{\"ok\":false,\"error\":\"") + code + "\"}";
}

inline void wpm_send_json(WiFiClient& c, const String& body) {
  writeHeader(c, "HTTP/1.1 200 OK", "Content-type:application/json", body.length());
  c.println(body);
}
