#pragma once

#include <stdint.h>

// Minimal ADXL345 driver over I2C plus smoothing. Values are in raw full-resolution LSB
// (3.9 mg/LSB, 1 g ~= 256). Filtered values are Q8 (raw << 8) for sub-LSB resolution.

bool accel_begin();                 // returns false if the device is not found
bool accel_poll();                  // call often; reads a sample when due, returns true if it did
int32_t accel_filtered(uint8_t axis);   // Q8 smoothed value for axis 0=X 1=Y 2=Z
int16_t accel_raw(uint8_t axis);        // last raw sample
uint8_t accel_dominantAxis();           // axis with the largest |filtered| value
