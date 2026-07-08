#ifndef BLUEPAD32_PLATFORM_H
#define BLUEPAD32_PLATFORM_H
#include <stdbool.h>

// BluePad32 "custom platform" entry point. main() passes the returned pointer
// to uni_platform_set_custom() before uni_init(). Forward-declared so callers
// (e.g. main.c) don't need to pull in all of <uni.h>.
struct uni_platform;
struct uni_platform *get_ps2_platform(void);

// True once a controller has connected and become ready. Backed by
// shared_input_connected(); the lifecycle in main.c uses it to launch/stop
// the core1 PS2 loop.
bool bp_controller_connected(void);

#endif // BLUEPAD32_PLATFORM_H
