#include "display.h"
#include "config.h"
#include <SmoothLedCcl.h>

#define LED_CHANNELS 3

static SmoothLedCcl leds;
static uint8_t ledBuf[NUM_LEDS * LED_CHANNELS];   // GRB order
static volatile uint8_t ledDataPos = 0;
static uint8_t rotation = ROT_0;

static inline void waitIdle() { while (ledDataPos != 0); }
static inline bool sideways() { return rotation == ROT_COL0_TOP || rotation == ROT_COL7_TOP; }

void display_begin() {
  pinMode(LED_PWR_PIN, OUTPUT);
  digitalWrite(LED_PWR_PIN, HIGH);
  delay(10);
  leds.begin(SmoothLedCcl::PA7_LUT1, SmoothLedCcl::PA3_SPI0_ASYNCCH0);
  display_clear();
  display_show();
}

void display_off() {
  waitIdle();
  display_clear();
  display_show();
  waitIdle();
  delay(2);
  digitalWrite(LED_PWR_PIN, LOW);
}

void display_setRotation(uint8_t r) { rotation = r; }
uint8_t display_viewW() { return sideways() ? MATRIX_H : MATRIX_W; }
uint8_t display_viewH() { return sideways() ? MATRIX_W : MATRIX_H; }

void display_clear() { display_fill(0, 0, 0); }

void display_fill(uint8_t r, uint8_t g, uint8_t b) {
  waitIdle();
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    uint8_t *p = &ledBuf[i * LED_CHANNELS];
    p[0] = g; p[1] = r; p[2] = b;
  }
}

//----------------------------------- Board frame ---------------------------------------//

void display_setBoardPixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (x >= MATRIX_W || y >= MATRIX_H) return;
  uint8_t *p = &ledBuf[(y * MATRIX_W + x) * LED_CHANNELS];
  p[0] = g; p[1] = r; p[2] = b;
}

// Scale a colour by weight/256 and write it (weight 0 leaves the pixel untouched)
static void blendBoardPixel(int8_t x, int8_t y, uint16_t w, uint8_t r, uint8_t g, uint8_t b) {
  if (w == 0 || x < 0 || y < 0) return;
  display_setBoardPixel(x, y, (r * w) >> 8, (g * w) >> 8, (b * w) >> 8);
}

void display_drawBubble(int16_t xQ4, int16_t yQ4, uint8_t r, uint8_t g, uint8_t b) {
  waitIdle();
  const int16_t maxX = (MATRIX_W - 1) * 16, maxY = (MATRIX_H - 1) * 16;
  if (xQ4 < 0) xQ4 = 0; else if (xQ4 > maxX) xQ4 = maxX;
  if (yQ4 < 0) yQ4 = 0; else if (yQ4 > maxY) yQ4 = maxY;
  int8_t x0 = xQ4 >> 4, y0 = yQ4 >> 4;
  uint8_t fx = xQ4 & 15, fy = yQ4 & 15;
  // bilinear weights, each pair sums to 16 -> product sums to 256
  blendBoardPixel(x0,     y0,     (16 - fx) * (16 - fy), r, g, b);
  blendBoardPixel(x0 + 1, y0,     fx        * (16 - fy), r, g, b);
  blendBoardPixel(x0,     y0 + 1, (16 - fx) * fy,        r, g, b);
  blendBoardPixel(x0 + 1, y0 + 1, fx        * fy,        r, g, b);
}

//----------------------------------- View frame ----------------------------------------//

void display_setPixel(uint8_t vx, uint8_t vy, uint8_t r, uint8_t g, uint8_t b) {
  if (vx >= display_viewW() || vy >= display_viewH()) return;
  uint8_t bx, by;
  switch (rotation) {
    default:
    case ROT_0:        bx = vx;                by = vy;                break;
    case ROT_180:      bx = MATRIX_W - 1 - vx; by = MATRIX_H - 1 - vy; break;
    case ROT_COL0_TOP: bx = vy;                by = MATRIX_H - 1 - vx; break;
    case ROT_COL7_TOP: bx = MATRIX_W - 1 - vy; by = vx;                break;
  }
  display_setBoardPixel(bx, by, r, g, b);
}

static void blendPixel(uint8_t vx, uint8_t vy, uint16_t w, uint8_t r, uint8_t g, uint8_t b) {
  if (w) display_setPixel(vx, vy, (r * w) >> 8, (g * w) >> 8, (b * w) >> 8);
}

void display_drawTicks() {
  const uint8_t t = TICK_BRIGHTNESS;
  const uint8_t w = display_viewW(), h = display_viewH();
  // even dimensions have two centre lines, odd have one
  for (uint8_t c = (w - 1) / 2; c <= w / 2; c++) {
    display_setPixel(c, 0, t, t, t);
    display_setPixel(c, h - 1, t, t, t);
  }
  for (uint8_t rrow = (h - 1) / 2; rrow <= h / 2; rrow++) {
    display_setPixel(0, rrow, t, t, t);
    display_setPixel(w - 1, rrow, t, t, t);
  }
}

void display_drawGlyph(uint32_t bitmap, uint8_t r, uint8_t g, uint8_t b) {
  waitIdle();
  const uint8_t x0 = (display_viewW() - 4) / 2, y0 = (display_viewH() - 6) / 2;
  for (uint8_t row = 0; row < 6; row++)
    for (uint8_t col = 0; col < 4; col++, bitmap >>= 1)
      if (bitmap & 1) display_setPixel(x0 + col, y0 + row, r, g, b);
}

void display_drawBitmap(const uint8_t *rows, uint8_t w, uint8_t h, uint8_t r, uint8_t g, uint8_t b) {
  waitIdle();
  const uint8_t x0 = (display_viewW() - w) / 2, y0 = (display_viewH() - h) / 2;
  for (uint8_t y = 0; y < h; y++) {
    uint8_t bits = rows[y];
    for (uint8_t x = 0; x < w; x++, bits >>= 1)
      if (bits & 1) display_setPixel(x0 + x, y0 + y, r, g, b);
  }
}

void display_setPerimeterPixel(uint8_t n, uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t w = display_viewW(), h = display_viewH();
  n %= PERIMETER_LEN;
  uint8_t x, y;
  if (n < w)                    { x = n;                 y = 0; }              // top, left -> right
  else if (n < w + h - 1)       { x = w - 1;             y = n - (w - 1); }    // right, down
  else if (n < 2 * w + h - 2)   { x = 2 * w + h - 3 - n; y = h - 1; }          // bottom, right -> left
  else                          { x = 0;                 y = PERIMETER_LEN - n; } // left, up
  display_setPixel(x, y, r, g, b);
}

void display_drawBar(uint8_t vy, uint8_t fillQ4, uint8_t r, uint8_t g, uint8_t b) {
  waitIdle();
  const uint8_t w = display_viewW();
  uint8_t full = fillQ4 >> 4, frac = fillQ4 & 15;
  for (uint8_t x = 0; x < full && x < w; x++) display_setPixel(x, vy, r, g, b);
  if (full < w) blendPixel(full, vy, frac * 16, r, g, b);
}

//----------------------------------- Output --------------------------------------------//

void display_show() {
  waitIdle();
  leds.beginTransaction();
  SPI0.INTCTRL |= SPI_DREIE_bm;      // 'data register empty' interrupt starts sending
}

ISR(SPI0_INT_vect) {
  SPI0.INTFLAGS |= SPI_DREIF_bm;
  SPI0.DATA = ~ledBuf[ledDataPos];   // inverted for the CCL LUT
  if (++ledDataPos == sizeof(ledBuf)) {
    SPI0.INTCTRL &= ~SPI_DREIE_bm;
    ledDataPos = 0;
  }
}
