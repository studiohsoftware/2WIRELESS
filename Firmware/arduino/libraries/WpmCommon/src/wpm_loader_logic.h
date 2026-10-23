#pragma once
// First-stage loader decision, kept free of hardware so it runs on the desktop.

enum WpmLoaderAction {
  WPM_LOADER_JUMP,   // start the application at WPM_APP_FLASH_BASE
  WPM_LOADER_COPY,   // verify and copy the staged FRAM image to flash
  WPM_LOADER_BLINK   // nothing valid to run: blink the LED, then hand off to SAM-BA
};

// pending: the FRAM record carries the "OTA1" magic.
// flash_app_ok: wpm_ota_check_head(head, 0) accepted the 44-byte head at the app base.
inline WpmLoaderAction wpm_loader_decide(bool pending, bool flash_app_ok) {
  if (pending) return WPM_LOADER_COPY;
  return flash_app_ok ? WPM_LOADER_JUMP : WPM_LOADER_BLINK;
}
