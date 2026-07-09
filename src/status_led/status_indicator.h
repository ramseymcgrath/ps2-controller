#ifndef STATUS_INDICATOR_H
#define STATUS_INDICATOR_H

#include "input_state.h"    // PSXInputState
#include "status_color.h"   // status_state_t

// External WS2812 data pin. GP16 is free (PS2 owns GP5-9, CYW43 owns GP23-25/29).
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 16u
#endif

// Claim one WS2812 SM on pio2 and start the 50 Hz render timer. Best-effort:
// on failure the module disables itself and all calls below become no-ops.
// Call once, on core0, after the system clock is set.
void status_indicator_init(void);

// Set the indicator state (core0 callers only).
void status_indicator_set(status_state_t s);

// Feed one controller input snapshot (core0, from the BluePad32 publish site);
// drives the input-paced rainbow.
void status_indicator_note_input(const PSXInputState *s);

#endif // STATUS_INDICATOR_H
