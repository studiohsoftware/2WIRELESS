#pragma once
// SAMD21 NVMCTRL row erase / page write. Addresses are byte addresses in flash.
// The caller must never pass an address below 0x6000 (the loader never erases
// itself); nvm_* enforce that as a last line of defence.
#include <Arduino.h>

#define NVM_PAGE_SIZE 64
#define NVM_ROW_SIZE 256
#define NVM_MIN_ADDR 0x6000u

static inline void nvm_wait_ready() {
  while (!NVMCTRL->INTFLAG.bit.READY) {
  }
}

static inline void nvm_cmd(uint32_t byte_addr, uint16_t cmd) {
  NVMCTRL->CTRLB.bit.MANW = 1;  // manual page write: only WP commits the buffer
  nvm_wait_ready();
  NVMCTRL->STATUS.reg |= NVMCTRL_STATUS_MASK;  // clear sticky error flags
  NVMCTRL->ADDR.reg = byte_addr / 2;           // 16-bit word address
  NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | cmd;
  nvm_wait_ready();
}

static inline void nvm_erase_row(uint32_t addr) {
  if (addr < NVM_MIN_ADDR) return;
  nvm_cmd(addr & ~(uint32_t)(NVM_ROW_SIZE - 1), NVMCTRL_CTRLA_CMD_ER);
}

// src: 64 bytes (any alignment). addr: 64-byte aligned, row already erased.
static inline void nvm_write_page(uint32_t addr, const uint8_t src[NVM_PAGE_SIZE]) {
  if (addr < NVM_MIN_ADDR) return;
  addr &= ~(uint32_t)(NVM_PAGE_SIZE - 1);
  NVMCTRL->CTRLB.bit.MANW = 1;
  nvm_wait_ready();
  NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
  nvm_wait_ready();
  volatile uint32_t* dst = (volatile uint32_t*)addr;
  for (int i = 0; i < NVM_PAGE_SIZE / 4; i++) {
    uint32_t w = (uint32_t)src[4 * i] | ((uint32_t)src[4 * i + 1] << 8) |
                 ((uint32_t)src[4 * i + 2] << 16) | ((uint32_t)src[4 * i + 3] << 24);
    dst[i] = w;
  }
  nvm_cmd(addr, NVMCTRL_CTRLA_CMD_WP);
}
