#ifndef MATRIX_GLYPHS_H
#define MATRIX_GLYPHS_H

#include <stdint.h>

// 8x8 glyph bitmaps: 8 bytes, one per row (y=0 top). Bit b of a row byte is
// column x=b, i.e. LSB = leftmost column (font8x8 convention).
//
// Digits are from font8x8 by Daniel Hepper (public domain,
// https://github.com/dhepper/font8x8); only the digits the display can show
// (1 and 2 players) are vendored. The error "X" is a built-in symmetric cross.
static const uint8_t GLYPH_1[8] = {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00};
static const uint8_t GLYPH_2[8] = {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00};
static const uint8_t GLYPH_X[8] = {0x41, 0x22, 0x14, 0x08, 0x14, 0x22, 0x41, 0x00};

#endif // MATRIX_GLYPHS_H
