# ESP32-DIV V2 — project context for Claude Code

This is Joe's fork of CiferTech's **ESP32-DIV** firmware (a multi-protocol RF
handheld) for the **ESP32-DIV V2 board (ESP32-S3-N16R8)**. This fork adds a
battery-percentage fix plus a NeoPixel status-LED service and a buzzer service.
On top of that, a **shared-SPI bus arbitration** patch has been applied.

## Board / target
- MCU: ESP32-S3 (16 MB flash, 8 MB OPI PSRAM). Classic-ESP32 2.x-era codebase.
- Display: TFT_eSPI, config in `User_Setup v2.h` (ILI9341 driver, HSPI). This
  must be copied to the TFT_eSPI library's `User_Setup.h`.
- Touch: XPT2046 on a separate SPI. Shared "peripheral" SPI bus (SD + PN532 +
  CC1101 + NRF24) is the global `SPI` object.

## Verified build recipe (do NOT drift the core major version)
This code only builds on **Arduino-ESP32 2.0.x** (NimBLE 1.x API, arduinoFFT v1
API, xreef PCF8574). The 3.x core will NOT compile it.

Library versions that match the code:
- NimBLE-Arduino ^1.4.1 · arduinoFFT ^1.6 · PCF8574 (xreef) 2.4.0 ·
  XPT2046_Touchscreen (PaulStoffregen git master — needs `begin(SPIClass&)`) ·
  RF24 1.6.1 · Adafruit PN532 1.3.4 (+ Adafruit BusIO) · rc-switch 2.6.4 ·
  ArduinoJson ^6 · IRremoteESP8266 · **Adafruit NeoPixel** (for the LED service) ·
  TFT_eSPI + SmartRC-CC1101 (bundled in `Libraries/`).

Two REQUIRED linker flags (both pre-existing upstream, unrelated to our patch):
`-Wl,--allow-multiple-definition`
  1. global `spi` symbol defined by both CC1101 lib and TFT_eSPI S3 file
  2. `wifi.cpp` deliberately overrides the SDK's `ieee80211_raw_frame_sanity_check`

### Arduino IDE Tools-menu settings
Board: "ESP32S3 Dev Module" · Flash Size: 16MB · PSRAM: "OPI PSRAM" ·
USB CDC On Boot: Enabled. Partition: "Huge APP 3MB" is fine if you ONLY flash
over USB; if you want the SD-card firmware updater to work, pick an OTA-capable
dual-app scheme instead — see "Deployment" below.
Put the linker flag in `boards.local.txt` or a platform build flag if the IDE
build hits the multiple-definition link error.

### PlatformIO (what was used to verify — builds green)
`platform = espressif32@6.9.0`, `board = esp32-s3-devkitc-1`,
`board_build.arduino.memory_type = qio_opi`, `board_upload.flash_size = 16MB`,
`build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
-Wl,--allow-multiple-definition`. Result: RAM ~36%, Flash ~55%.

## The SPI arbitration patch (already applied)
Problem: SD, PN532, CC1101 and NRF24 share one SPI bus; modules reconfigured it
with `SPI.begin(...)` and ran transactions with no locking, while background
tasks (wardriving pinned to core 0, pcap writer) hit the same bus from the other
core — classic corruption / random-reset source.

Fix — `SpiBus.h` / `SpiBus.cpp`: a recursive FreeRTOS mutex + `SpiBusLock` RAII
guard + `spiBusInit()` (called at top of `setup()`).
- **Phase 1** — locks at bus *reconfigure/mount* points: `sdSpiInit`,
  `sdMountChipSelect` (utils.cpp), `rfidAttachBus`/`rfidRestoreBus` (rfid.cpp),
  `subghzMountSD`, `pcapMountSD`, ducky `mountSD`, NRF24 reconfigure (bluetooth).
- **Phase 2** — locks in the *transaction bodies*: NRF24 `getRegister`/
  `setRegister` (covers all 2.4GHz), pcap SD write/flush/open/close (wifi.cpp),
  wardriving row-write bursts (gps.cpp — NOT held across the blocking scans).
- **Phase 2b (DONE)** — CC1101 (subghz.cpp) + PN532 (rfid.cpp) transaction bodies
  now guarded at the sketch call sites. CC1101: every SetTx/SetRx/setMHZ/setSidle/
  getRssi/Init op wrapped; init sequences held under one lock; jammer FIFO
  fill+strobe bursts atomic (lock released before the inter-burst delay); spectrum
  getRssi locked per-sample. PN532: shared helpers (detectCardType, tryMagicBackdoor,
  rfidTagStillPresent, rfidTargetSendResponse) locked at source; auth->read/write
  on the same block share one lock; poll loops (readPassiveTargetID/AsTarget/
  getDataTarget) lock per SPI call, never across the multi-second loop. Builds green
  (Flash ~40% of 4MB, RAM ~36%). Hardware-validated 2026-08-02 (see Reality check).

Design notes: mutex is recursive (nested locks are safe); do NOT hold the lock
across blocking radio scans; the always-on status-bar task only reads the SD
card-detect GPIO, so it never touches the bus.

## TODO / not yet done
- **NRF24 pins (DONE)** — replaced `SPI.begin(13,11,12,4)` with named `NRF24_*`
  macros in shared.h, aliased to the shared SD_* bus. Verifying against the V2
  Shield-Schematic showed the old literals were WRONG: the three nRF24L01 and the
  CC1101 share SCK=IO12/MOSI=IO11/MISO=IO13. Fixed and confirmed on hardware
  (2.4GHz works).
- **`esp_event_loop.h` deprecation (DONE)** — removed the redundant include from
  config.h. `esp_event.h` (already included) provides everything used, including
  `system_event_t` via esp_event_legacy.h; the deprecated header only re-included
  esp_event.h and emitted the #warning.
- **PN532 SPI pins (latent bug)** — `PN532_MISO`/`PN532_MOSI` in shared.h (11/13)
  are swapped vs the shared bus (should be MISO=13/MOSI=11). Harmless today (no
  PN532 fitted on this board — GPS is the installed add-on), but fix to
  `PN532_MISO 13 / PN532_MOSI 11` before anyone adds a PN532. Note `PN532_SS 4`
  also collides with NRF24 #1 CSN (=4).

## Phase 2b — detailed plan (guard CC1101 + PN532 transaction bodies)
Goal = complete mutual exclusion. Phase 2 made the background SD writers
(wardriving on core 0, pcap) take the bus lock. For that to actually protect a
foreground CC1101/PN532 transaction, the radio side must ALSO hold the lock
during its SPI ops. These features are foreground/core-1; the real race is a
*lingering* background SD writer that kept running as you navigated into
SubGHz/RFID.

Constraint: `SpiBus.h` lives in the sketch folder; the bundled CC1101 lib (in
`Libraries/`) and the PN532 registry lib CANNOT include a sketch header. So
guard at the **sketch call sites** (subghz.cpp / rfid.cpp), where SpiBus.h is
already available via shared.h. (Alt: promote SpiBus to its own library so both
sketch and libs can `#include <SpiBus.h>` — more restructuring, skip for v1.)

CC1101 (subghz.cpp): the SmartRC lib funnels ALL bus access through
`SpiWriteReg / SpiWriteBurstReg / SpiStrobe / SpiReadReg / SpiReadBurstReg /
SpiReadStatus` (each toggles SS around SPI.transfer) — you do NOT edit the lib,
you guard the sketch ops that call it: `SetTx()/SetRx()`, `SendData()`,
`ReceiveData()`, and the raw jammer bursts near line ~2800
(`SpiWriteReg(TXFIFO,…)` + `SpiStrobe(STX)` in tight loops). Wrap each discrete
op in `{ SpiBusLock lock; … }`; for jammer bursts wrap the whole fill+strobe in
ONE lock but do NOT hold it across the inter-burst `delay*()`. For RX, guard the
register reads but poll GDO0 with `digitalRead` (a GPIO, not the bus) OUTSIDE the
lock so a long wait doesn't starve other owners.

PN532 (rfid.cpp): ~49 `s_nfc.*` calls clustering into ~10 operation functions
(scan/read/clone/key-brute/emulate/NTAG). `rfidAttachBus()` already locks the
wiring switch; the recursive mutex makes nested guards safe. Guard each card-op
*burst* (e.g. adjacent `mifareclassic_AuthenticateBlock` +
`ReadDataBlock`/`WriteDataBlock`, and each `readPassiveTargetID`) in
`{ SpiBusLock lock; }`. Note `readPassiveTargetID(…, timeoutMs)` polls the PN532
over SPI for its whole 80–150 ms timeout, so the lock is held that long —
acceptable, or lower the timeout if a background SD writer needs the bus sooner.

Verify: rebuild after CC1101, then after PN532 (separate commits), same green
target (RAM ~36%, Flash ~55%). Real test on hardware: run a SubGHz jammer/replay
and an RFID read while a wardriving/pcap capture is left running — the exact
cross-core scenario these guards close.

## Deployment — USB flash vs SD-card firmware update
The firmware has a built-in SD updater: `FirmwareUpdate` in wifi.cpp reads
**`/firmware.bin` from the SD-card root** and applies it via the ESP32 OTA
`Update` API (writeStream → the other app partition → reboot). So updates CAN go
by SD card — with caveats:
- **First flash must be over USB** (IDE / arduino-cli / esptool). Any change to
  the bootloader or partition table also requires a USB flash. After that, app
  updates can ride the SD path.
- The SD `firmware.bin` is the **plain compiled app image** (PlatformIO
  `.pio/build/<env>/firmware.bin`, ~1.7 MB), NOT a merged/0x0 image.
- **Partition scheme must be OTA-capable — use the included
  `ESP32-DIV/partitions.csv`.** It's a 16 MB dual-OTA table: two 4 MB
  `ota_0`/`ota_1` app slots + `otadata` + ~7.9 MB data. The ~1.7 MB app uses
  ~41% of a slot (verified build: Flash 41.4% of 4 MB). Wiring it up:
  - **Arduino IDE**: a file named `partitions.csv` in the sketch folder is used
    automatically and overrides the Tools ▸ Partition Scheme menu — it's already
    there, nothing to select.
  - **PlatformIO**: `board_build.partitions = ESP32-DIV/partitions.csv`.
  Do NOT use `huge_app.csv` for OTA — it's a single app slot with no OTA
  partition. Keep the SAME table between the USB flash and every SD `.bin`
  after, or OTA will mismatch.
- Workflow once set up: build → copy `firmware.bin` to SD root → on device
  Tools ▸ Update Firmware. No USB needed for app-only changes like our SPI patch.

## Reality check
A green build only proves it COMPILES, not that it fixes the crashes. The real
test is flashing and hammering the wardriving + 2.4GHz modules that were
crashing.

**VALIDATED 2026-08-02:** the cross-core stress test PASSED on hardware — a
background wardriving/pcap SD capture left running while driving SubGHz
(jammer/replay) and an RFID read, the exact race Phase 1/2/2b close. No
resets/corruption. Also confirmed independently: 2.4GHz (post NRF24 pin fix) and
GPS both work. The SPI arbitration patch is now hardware-proven, not just
compiling.
