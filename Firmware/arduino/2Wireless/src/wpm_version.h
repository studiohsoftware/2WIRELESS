#pragma once
// /version capability route support: firmware version string and the JSON body.
#include <Arduino.h>
#include <stdint.h>

#define FW_VERSION "1.20"

// True when a plausible first-stage loader vector table sits at flash 0x2000.
bool wpm_loader_present();
// Reset cause from PM->RCAUSE: "POR", "BOD", "EXT", "WDT", "SYST" or "UNKNOWN".
const char* wpm_reset_cause_str();
// Builds the /version JSON body.
String wpm_version_json(bool loaderPresent, uint8_t usbMode, uint8_t pollOnStart);
