// First-stage FRAM loader for the 2WIRELESS WPM. Built for the wpm_loader
// profile (no USB), embedded in the app's .sketch_boot section at flash
// 0x2000..0x5FFF; the application proper starts at 0x6000.
//
// Boot: read the 18-byte OTA record from FRAM. If an update is pending, verify
// the staged image CRC, copy it row by row to flash from 0x6000 (row 0, the vectors and the
// marker, erased first and written last), verify the flash CRC, record the result and reset.
// Otherwise jump to the application, or, if the application head is not plausible, blink the
// LED at 5 Hz for 3 s and enter SAM-BA. A copy that never verified therefore always ends in
// the blink + hand-off, never in a jump into a half-written image.
// record.flags is the retry counter: the application writes flags=0 whenever it
// commits a new pending record; the loader increments it after each failed
// flash verify and gives up (last_result=2, magic cleared) at WPM_LOADER_MAX_TRIES.
// An app image carries a 16-byte marker at offset 0x1C (build.sh stamps it: "WPMAPP1\0",
// link base 0x6000, image length). Before any erase the loader checks the staged len and the
// FRAM head (vectors plus marker, marker length == record len); a failure clears the magic
// with last_result=3 (image). last_result: 0 none, 1 ok, 2 crc_mismatch (staged CRC bad or
// flash never verified), 3 image (len or head refused).
// Beyond the core's init() (which sets every pin to INPUT, as the app's own init() does), only
// pins 4 (FRAM CS), 8/9/10 (SPI) and LED_BUILTIN are driven.
#include <Arduino.h>
#include <SPI.h>
#include <wpm_crc32.h>
#include <wpm_loader_logic.h>
#include <wpm_ota_record.h>

#include "fram.h"
#include "nvm.h"

#define WPM_LOADER_MAX_TRIES 3

// Bench only (build.sh FramLoader --slow <ms>): pause after each 256-byte row so a
// power cut can be timed mid-copy. Production builds leave this 0 and compile it out.
#ifndef LOADER_SLOW_COPY_MS
#define LOADER_SLOW_COPY_MS 0
#endif

static void write_record(const WpmOtaRecord& r) {
  uint8_t raw[WPM_OTA_RECORD_LEN];
  wpm_ota_record_serialize(&r, raw);
  fram_write(WPM_OTA_RECORD_ADDR, raw, WPM_OTA_RECORD_LEN);
}

// Clear the pending magic and report a failed stage (last_result 2 or 3).
static void reject(WpmOtaRecord& r, uint8_t last_result) {
  r.pending = false;
  r.flags = 0;
  r.last_result = last_result;
  write_record(r);
  fram_end();
  NVIC_SystemReset();
}

// Success: write fields 4..17 first (magic still intact), then zero the magic
// in a second write, so a power cut never leaves a cleared magic with stale fields
// nor a half-written record that looks complete.
static void commit_success(WpmOtaRecord& r) {
  uint8_t raw[WPM_OTA_RECORD_LEN];
  r.flags = 0;
  r.last_result = 1;
  r.pending = true;  // serialize with the magic, write all but the magic first
  wpm_ota_record_serialize(&r, raw);
  fram_write(WPM_OTA_RECORD_ADDR + 4, raw + 4, WPM_OTA_RECORD_LEN - 4);
  const uint8_t zero[4] = {0, 0, 0, 0};
  fram_write(WPM_OTA_RECORD_ADDR, zero, 4);
}

static bool flash_app_ok() {
  uint8_t v[WPM_OTA_HEAD_LEN];
  memcpy(v, (const void*)WPM_APP_FLASH_BASE, WPM_OTA_HEAD_LEN);
  return wpm_ota_check_head(v, 0) == WPM_OTA_OK;
}

[[noreturn]] static void jump_to_app() {
  fram_end();  // SPI off, CS back to input: leave the pins as the app expects
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(LED_BUILTIN, INPUT);
  __disable_irq();
  SysTick->CTRL = 0;
  SCB->VTOR = WPM_APP_FLASH_BASE;
  __set_MSP(*(volatile uint32_t*)WPM_APP_FLASH_BASE);
  // The app's startup never re-enables interrupts (PRIMASK stays as we leave it),
  // so clear all pending state first, then enable, then branch.
  NVIC->ICER[0] = 0xFFFFFFFF;
  NVIC->ICPR[0] = 0xFFFFFFFF;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  __enable_irq();
  ((void (*)(void))(*(volatile uint32_t*)(WPM_APP_FLASH_BASE + 4)))();
  for (;;) {
  }
}

// No plausible application: blink at 5 Hz for 3 s so the fault is visible, then hand the
// unit to the SAM-BA bootloader through its own double-tap path (the magic word survives a
// system reset; the bootloader sees it on a non-POR reset, clears it and stays resident with
// its USB port), so a USB upload recovers the unit without reaching the reset button. The
// core's 1200-baud touch instead erases the row at 0x2000, which here would destroy this
// loader. On a power-on reset the bootloader clears the magic, so each power-up repeats
// blink + hand-off.
#define BOOT_DOUBLE_TAP_ADDRESS (0x20007FFCul)
#define DOUBLE_TAP_MAGIC 0x07738135
[[noreturn]] static void blink_then_bootloader() {
  fram_end();
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 15; i++) {  // 5 Hz: 100 ms on, 100 ms off, 3 s
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
  __disable_irq();
  *(volatile uint32_t*)BOOT_DOUBLE_TAP_ADDRESS = DOUBLE_TAP_MAGIC;
  NVIC_SystemReset();
  for (;;) {
  }
}

// CRC32 of FRAM [0, len) in 256-byte chunks.
static uint32_t fram_image_crc(uint32_t len) {
  uint8_t buf[NVM_ROW_SIZE];
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t off = 0; off < len; off += NVM_ROW_SIZE) {
    uint32_t n = len - off < NVM_ROW_SIZE ? len - off : NVM_ROW_SIZE;
    fram_read(off, buf, n);
    crc = wpm_crc32_update(crc, buf, n);
  }
  return crc ^ 0xFFFFFFFFu;
}

// Erase and write one 256-byte row of the image (the last row is padded with 0xFF).
static void copy_row(uint32_t off, uint32_t len) {
  uint8_t buf[NVM_ROW_SIZE];
  uint32_t n = len - off < NVM_ROW_SIZE ? len - off : NVM_ROW_SIZE;
  fram_read(off, buf, n);
  for (uint32_t i = n; i < NVM_ROW_SIZE; i++) buf[i] = 0xFF;
  nvm_erase_row(WPM_APP_FLASH_BASE + off);
  for (uint32_t p = 0; p < NVM_ROW_SIZE; p += NVM_PAGE_SIZE) {
    nvm_write_page(WPM_APP_FLASH_BASE + off + p, buf + p);
  }
  if (LOADER_SLOW_COPY_MS) delay(LOADER_SLOW_COPY_MS);
}

// Row 0 holds the vectors and the marker: erase it first and write it last, so flash never
// looks like a plausible app while the copy is incomplete. If the magic is lost at any point
// in between (retries exhausted, record or FRAM misread after a power cut), the next boot
// blinks and hands off to SAM-BA instead of jumping into a half-written image.
static void copy_image(WpmOtaRecord& r) {
  nvm_erase_row(WPM_APP_FLASH_BASE);
  for (uint32_t off = NVM_ROW_SIZE; off < r.len; off += NVM_ROW_SIZE) copy_row(off, r.len);
  copy_row(0, r.len);
}

static void do_copy(WpmOtaRecord& r) {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  if (r.flags >= WPM_LOADER_MAX_TRIES) reject(r, 2);
  if (wpm_ota_check_len(r.len) != WPM_OTA_OK) reject(r, 3);
  uint8_t head[WPM_OTA_HEAD_LEN];
  fram_read(0, head, WPM_OTA_HEAD_LEN);
  if (wpm_ota_check_head(head, r.len) != WPM_OTA_OK) reject(r, 3);
  if (fram_image_crc(r.len) != r.crc32) reject(r, 2);

  NVMCTRL->CTRLB.bit.CACHEDIS = 1;  // verify reads must see the fresh flash
  copy_image(r);

  uint32_t crc = wpm_crc32((const uint8_t*)WPM_APP_FLASH_BASE, r.len);
  if (crc == r.crc32) {
    r.app_crc32 = crc;
    commit_success(r);
  } else {
    // Magic stays so the copy re-runs on the next boot, at most MAX_TRIES times. Row 0 is
    // erased again first: an unverified image must never be jumped into, even if the magic
    // is cleared later (retries exhausted) or misread.
    nvm_erase_row(WPM_APP_FLASH_BASE);
    r.flags++;
    write_record(r);
  }
  fram_end();
  NVIC_SystemReset();
}

void setup() {
  fram_begin();
  uint8_t raw[WPM_OTA_RECORD_LEN];
  fram_read(WPM_OTA_RECORD_ADDR, raw, WPM_OTA_RECORD_LEN);
  WpmOtaRecord r;
  wpm_ota_record_parse(raw, &r);

  switch (wpm_loader_decide(r.pending, flash_app_ok())) {
    case WPM_LOADER_COPY: do_copy(r); break;
    case WPM_LOADER_JUMP: jump_to_app(); break;
    default: blink_then_bootloader(); break;
  }
}

void loop() {}
