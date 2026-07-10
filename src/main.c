// PS2 DualShock 2 Bluetooth adapter — entry point.
//
// core0: BluePad32 (BTstack + cyw43) receives the Bluetooth controller and maps
// it into the shared PSXInputState. On controller connect/disconnect the
// platform callbacks bring the PS2 device side (core1 + SEL IRQ) up and down.
// core1: the PS2 controller-port protocol loop (ps2_device_thread).

#include <btstack_run_loop.h>
#include <hardware/clocks.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "sdkconfig.h"
#include "bluepad32_platform.h"
#include "ps2_device.h"
#include "status_indicator.h"

// Sanity check: the Pico W platform must be built as a custom platform.
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

// 125 MHz system clock so psxSPI.pio's SLOW_CLKDIV 50 yields exactly 2.5 MHz
// (125/50). That rate sets the dat_writer's ACK-pulse width (~2 µs via the [5]
// side-set delays); other clocks would mis-time ACK. See docs/wiring.md.
#define SYS_CLOCK_KHZ 125000

int main(void) {
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();

    // Bring the status LED up first (needs only the clock + i2c0). Its render
    // timer runs on the SDK alarm pool, so it works even if BT init fails below.
    status_indicator_init();

    // Enables Bluetooth too (CYW43_ENABLE_BLUETOOTH). Must precede uni_init().
    if (cyw43_arch_init()) {
        loge("failed to initialise cyw43_arch\n");
        status_indicator_set(STATUS_ERROR);
        // Spin rather than return: the core0 alarm-pool timer keeps blinking the
        // red error state. Assumes no watchdog is enabled (SDK default), else this
        // would reset-loop.
        while (true)
            tight_loop_contents();
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // Init both port transports (pio0 GP6-10, pio1 GP11-15) after cyw43 has taken
    // its own PIO resources, and launch the single core1 PS2 loop once.
    ps2_device_global_init();

    uni_platform_set_custom(get_ps2_platform());
    uni_init(0, NULL);

    // BluePad32 owns the run loop on core0; does not return. core1 was already
    // launched by ps2_device_global_init(); the platform's connect/disconnect
    // callbacks only flip each port's active flag (ps2_device_start/stop).
    btstack_run_loop_execute();
    return 0;
}
