#ifndef DS2_PROTOCOL_H
#define DS2_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "input_state.h"

typedef struct {
    uint8_t mode;            // MODE_DIGITAL or MODE_ANALOG. MODE_ANALOG_PRESSURE
                             // (0x79) is unreachable in the MVP — the emulator is
                             // clamped to analog; pressure is deferred to M5.
    bool    config;          // in config/escape mode
    bool    analog_lock;
    uint8_t poll_config[4];
    uint8_t motor_bytes[6];
} ds2_state_t;

void ds2_init(ds2_state_t *st);

// The leading response byte (0xF3 in config, else the current mode). The
// transport sends this simultaneously with the console's command byte — i.e.
// before ds2_response() can be called — so it is exposed separately. It equals
// out[0] from ds2_response() for the same state.
uint8_t ds2_id_byte(const ds2_state_t *st);

// Build the full DATA response frame for `cmd` given current state + input.
// out[0]=ID (0xF3 in config, else mode), out[1]=0x5A, then payload.
// `req` = the console's payload bytes after the command byte (for offset-
// dependent constants); may be NULL when req_len==0. Returns bytes written.
size_t ds2_response(const ds2_state_t *st, uint8_t cmd,
                    const PSXInputState *in,
                    const uint8_t *req, size_t req_len,
                    uint8_t *out, size_t cap);

// Apply a completed packet's request bytes: config enter/exit, mode switch,
// poll-config, motor mapping, and poll's implicit config-exit. Call at packet end.
void ds2_apply_request(ds2_state_t *st, uint8_t cmd,
                       const uint8_t *req, size_t req_len);

#endif // DS2_PROTOCOL_H
