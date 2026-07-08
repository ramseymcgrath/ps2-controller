#ifndef INPUT_STATE_H
#define INPUT_STATE_H
#include <stdint.h>

// DualShock controller state, already in PS2 wire encoding.
//
// _Alignas(4): at runtime this 8-byte struct is shared core0 (BluePad32 writer) <->
// core1 (poll-response reader). Word alignment lets the deferred atomic-snapshot
// publish (seqlock / double-buffer) move it as aligned 32-bit words instead of
// byte-by-byte, and avoids unaligned exclusive-access faults on the Cortex-M33.
// It's free today: the struct is exactly 8 bytes.
typedef struct {
    _Alignas(4) uint8_t buttons1;  // BTNL, active-low: Select,L3,R3,Start, Up,Right,Down,Left
    uint8_t buttons2;  // BTNH, active-low: L2,R2,L1,R1, Tri,Circle,Cross,Square
    uint8_t rx, ry;    // right stick, 0x00..0xFF, neutral 0x80
    uint8_t lx, ly;    // left  stick, 0x00..0xFF, neutral 0x80
    uint8_t l2, r2;    // analog trigger pressure (used only in deferred 0x79 mode)
} PSXInputState;

static inline PSXInputState ds2_neutral_state(void) {
    PSXInputState s;
    s.buttons1 = 0xFF; s.buttons2 = 0xFF;
    s.rx = s.ry = s.lx = s.ly = 0x80;
    s.l2 = s.r2 = 0x00;
    return s;
}
#endif // INPUT_STATE_H
