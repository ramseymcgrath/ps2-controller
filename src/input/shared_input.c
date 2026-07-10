#include "shared_input.h"
#include "hardware/sync.h"   // __dmb()

// One seqlock slot per port. Even sequence == stable, odd == a write is in
// progress. The single writer bumps the counter odd, stores, then bumps it even;
// a reader retries until it sees a stable, unchanged counter across the copy.
// Aligned 32-bit loads/stores are atomic on the Cortex-M33, and __dmb() orders
// the payload against the counter so the reader never observes a torn frame.
typedef struct {
    PSXInputState     state;
    volatile uint32_t seq;
    volatile bool     connected;
} slot_t;

static slot_t s_slot[PS2_NUM_PORTS];

void shared_input_init(void) {
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++) {
        s_slot[p].state = ds2_neutral_state();
        s_slot[p].seq = 0;
        s_slot[p].connected = false;
    }
}

void shared_input_publish(unsigned port, const PSXInputState *s) {
    slot_t *sl = &s_slot[port];
    uint32_t seq = sl->seq + 1u;   // odd: write in progress
    sl->seq = seq;
    __dmb();
    sl->state = *s;
    __dmb();
    sl->seq = seq + 1u;            // even: stable again
}

PSXInputState shared_input_snapshot(unsigned port) {
    slot_t *sl = &s_slot[port];
    PSXInputState out;
    uint32_t before, after;
    do {
        before = sl->seq;
        __dmb();
        out = sl->state;
        __dmb();
        after = sl->seq;
    } while ((before & 1u) || before != after);
    return out;
}

void shared_input_set_connected(unsigned port, bool connected) { s_slot[port].connected = connected; }
bool shared_input_connected(unsigned port) { return s_slot[port].connected; }
