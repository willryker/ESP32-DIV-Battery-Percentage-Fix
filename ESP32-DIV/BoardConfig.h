#pragma once

// Select the hardware target.
// Leave all lines commented to use the ESP32-DIV V2 wiring.

// #define BOARD_CYD
// #define BOARD_ESP32_DIV_V1
 #define BOARD_ESP32_DIV_V2
// ESP32-DIV V2 battery configuration
// GPIO 2 is shared by the buzzer and battery divider.
// Disable the buzzer so GPIO 2 can function as an ADC input.
#define BUZZER_PIN -1

#define BATTERY_ADC_PIN 2
// Divider ratio empirically calibrated on hardware: multimeter read 3.525 V at
// the cell while GPIO2 read 0.633 V, i.e. ratio = 3.525 / 0.633 = 5.569x (the
// nominal 100k/100k "2x" was wrong — the fitted divider / S3 ADC give ~5.57x).
// readBatteryVoltage() uses (R1+R2)/R2, so R1=456900, R2=100000 -> 5.569x.
#define BATTERY_VDIV_R1 456900.0f
#define BATTERY_VDIV_R2 100000.0f
// Set to 0 to hide the on-screen touch nav bar (5 footer buttons).
// Touch button input will still work when this is disabled.
#define TOUCH_BUTTON_CUE_ENABLED 1

// Optional fixed PCF8574 I2C address (0x20-0x27). Leave commented for auto-detect.
//#define pcf_ADDR 0x20

// Optional per-board touch calibration overrides (raw XPT2046 ADC range).
// CYD defaults (portrait): X 200..3700, Y 240..3800 — run Touch Calibrate if needed.
//#define TOUCH_X_MIN 200
//#define TOUCH_X_MAX 3700
//#define TOUCH_Y_MIN 240
//#define TOUCH_Y_MAX 3800

#if !defined(BOARD_ESP32_DIV_V2) && !defined(BOARD_CYD) && !defined(BOARD_ESP32_DIV_V1)
#define BOARD_ESP32_DIV_V2
#endif
