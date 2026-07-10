#include "status_indicator.h"

// Stub: the matrix backend is wired in a later task. Keeps the public API
// linkable so main.c and the platform build and run unchanged (LED dark).
void status_indicator_init(void) {}
void status_indicator_set(status_state_t s) { (void)s; }
