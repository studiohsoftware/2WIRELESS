#pragma once
// Minimal MB85RS2MTA FRAM access for the loader. Touches only pins 4 (CS),
// 8/9/10 (SPI, via the core's SPI object). Never touches the WINC1500 pins.
#include <Arduino.h>
#include <SPI.h>

#define FRAM_CS_PIN 4
#define FRAM_OP_WREN 0x06
#define FRAM_OP_WRITE 0x02
#define FRAM_OP_READ 0x03

static inline void fram_begin() {
  pinMode(FRAM_CS_PIN, OUTPUT);
  digitalWrite(FRAM_CS_PIN, HIGH);
  SPI.begin();
}

static inline void fram_end() {
  SPI.end();
  pinMode(FRAM_CS_PIN, INPUT);
}

static inline void fram_addr(uint32_t addr) {
  SPI.transfer((uint8_t)(addr >> 16));
  SPI.transfer((uint8_t)(addr >> 8));
  SPI.transfer((uint8_t)addr);
}

static inline void fram_read(uint32_t addr, uint8_t* dst, size_t n) {
  SPI.beginTransaction(SPISettings(12000000, MSBFIRST, SPI_MODE0));
  digitalWrite(FRAM_CS_PIN, LOW);
  SPI.transfer(FRAM_OP_READ);
  fram_addr(addr);
  for (size_t i = 0; i < n; i++) dst[i] = SPI.transfer(0);
  digitalWrite(FRAM_CS_PIN, HIGH);
  SPI.endTransaction();
}

static inline void fram_write(uint32_t addr, const uint8_t* src, size_t n) {
  SPI.beginTransaction(SPISettings(12000000, MSBFIRST, SPI_MODE0));
  digitalWrite(FRAM_CS_PIN, LOW);
  SPI.transfer(FRAM_OP_WREN);
  digitalWrite(FRAM_CS_PIN, HIGH);
  digitalWrite(FRAM_CS_PIN, LOW);
  SPI.transfer(FRAM_OP_WRITE);
  fram_addr(addr);
  for (size_t i = 0; i < n; i++) SPI.transfer(src[i]);
  digitalWrite(FRAM_CS_PIN, HIGH);
  SPI.endTransaction();
}
