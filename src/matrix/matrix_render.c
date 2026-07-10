#include "matrix_render.h"
#include "matrix_glyphs.h"

// HT16K33 bicolor RAM: row y uses frame[y*2+0]=green mask, frame[y*2+1]=red mask.
// Column-bit fixup: the Adafruit 8x8 bicolor maps logical column x to bit
// (x+7)&7. Single bench-tunable point (hardware open-item #1).
static inline unsigned col_fixup(unsigned x) { return (x + 7u) & 7u; }

void matrix_clear(uint8_t f[MATRIX_FRAME_BYTES]) {
    for (unsigned i = 0; i < MATRIX_FRAME_BYTES; i++) f[i] = 0;
}

void matrix_set_pixel(uint8_t f[MATRIX_FRAME_BYTES], unsigned x, unsigned y, unsigned color) {
    if (x >= 8u || y >= 8u) return;
    uint8_t bit = (uint8_t)(1u << col_fixup(x));
    uint8_t *g = &f[y * 2u + 0u];
    uint8_t *r = &f[y * 2u + 1u];
    *g = (uint8_t)(*g & ~bit);
    *r = (uint8_t)(*r & ~bit);
    if (color & MATRIX_GREEN) *g |= bit;
    if (color & MATRIX_RED)   *r |= bit;
}

unsigned matrix_get_pixel(const uint8_t f[MATRIX_FRAME_BYTES], unsigned x, unsigned y) {
    if (x >= 8u || y >= 8u) return MATRIX_OFF;
    uint8_t bit = (uint8_t)(1u << col_fixup(x));
    unsigned c = 0;
    if (f[y * 2u + 0u] & bit) c |= MATRIX_GREEN;
    if (f[y * 2u + 1u] & bit) c |= MATRIX_RED;
    return c;
}

static void draw_glyph(uint8_t f[MATRIX_FRAME_BYTES], const uint8_t g[8], unsigned color) {
    for (unsigned y = 0; y < 8u; y++)
        for (unsigned x = 0; x < 8u; x++)
            if (g[y] & (1u << x))
                matrix_set_pixel(f, x, y, color);
}

// 28-cell perimeter ring, clockwise from top-left.
static const uint8_t RING[28][2] = {
    {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0},
    {7,1},{7,2},{7,3},{7,4},{7,5},{7,6},{7,7},
    {6,7},{5,7},{4,7},{3,7},{2,7},{1,7},{0,7},
    {0,6},{0,5},{0,4},{0,3},{0,2},{0,1},
};

void matrix_render_frame(status_state_t s, uint16_t phase, uint8_t f[MATRIX_FRAME_BYTES]) {
    matrix_clear(f);
    switch (s) {
    case STATUS_BOOT:
        break;                                       // blank
    case STATUS_SEARCHING: {
        unsigned pos = (unsigned)((phase >> 6) % 28u);   // ~1.8 s / revolution
        matrix_set_pixel(f, RING[pos][0], RING[pos][1], MATRIX_GREEN);
        break;
    }
    case STATUS_CONNECTED_1P:
        draw_glyph(f, GLYPH_1, MATRIX_GREEN);
        break;
    case STATUS_CONNECTED_2P:
        draw_glyph(f, GLYPH_2, MATRIX_GREEN);
        break;
    case STATUS_ERROR:
        if ((phase >> 9) & 1u)                       // ~1 Hz blink
            draw_glyph(f, GLYPH_X, MATRIX_RED);
        break;
    }
}
