#pragma once

#include <stdint.h>

// Low res 4x6 character bitmaps. Each nibble is one row (LSB nibble = top row),
// bit 0 of the nibble is the left-most column.

static const uint32_t digit[10] = {
  0x69bd96,
  0x722232,
  0xf24896,
  0x69424f,
  0x44f555,
  0x69871f,
  0x69f196,
  0x11248f,
  0x699696,
  0x688e96
};

// Orientation glyphs, indexed by gravity axis: 0 = X, 1 = Y, 2 = Z
static const uint32_t axisGlyph[3] = {
  0x996699,   // X
  0x222699,   // Y
  0xf1248f    // Z
};
