#ifndef STATUS_INDICATOR_H
#define STATUS_INDICATOR_H

#include "status_state.h"

// Bring the status LED up (I2C matrix). Best-effort: on failure the module
// disables itself and the calls below become no-ops. Call once on core0 after
// the system clock is set.
void status_indicator_init(void);

// Set the indicator state (core0 callers only).
void status_indicator_set(status_state_t s);

#endif // STATUS_INDICATOR_H
