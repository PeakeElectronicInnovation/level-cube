#include "accel.h"
#include "config.h"
#include <Wire.h>

// ADXL345 registers
#define REG_DEVID        0x00
#define REG_BW_RATE      0x2C
#define REG_POWER_CTL    0x2D
#define REG_DATA_FORMAT  0x31
#define REG_DATAX0       0x32

#define DEVID_VALUE      0xE5
#define BW_RATE_100HZ    0x0A
#define POWER_CTL_MEASURE 0x08
#define DATA_FORMAT_FULLRES_2G 0x08

static int16_t raw[3];
static int32_t filt[3];
static bool primed = false;
static unsigned long nextSample = 0;

static void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADXL345_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

bool accel_begin() {
  Wire.begin();
  Wire.setClock(400000);
  if (readReg(REG_DEVID) != DEVID_VALUE) return false;
  writeReg(REG_DATA_FORMAT, DATA_FORMAT_FULLRES_2G);
  writeReg(REG_BW_RATE, BW_RATE_100HZ);
  writeReg(REG_POWER_CTL, POWER_CTL_MEASURE);
  nextSample = millis() + 20;   // let the first sample settle
  return true;
}

bool accel_poll() {
  unsigned long now = millis();
  if ((long)(now - nextSample) < 0) return false;
  nextSample += ACCEL_SAMPLE_MS;

  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(REG_DATAX0);
  Wire.endTransmission(false);
  if (Wire.requestFrom(ADXL345_ADDR, (uint8_t)6) != 6) return false;
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t lo = Wire.read(), hi = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);
  }

  if (!primed) {
    for (uint8_t i = 0; i < 3; i++) filt[i] = (int32_t)raw[i] << 8;
    primed = true;
  } else {
    for (uint8_t i = 0; i < 3; i++) filt[i] += (((int32_t)raw[i] << 8) - filt[i]) >> ACCEL_FILTER_SHIFT;
  }
  return true;
}

int32_t accel_filtered(uint8_t axis) { return filt[axis]; }
int16_t accel_raw(uint8_t axis) { return raw[axis]; }

uint8_t accel_dominantAxis() {
  uint8_t best = 0;
  int32_t bestMag = 0;
  for (uint8_t i = 0; i < 3; i++) {
    int32_t m = filt[i] < 0 ? -filt[i] : filt[i];
    if (m > bestMag) { bestMag = m; best = i; }
  }
  return best;
}
