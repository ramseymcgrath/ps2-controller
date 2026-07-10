#ifndef MATRIX_RENDER_H
#define MATRIX_RENDER_H

#include <stdint.h>
#include "status_state.h"

#define MATRIX_FRAME_BYTES 16u

enum { MATRIX_OFF = 0, MATRIX_GREEN = 1, MATRIX_RED = 2, MATRIX_YELLOW = 3 };

// Zero all 16 bytes (both color planes).
void matrix_clear(uint8_t frame[MATRIX_FRAME_BYTES]);

// Set pixel (x,y), 0..7, to color (0=off,1=green,2=red,3=yellow). Off clears
// both planes. Out-of-range is ignored. Applies the panel column-bit fixup.
void matrix_set_pixel(uint8_t frame[MATRIX_FRAME_BYTES], unsigned x, unsigned y, unsigned color);

// Read back the color at (x,y). Out-of-range returns MATRIX_OFF.
unsigned matrix_get_pixel(const uint8_t frame[MATRIX_FRAME_BYTES], unsigned x, unsigned y);

// Compose the full frame for a status state. `phase` is a free-running
// millisecond counter driving the searching orbit and the error blink.
void matrix_render_frame(status_state_t s, uint16_t phase, uint8_t frame[MATRIX_FRAME_BYTES]);

#endif // MATRIX_RENDER_H
