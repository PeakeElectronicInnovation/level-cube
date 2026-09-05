#pragma once

#include <Arduino.h>

//----------------------------------- Hardware pins ------------------------------------//
#define BATT_VOLTS_PIN    PIN_PA1
#define USB_VOLTS_PIN     PIN_PA2
#define ANA_FLOAT_PIN     PIN_PA4
#define AXL_INT1_PIN      PIN_PA5
#define AXL_INT2_PIN      PIN_PA6
#define LED_DAT_PIN       PIN_PA7      // CCL LUT1 output, SPI0 SCK on PA3 is the bit clock
#define I2C_SCL_PIN       PIN_PB0
#define I2C_SDA_PIN       PIN_PB1
#define UART_TX_PIN       PIN_PB2
#define UART_RX_PIN       PIN_PB3
#define PWR_PIN           PIN_PC0      // drive high to hold main power on
#define LED_PWR_PIN       PIN_PC1      // drive high to power the LED matrix
#define BUTTON_PIN        PIN_PC2      // active low, needs pull-up
#define CC_STAT_PIN       PIN_PC3

//----------------------------------- LED matrix ---------------------------------------//
#define MATRIX_W          8
#define MATRIX_H          7
#define NUM_LEDS          (MATRIX_W * MATRIX_H)
#define FRAME_PERIOD_MS   25           // display refresh (~40 Hz)

// Brightness (0-255 per channel). Keep low: 56 LEDs at full white would draw ~3 A.
#define BUBBLE_BRIGHTNESS 48
#define TICK_BRIGHTNESS   3            // dim edge reference ticks
#define GLYPH_BRIGHTNESS  24
#define BAR_BRIGHTNESS    24

//----------------------------------- Accelerometer ------------------------------------//
#define ADXL345_ADDR      0x53         // SDO/ALT pulled low
#define ACCEL_SAMPLE_MS   10           // 100 Hz, matches ADXL345 BW_RATE setting
#define ACCEL_FILTER_SHIFT 4           // EMA: y += (x - y) >> shift  (tau ~ 160 ms @ 100 Hz)

// Full-resolution mode: 3.9 mg/LSB -> 1 g = ~256 LSB. Small angle: LSB ~= 256*sin(angle)
#define TILT_FULL_SCALE_LSB  45        // ~10 deg = half matrix width
#define TILT_LEVEL_LSB        4.5f     // ~1 deg = "level" (green)
#define TILT_NEAR_LSB        13.4f     // ~3 deg = "near" (amber)

// The button selects which sensor axis is "up" (0 = X, 1 = Y, 2 = Z). For X and Y the sign of
// gravity along that axis is detected automatically, giving 5 cases. Each case defines which
// sensor axis drives the matrix columns/rows (with sign so the bubble travels toward the HIGH
// side) and how the display must be rotated so the UI appears upright to the viewer.
// Case index = axis * 2 + (gravity negative ? 1 : 0); Z uses case 4 only.
enum DisplayRotation : uint8_t {
  ROT_0,          // row 0 at top, col 0 at left (board flat or +Y up)
  ROT_180,        // upside down
  ROT_COL0_TOP,   // board turned so column 0 is at the top
  ROT_COL7_TOP,   // board turned so column 7 is at the top
};
struct AxisMap { uint8_t colAxis; int8_t colSign; uint8_t rowAxis; int8_t rowSign; uint8_t rotation; };
#define NUM_CASES 5
static const AxisMap AXIS_MAP[NUM_CASES] = {
  { 2, -1, 1, -1, ROT_COL0_TOP },   // 0: +X up  cols <- Z, rows <- Y
  { 2, +1, 1, -1, ROT_COL7_TOP },   // 1: -X up  cols <- Z, rows <- Y
  { 0, -1, 2, -1, ROT_0 },          // 2: +Y up  cols <- X, rows <- Z
  { 0, -1, 2, +1, ROT_180 },        // 3: -Y up  cols <- X, rows <- Z
  { 0, -1, 1, -1, ROT_0 },          // 4:  Z up  cols <- X, rows <- Y (flat)
};
#define UP_SIGN_THRESHOLD_LSB 128    // ~0.5 g before the up/down sign is switched

//----------------------------------- Button timing ------------------------------------//
#define BUTTON_DEBOUNCE_MS     30
#define BUTTON_POWER_OFF_MS  2000      // hold 2-5 s then release -> power off
#define BUTTON_CALIBRATE_MS  5000      // hold >5 s then release -> calibrate level

//----------------------------------- Auto power off -----------------------------------//
#define AUTO_OFF_MS          60000UL   // 1 minute of stillness
#define MOVE_CHECK_MS        500
#define MOVE_THRESHOLD_LSB   3         // ~0.7 deg change counts as movement

//----------------------------------- Battery ------------------------------------------//
#define ANA_REF              INTERNAL2V5
#define ANA_RES              10
// Divider is 100k/100k with 2.5 V reference: mV = raw * 5000 / 1024
#define V_BAT_MIN_MV         3400
#define V_BAT_FULL_MV        4150
#define BATT_CHECK_MS        5000
#define BATT_GAUGE_SHOW_MS   700

//----------------------------------- UI timing ----------------------------------------//
#define GLYPH_SHOW_MS        500

//----------------------------------- Calibration --------------------------------------//
#define CAL_COUNTDOWN_MS     5000      // 5..1 countdown so the device can be put down and settle
#define CAL_SAMPLE_MS        1500      // averaging window (150 samples @ 100 Hz)
#define CAL_DONE_MS          800       // "tick" shown after calibration

//----------------------------------- EEPROM -------------------------------------------//
#define EEPROM_MAGIC         0xA5
#define EEPROM_ADDR_MAGIC    0
#define EEPROM_ADDR_CAL      1         // 5 cases x 2 axes x int16 = 20 bytes

//----------------------------------- Debug --------------------------------------------//
#ifdef DEBUG
  #define DBG_BEGIN()   Serial.begin(115200)
  #define DBG(x)        Serial.print(x)
  #define DBGLN(x)      Serial.println(x)
#else
  #define DBG_BEGIN()
  #define DBG(x)
  #define DBGLN(x)
#endif
