#include "ds2_protocol.h"
#include "ds2_ids.h"
#include <string.h>

void ds2_init(ds2_state_t *st) {
    st->mode = MODE_DIGITAL;
    st->config = false;
    st->analog_lock = false;
    memset(st->poll_config, 0x00, sizeof st->poll_config); // -> detectAnalog() == ANALOG
    memset(st->motor_bytes, 0xFF, sizeof st->motor_bytes);
}

static uint8_t id_byte(const ds2_state_t *st) {
    return st->config ? MODE_CONFIG : st->mode;
}

size_t ds2_response(const ds2_state_t *st, uint8_t cmd,
                    const PSXInputState *in,
                    const uint8_t *req, size_t req_len,
                    uint8_t *out, size_t cap) {
    (void)req; (void)req_len;
    size_t n = 0;
    if (cap < 2) return 0;
    if (n < cap) out[n++] = id_byte(st);
    if (n < cap) out[n++] = 0x5A;

    switch (cmd) {
        case CMD_POLL: {
            if (st->mode == MODE_DIGITAL) {
                if (n < cap) out[n++] = in->buttons1;
                if (n < cap) out[n++] = in->buttons2;
            } else { // ANALOG (pressure handled in a later task)
                if (n < cap) out[n++] = in->buttons1;
                if (n < cap) out[n++] = in->buttons2;
                if (n < cap) out[n++] = in->rx;
                if (n < cap) out[n++] = in->ry;
                if (n < cap) out[n++] = in->lx;
                if (n < cap) out[n++] = in->ly;
            }
            break;
        }
        default:
            break;
    }
    return n;
}

void ds2_apply_request(ds2_state_t *st, uint8_t cmd,
                       const uint8_t *req, size_t req_len) {
    (void)req; (void)req_len;
    if (cmd == CMD_POLL) st->config = false;
}
