# Level sensor (ATtiny1606 + ADXL345 + 8x7 WS2812B)

Two-axis level indicator. PlatformIO project, megaTinyCore, `Upload_UPDI` env (Atmel-ICE).

## Build / flash
- Build: `pio run`
- Flash: `pio run -t upload`
- Debug UART (PB2 TX, 115200): build with `-DDEBUG` (uncomment in `platformio.ini` or
  `$env:PLATFORMIO_BUILD_FLAGS="-DDEBUG"; pio run`), then `pio device monitor`.
- No hardware tests; verify on the board.

## Layout
- `include/config.h` - pins, timings, thresholds, per-orientation axis mapping/signs.
- `include/characters.h` - 4x6 glyph bitmaps (digits, X/Y/Z).
- `src/display.*` - LED frame buffer, SPI0 ISR streaming via CCL (LUT1 on PA7, SCK PA3).
- `src/accel.*` - minimal ADXL345 I2C driver (addr 0x53) + EMA smoothing (Q8).
- `src/main.cpp` - power latch, button state machine, orientation, calibration, auto-off, UI.
- `lib/SmoothLedCcl/` - vendored `SmoothLedCcl.h` from mattshepcar/SmoothLed (MIT), patched so
  the PC0/ASYNCCH2 branch compiles on 0-series parts. Do not add the registry SmoothLed lib.
- `hardware/` - Altium project + schematic PDF. `images/` - renders used by `README.md`.
- `README.md` - full user/hardware/firmware documentation; keep it in sync with `config.h`.

## Notes
- `millis()` uses TCA0 (`MILLIS_USE_TIMERA0`, defined by the PlatformIO ATtiny1606 board
  manifest, not in `platformio.ini`), leaving TCB0 free for the LED bit timer.
- Keep RAM small: 1 KB total. LED buffer is 168 B. Avoid floats in the loop and Adafruit libs.
- Orientation: button picks the up axis (X/Y/Z); the sign of gravity along it is auto-detected,
  giving 5 cases in `AXIS_MAP` (axis mapping, signs and display rotation). UI is drawn in
  "view" coords and rotated; the bubble is drawn in board coords.
- Calibration offsets live in EEPROM per case (magic byte at 0, 20 bytes from address 1).
