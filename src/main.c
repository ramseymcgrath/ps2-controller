// PS2 DualShock 2 Bluetooth adapter — entry point.
//
// core0: BluePad32 (BTstack + cyw43) receives the Bluetooth controller and
// publishes mapped state via shared_input. The core1 PS2 poll-response loop
// and connect/disconnect lifecycle are added in Tasks 13-14; for now this
// brings up Bluetooth input on core0.

#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "sdkconfig.h"
#include "bluepad32_platform.h"

// Sanity check: the Pico W platform must be built as a custom platform.
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

int main(void) {
    stdio_init_all();

    if (cyw43_arch_init()) {
        loge("failed to initialise cyw43_arch\n");
        return -1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    uni_platform_set_custom(get_ps2_platform());
    uni_init(0, NULL);

    // BluePad32 owns the run loop on core0; does not return.
    btstack_run_loop_execute();
    return 0;
}
