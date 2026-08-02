#pragma once
//
// SpiBus — arbitration for the shared peripheral SPI bus.
//
// On ESP32-DIV the SD card, PN532 (RFID), CC1101 (SubGHz) and NRF24 (2.4GHz)
// all share ONE SPI peripheral (the global `SPI` object). Individual modules
// reconfigure that bus with SPI.begin(...) for their own wiring and then run
// transactions. With the UI on one core and background scan / wardriving /
// pcap tasks on the other core, two owners can touch the bus at the same time
// and corrupt each other — a classic source of random hangs and resets.
//
// This module provides a single recursive mutex that serializes access to the
// shared bus. Take it around any "reconfigure + use" sequence. It is recursive,
// so nesting (a guarded helper called from an already-guarded caller) is safe.
//
// NOTE: the display (TFT_eSPI) and the touch controller (touchscreenSPI) live
// on their own separate buses and are intentionally NOT managed here.
//
#include <Arduino.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Create the mutex. Call once from setup() before any task or SPI use.
void spiBusInit();

// Serialize access to the shared SPI bus. Recursive: safe to nest within a
// single task. Returns true when the lock was acquired.
bool spiBusTake(uint32_t timeoutMs = 0xFFFFFFFFUL);  // default: block until free
void spiBusGive();

// RAII guard: acquires on construction, releases at end of scope (including on
// early return). Check `.ok` if a finite timeout was requested.
struct SpiBusLock {
  bool ok;
  explicit SpiBusLock(uint32_t timeoutMs = 0xFFFFFFFFUL) { ok = spiBusTake(timeoutMs); }
  ~SpiBusLock() { if (ok) spiBusGive(); }
  SpiBusLock(const SpiBusLock&) = delete;
  SpiBusLock& operator=(const SpiBusLock&) = delete;
};
