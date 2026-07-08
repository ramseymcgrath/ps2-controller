#include "shared_input.h"
#include "hardware/sync.h"   // __dmb()

// Seqlock: even sequence == stable, odd == a write is in progress. The single
// writer bumps the counter odd, stores, then bumps it even; a reader retries
// until it sees a stable, unchanged counter across the copy. Aligned 32-bit
// loads/stores are atomic on the Cortex-M33, and __dmb() orders the payload
// store/load against the counter so the reader never observes a torn frame.
static PSXInputState  s_state;
static volatile uint32_t s_seq;
static volatile bool  s_connected;

void shared_input_init(void) {
    s_state = ds2_neutral_state();
    s_seq = 0;
    s_connected = false;
}

void shared_input_publish(const PSXInputState *s) {
    uint32_t seq = s_seq + 1u;   // odd: write in progress
    s_seq = seq;
    __dmb();
    s_state = *s;
    __dmb();
    s_seq = seq + 1u;            // even: stable again
}

PSXInputState shared_input_snapshot(void) {
    PSXInputState out;
    uint32_t before, after;
    do {
        before = s_seq;
        __dmb();
        out = s_state;
        __dmb();
        after = s_seq;
    } while ((before & 1u) || before != after);
    return out;
}

void shared_input_set_connected(bool connected) { s_connected = connected; }
bool shared_input_connected(void) { return s_connected; }
