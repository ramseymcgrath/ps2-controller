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
    (void)st;
    // MVP: pressure (0x79) deferred to M5; always present as analog (0x73).
    // See docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md
    return MODE_ANALOG;
}

size_t ds2_response(const ds2_state_t *st, uint8_t cmd,
                    const PSXInputState *in,
                    const uint8_t *req, size_t req_len,
                    uint8_t *out, size_t cap) {
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
        case CMD_STATUS: {  // 0x45
            if (st->config) {
                const uint8_t tail[] = {0x03, 0x02,
                    (uint8_t)(st->mode == MODE_DIGITAL ? 0x00 : 0x01),
                    0x02, 0x01, 0x00};
                for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            }
            break;
        }
        case CMD_POLL_CONFIG_STATUS: {  // 0x41
            if (st->config) {
                bool dig = (st->mode == MODE_DIGITAL);
                const uint8_t tail[] = {
                    (uint8_t)(dig ? 0x00 : 0xFF), (uint8_t)(dig ? 0x00 : 0xFF),
                    (uint8_t)(dig ? 0x00 : 0x03), (uint8_t)(dig ? 0x00 : 0x00),
                    0x00, (uint8_t)(dig ? 0x00 : 0x5A)};
                for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            }
            break;
        }
        case CMD_PRES_CONFIG: {  // 0x40
            if (st->config) {
                const uint8_t tail[] = {0x00, 0x00, 0x02, 0x00, 0x00, 0x5A};
                for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            }
            break;
        }
        case CMD_CONST_46: {  // offset-dependent (req[1] = offset)
            if (st->config) {
                uint8_t off = (req && req_len > 1) ? req[1] : 0x00;
                const uint8_t tail[] = {0x00, 0x01,
                    (uint8_t)(off == 0x00 ? 0x02 : 0x01),
                    (uint8_t)(off == 0x00 ? 0x00 : 0x01), 0x0F};
                for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            }
            break;
        }
        case CMD_CONST_47: {
            if (st->config) {
                const uint8_t tail[] = {0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
                for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            }
            break;
        }
        case CMD_CONST_4C: {  // offset-dependent
            if (st->config) {
                uint8_t off = (req && req_len > 1) ? req[1] : 0x00;
                const uint8_t tail[] = {0x00, 0x00,
                    (uint8_t)(off == 0x00 ? 0x04 : 0x07), 0x00, 0x00};
                for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            }
            break;
        }
        case CMD_POLL_CONFIG:    // 0x4F — response is fixed 5A + 5 zeros
        case CMD_ENABLE_RUMBLE:  // 0x4D — response is fixed 5A + 5 zeros
            for (int i = 0; i < 5 && n < cap; i++) out[n++] = 0x00;
            break;
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
        case CMD_POLL_CONFIG:
            if (st->config && req_len > 4) {
                for (int i = 0; i < 4; i++) st->poll_config[i] = req[1 + i];
                st->mode = detect_analog(st);
            }
            break;
        case CMD_ENABLE_RUMBLE:
            if (st->config && req_len > 6)
                for (int i = 0; i < 6; i++) st->motor_bytes[i] = req[1 + i];
            break;
        default:
            break;
    }
}
