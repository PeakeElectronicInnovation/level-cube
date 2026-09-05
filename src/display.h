#pragma once

#include <stdint.h>

// 8x7 WS2812B matrix, LEDs addressed left to right, top to bottom (board frame).
//
// Two coordinate systems:
//  - BOARD coords: fixed to the PCB (x 0..7, y 0..6). Used for the bubble, whose position is
//    physically meaningful regardless of how the board is held.
//  - VIEW coords: what the person looking at the board sees, after the current rotation
//    (see DisplayRotation in config.h). Width/height swap for the 90 degree cases. Used for
//    glyphs, bars and other UI so they always appear upright.
// All draw calls write into a frame buffer; display_show() pushes it to the LEDs.

void display_begin();
void display_off();

void display_setRotation(uint8_t rotation);
uint8_t display_viewW();
uint8_t display_viewH();

void display_clear();
void display_fill(uint8_t r, uint8_t g, uint8_t b);

// Board frame
void display_setBoardPixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);
// Bubble centred at (x, y) in Q4 fixed point board pixels (16 = one pixel), bilinearly spread
// over up to 2x2 LEDs so motion is smooth.
void display_drawBubble(int16_t xQ4, int16_t yQ4, uint8_t r, uint8_t g, uint8_t b);

// View frame
void display_setPixel(uint8_t vx, uint8_t vy, uint8_t r, uint8_t g, uint8_t b);
// Dim reference ticks marking the level position on the edges.
void display_drawTicks();
// 4x6 glyph (see characters.h), centred.
void display_drawGlyph(uint32_t bitmap, uint8_t r, uint8_t g, uint8_t b);
// Bitmap up to 8 wide: one byte per row, bit 0 = left-most column. Centred.
void display_drawBitmap(const uint8_t *rows, uint8_t w, uint8_t h, uint8_t r, uint8_t g, uint8_t b);
// Light the n-th pixel around the perimeter (clockwise from the viewer's top-left).
void display_setPerimeterPixel(uint8_t n, uint8_t r, uint8_t g, uint8_t b);
#define PERIMETER_LEN 26   // 2*(8+7) - 4, same for either rotation
// Horizontal bar on a view row, filled from the left. fillQ4 is in Q4 pixels (0..viewW*16).
void display_drawBar(uint8_t vy, uint8_t fillQ4, uint8_t r, uint8_t g, uint8_t b);

void display_show();
