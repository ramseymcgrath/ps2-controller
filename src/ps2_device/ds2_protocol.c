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

static uint8_t detect_analog(const ds2_state_t *st) {
    int sum = st->poll_config[0] + st->poll_config[1]
            + st->poll_config[2] + st->poll_config[3];
    return (sum > 0) ? MODE_ANALOG_PRESSURE : MODE_ANALOG;
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
        case CMD_CONFIG: {
            for (int i = 0; i < 6 && n < cap; i++) out[n++] = 0x00;
            break;
        }
        case CMD_ANALOG_SWITCH: {
            for (int i = 0; i < 5 && n < cap; i++) out[n++] = 0x00;
            break;
        }
        default:
            break;
    }
    return n;
}

void ds2_apply_request(ds2_state_t *st, uint8_t cmd,
                       const uint8_t *req, size_t req_len) {
    switch (cmd) {
        case CMD_POLL:
            st->config = false;
            break;
        case CMD_CONFIG:
            if (req_len > 1) st->config = (req[1] == 0x01);
            break;
        case CMD_ANALOG_SWITCH:
            if (st->config && req_len > 2) {
                st->mode = (req[1] == 0x01) ? detect_analog(st) : MODE_DIGITAL;
                st->analog_lock = (req[2] == 0x03);
            }
            break;
        default:
            break;
    }
}
