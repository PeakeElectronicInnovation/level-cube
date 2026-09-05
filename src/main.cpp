#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "characters.h"
#include "display.h"
#include "accel.h"

//----------------------------------- State ---------------------------------------------//

enum UiMode : uint8_t { UI_BATTERY, UI_GLYPH, UI_CAL_COUNTDOWN, UI_CAL_SAMPLING, UI_CAL_DONE, UI_LEVEL };

static UiMode uiMode = UI_BATTERY;
static unsigned long uiUntil = 0, uiStart = 0;

static uint8_t upAxis = 2;               // sensor axis pointing up (0=X 1=Y 2=Z), button selected
static bool upNegative = false;          // gravity along upAxis is negative (board flipped), auto
static uint8_t activeCase = 4;           // index into AXIS_MAP, derived from the two above
static int16_t calOffset[NUM_CASES][2];  // per case: [col, row] zero offsets, Q4 LSB
static int32_t calSum[2];                // accumulators while sampling
static uint16_t calCount = 0;

// 7x7 tick, fits either rotation
static const uint8_t tickBitmap[7] = { 0x00, 0x40, 0x20, 0x11, 0x0A, 0x04, 0x00 };

// Button
static bool btnRawLast = true, btnDown = true, btnInitialPress = true;
static unsigned long btnChangeAt = 0, btnPressAt = 0;

// Auto power off
static unsigned long lastMoveAt = 0, nextMoveCheck = 0;
static int16_t moveRef[3];

// Battery
static unsigned long nextBattCheck = 0;
static uint8_t battPercent = 100;

static unsigned long nextFrame = 0;

//----------------------------------- Power ---------------------------------------------//

static void powerDown() {
  DBGLN("Power down");
  display_off();
  while (!digitalRead(BUTTON_PIN));
  digitalWrite(PWR_PIN, LOW);
  while (1);
}

//----------------------------------- Battery -------------------------------------------//

static uint16_t readBatteryMv() {
  uint32_t raw = analogRead(BATT_VOLTS_PIN);
  return (uint16_t)(raw * 5000UL / 1024UL);
}

static void checkBattery() {
  uint16_t mv = readBatteryMv();
  DBG("Vbat mV: "); DBGLN(mv);
  if (mv < V_BAT_MIN_MV) {
    for (uint8_t i = 0; i < 3; i++) {
      display_fill(BAR_BRIGHTNESS, 0, 0); display_show(); delay(150);
      display_clear(); display_show(); delay(150);
    }
    powerDown();
  }
  int32_t pct = ((int32_t)mv - V_BAT_MIN_MV) * 100L / (V_BAT_FULL_MV - V_BAT_MIN_MV);
  battPercent = pct < 0 ? 0 : pct > 100 ? 100 : (uint8_t)pct;
}

//----------------------------------- Calibration ---------------------------------------//

static void loadCalibration() {
  if (EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_ADDR_CAL, calOffset);
  } else {
    memset(calOffset, 0, sizeof(calOffset));
  }
}

static void saveCalibration() {
  EEPROM.put(EEPROM_ADDR_CAL, calOffset);
  EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
}

//----------------------------------- Orientation ---------------------------------------//

// Re-evaluate which of the 5 cases applies. The up axis comes from the button, its sign from
// gravity (with hysteresis so it only flips once the board is clearly turned over).
static void updateOrientation() {
  if (upAxis == 2) {
    upNegative = false;
  } else {
    int16_t g = accel_filtered(upAxis) >> 8;
    if (g > UP_SIGN_THRESHOLD_LSB) upNegative = false;
    else if (g < -UP_SIGN_THRESHOLD_LSB) upNegative = true;
  }
  uint8_t c = upAxis == 2 ? 4 : upAxis * 2 + (upNegative ? 1 : 0);
  if (c != activeCase) {
    activeCase = c;
    display_setRotation(AXIS_MAP[c].rotation);
    DBG("Case: "); DBGLN(c);
  }
}

//----------------------------------- Level maths ---------------------------------------//

// Signed, mapped tilt for the current case in Q4 LSB, before calibration offset
static int16_t mappedTilt(uint8_t axis, int8_t sign) {
  return (int16_t)((accel_filtered(axis) >> 4) * sign);
}

static void getTilt(int16_t &col, int16_t &row) {
  const AxisMap &m = AXIS_MAP[activeCase];
  col = mappedTilt(m.colAxis, m.colSign) - calOffset[activeCase][0];
  row = mappedTilt(m.rowAxis, m.rowSign) - calOffset[activeCase][1];
}

static void setUi(UiMode mode, unsigned long duration) {
  uiMode = mode;
  uiStart = millis();
  uiUntil = uiStart + duration;
}

static bool calibrating() {
  return uiMode == UI_CAL_COUNTDOWN || uiMode == UI_CAL_SAMPLING || uiMode == UI_CAL_DONE;
}

static void startCalibration() {
  DBGLN("Calibration countdown");
  setUi(UI_CAL_COUNTDOWN, CAL_COUNTDOWN_MS);
}

// Called for every new accelerometer sample while sampling: accumulate mapped raw values (Q4)
static void calibrationSample() {
  const AxisMap &m = AXIS_MAP[activeCase];
  calSum[0] += ((int32_t)accel_raw(m.colAxis) << 4) * m.colSign;
  calSum[1] += ((int32_t)accel_raw(m.rowAxis) << 4) * m.rowSign;
  calCount++;
}

static void finishCalibration() {
  if (calCount) {
    calOffset[activeCase][0] = calSum[0] / (int32_t)calCount;
    calOffset[activeCase][1] = calSum[1] / (int32_t)calCount;
    saveCalibration();
  }
  DBG("Calibrated from "); DBG(calCount); DBGLN(" samples");
}

//----------------------------------- UI ------------------------------------------------//

static void showGlyph() { setUi(UI_GLYPH, GLYPH_SHOW_MS); }

static void drawLevel() {
  int16_t col, row;
  getTilt(col, row);

  // Bubble travels toward the high side; position scaled so TILT_FULL_SCALE_LSB = half width
  const int16_t centreX = (MATRIX_W - 1) * 8;    // 3.5 px in Q4
  const int16_t centreY = (MATRIX_H - 1) * 8;    // 3.0 px in Q4
  const int32_t scaleNum = (MATRIX_W - 1) * 8;
  const int32_t scaleDen = (int32_t)TILT_FULL_SCALE_LSB * 16;
  int16_t x = centreX + (int16_t)((int32_t)col * scaleNum / scaleDen);
  int16_t y = centreY + (int16_t)((int32_t)row * scaleNum / scaleDen);

  const int32_t levelSq = (int32_t)(TILT_LEVEL_LSB * 16 * TILT_LEVEL_LSB * 16);
  const int32_t nearSq  = (int32_t)(TILT_NEAR_LSB * 16 * TILT_NEAR_LSB * 16);
  int32_t d2 = (int32_t)col * col + (int32_t)row * row;

  uint8_t r, g, b = 0;
  if (d2 < levelSq)     { r = 0;                 g = BUBBLE_BRIGHTNESS; }
  else if (d2 < nearSq) { r = BUBBLE_BRIGHTNESS; g = BUBBLE_BRIGHTNESS / 2; }
  else                  { r = BUBBLE_BRIGHTNESS; g = 0; }

  display_clear();
  display_drawTicks();
  display_drawBubble(x, y, r, g, b);
}

static void drawHoldBar(unsigned long held) {
  const uint8_t w = display_viewW(), bottom = display_viewH() - 1;
  const uint16_t fullQ4 = w * 16;
  uint8_t fill, r, g, b;
  if (held < BUTTON_POWER_OFF_MS) {
    fill = held * fullQ4 / BUTTON_POWER_OFF_MS;
    r = g = b = BAR_BRIGHTNESS;
  } else if (held < BUTTON_CALIBRATE_MS) {
    fill = (held - BUTTON_POWER_OFF_MS) * fullQ4 / (BUTTON_CALIBRATE_MS - BUTTON_POWER_OFF_MS);
    r = BAR_BRIGHTNESS; g = b = 0;
  } else {
    fill = fullQ4;
    b = BAR_BRIGHTNESS; r = g = 0;
  }
  for (uint8_t x = 0; x < w; x++) display_setPixel(x, bottom, 0, 0, 0);
  display_drawBar(bottom, fill, r, g, b);
}

static void drawBatteryGauge() {
  uint8_t r, g;
  if (battPercent > 50)      { r = 0; g = BAR_BRIGHTNESS; }
  else if (battPercent > 20) { r = BAR_BRIGHTNESS; g = BAR_BRIGHTNESS / 2; }
  else                       { r = BAR_BRIGHTNESS; g = 0; }
  display_clear();
  display_drawBar(display_viewH() / 2, battPercent * (display_viewW() * 16) / 100, r, g, 0);
}

static void drawCountdown(unsigned long now) {
  uint8_t secs = (uiUntil - now) / 1000 + 1;      // 5,4,3,2,1
  if (secs > 9) secs = 9;
  display_clear();
  display_drawGlyph(digit[secs], GLYPH_BRIGHTNESS, GLYPH_BRIGHTNESS, 0);
}

static void drawSampling(unsigned long now) {
  unsigned long elapsed = now - uiStart;
  display_clear();
  // blue dot chasing round the perimeter with a fading tail
  uint8_t head = (elapsed / 40) % PERIMETER_LEN;
  for (uint8_t i = 0; i < 4; i++)
    display_setPerimeterPixel(head + PERIMETER_LEN - i, 0, 0, BAR_BRIGHTNESS >> i);
  // centre row fills as the averaging window progresses
  uint8_t fill = elapsed * (display_viewW() * 16) / CAL_SAMPLE_MS;
  display_drawBar(display_viewH() / 2, fill, 0, 0, BAR_BRIGHTNESS);
}

static void render() {
  unsigned long now = millis();
  if (uiMode != UI_LEVEL && (long)(now - uiUntil) >= 0) {
    switch (uiMode) {
      case UI_BATTERY:
        // Filter has settled during the gauge; pick the starting up axis from gravity
        upAxis = accel_dominantAxis();
        DBG("Up axis: "); DBGLN(upAxis);
        updateOrientation();
        showGlyph();
        break;
      case UI_CAL_COUNTDOWN:
        calSum[0] = calSum[1] = 0;
        calCount = 0;
        setUi(UI_CAL_SAMPLING, CAL_SAMPLE_MS);
        break;
      case UI_CAL_SAMPLING:
        finishCalibration();
        setUi(UI_CAL_DONE, CAL_DONE_MS);
        break;
      default:
        uiMode = UI_LEVEL;
        break;
    }
  }

  switch (uiMode) {
    case UI_BATTERY: drawBatteryGauge(); break;
    case UI_GLYPH:
      display_clear();
      display_drawGlyph(axisGlyph[upAxis], GLYPH_BRIGHTNESS, GLYPH_BRIGHTNESS, GLYPH_BRIGHTNESS);
      break;
    case UI_CAL_COUNTDOWN: drawCountdown(now); break;
    case UI_CAL_SAMPLING:  drawSampling(now); break;
    case UI_CAL_DONE:
      display_clear();
      display_drawBitmap(tickBitmap, 7, 7, 0, BUBBLE_BRIGHTNESS, 0);
      break;
    case UI_LEVEL: drawLevel(); break;
  }
  if (btnDown && !btnInitialPress) drawHoldBar(now - btnPressAt);
  display_show();
}

//----------------------------------- Button --------------------------------------------//

static void onShortPress() {
  upAxis = (upAxis + 1) % 3;
  DBG("Up axis: "); DBGLN(upAxis);
  updateOrientation();
  showGlyph();
}

static void pollButton() {
  unsigned long now = millis();
  bool rawDown = !digitalRead(BUTTON_PIN);
  if (rawDown != btnRawLast) {
    btnRawLast = rawDown;
    btnChangeAt = now;
    return;
  }
  if (rawDown == btnDown || now - btnChangeAt < BUTTON_DEBOUNCE_MS) return;

  btnDown = rawDown;
  lastMoveAt = now;                       // button activity counts as movement
  if (btnDown) {
    btnPressAt = now;
    return;
  }

  // released
  if (btnInitialPress) { btnInitialPress = false; return; }
  unsigned long held = now - btnPressAt;
  if (held >= BUTTON_CALIBRATE_MS) {
    startCalibration();
  } else if (held >= BUTTON_POWER_OFF_MS) {
    powerDown();
  } else if (!calibrating()) {
    onShortPress();
  }
}

//----------------------------------- Auto power off ------------------------------------//

static void pollMovement() {
  unsigned long now = millis();
  if ((long)(now - nextMoveCheck) < 0) return;
  nextMoveCheck = now + MOVE_CHECK_MS;

  bool moved = false;
  for (uint8_t i = 0; i < 3; i++) {
    int16_t cur = accel_filtered(i) >> 8;
    int16_t d = cur - moveRef[i];
    if (d > MOVE_THRESHOLD_LSB || d < -MOVE_THRESHOLD_LSB) { moved = true; moveRef[i] = cur; }
  }
  if (moved) lastMoveAt = now;
  else if (now - lastMoveAt >= AUTO_OFF_MS) {
    DBGLN("Auto off");
    powerDown();
  }
}

//----------------------------------- Setup / loop --------------------------------------//

void setup() {
  // Latch power on first, before anything else can delay us
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CC_STAT_PIN, INPUT_PULLUP);
  pinMode(AXL_INT1_PIN, INPUT_PULLUP);
  pinMode(AXL_INT2_PIN, INPUT_PULLUP);
  analogReadResolution(ANA_RES);
  analogReference(ANA_REF);

  DBG_BEGIN();
  DBGLN("Level sensor");

  loadCalibration();
  display_begin();

  if (!accel_begin()) {
    DBGLN("ADXL345 not found");
    display_fill(BAR_BRIGHTNESS, 0, 0);
    display_show();
    delay(1000);
  }

  checkBattery();
  unsigned long now = millis();
  uiMode = UI_BATTERY;
  uiUntil = now + BATT_GAUGE_SHOW_MS;
  lastMoveAt = now;
  nextMoveCheck = now + MOVE_CHECK_MS;
  nextBattCheck = now + BATT_CHECK_MS;
  nextFrame = now;
}

void loop() {
  if (accel_poll() && uiMode == UI_CAL_SAMPLING) calibrationSample();
  pollButton();
  pollMovement();

  unsigned long now = millis();
  if ((long)(now - nextBattCheck) >= 0) {
    nextBattCheck = now + BATT_CHECK_MS;
    checkBattery();
  }
  if ((long)(now - nextFrame) >= 0) {
    nextFrame = now + FRAME_PERIOD_MS;
    if (uiMode == UI_LEVEL || uiMode == UI_GLYPH) updateOrientation();
    render();
#ifdef DEBUG
    static uint8_t dbgDiv = 0;
    if (++dbgDiv >= 20) {
      dbgDiv = 0;
      int16_t c, r; getTilt(c, r);
      DBG("tilt col/row Q4: "); DBG(c); DBG(' '); DBGLN(r);
    }
#endif
  }
}
