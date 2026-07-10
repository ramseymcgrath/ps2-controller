#ifndef STATUS_STATE_H
#define STATUS_STATE_H

// Public status vocabulary for the LED indicator. The player count is encoded
// in the state (CONNECTED_1P / _2P) so the render layer needs no separate arg.
typedef enum {
    STATUS_BOOT = 0,       // pre-BT init
    STATUS_SEARCHING,      // BT up, no player connected (animated)
    STATUS_CONNECTED_1P,   // exactly one port connected
    STATUS_CONNECTED_2P,   // both ports connected
    STATUS_ERROR,          // fault (blinking)
} status_state_t;

#endif // STATUS_STATE_H
