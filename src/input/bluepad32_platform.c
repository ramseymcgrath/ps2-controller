// BluePad32 "custom platform" for the PS2 adapter. Runs on core0 (the BT
// thread): receives controller reports, maps them into a PSXInputState, and
// publishes to the cross-core seqlock that core1's PS2 loop reads.
//
// Modeled on external/bluepad32/examples/pico_w/src/my_platform.c (Public
// Domain); demo rumble/LED code removed.

#include <stddef.h>

#include <pico/cyw43_arch.h>
#include <uni.h>

#include "sdkconfig.h"

#include "gamepad_map.h"
#include "shared_input.h"
#include "bluepad32_platform.h"
#include "ps2_device.h"
#include "port_router.h"
#include "status_indicator.h"

// Sanity check: the Pico W platform must be built as a custom platform.
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

// The snapshot masks in gamepad_map.h are copied straight from uni_gamepad_t,
// so they must equal BluePad32's own bit values. If a future BluePad32 bump
// renumbers an enum, these fail the build instead of silently mis-mapping.
_Static_assert(BP_DPAD_UP == DPAD_UP,       "dpad up mask drift");
_Static_assert(BP_DPAD_DOWN == DPAD_DOWN,   "dpad down mask drift");
_Static_assert(BP_DPAD_LEFT == DPAD_LEFT,   "dpad left mask drift");
_Static_assert(BP_DPAD_RIGHT == DPAD_RIGHT, "dpad right mask drift");
_Static_assert(BP_BTN_A == BUTTON_A,        "cross mask drift");
_Static_assert(BP_BTN_B == BUTTON_B,        "circle mask drift");
_Static_assert(BP_BTN_X == BUTTON_X,        "square mask drift");
_Static_assert(BP_BTN_Y == BUTTON_Y,        "triangle mask drift");
_Static_assert(BP_BTN_L1 == BUTTON_SHOULDER_L, "L1 mask drift");
_Static_assert(BP_BTN_R1 == BUTTON_SHOULDER_R, "R1 mask drift");
_Static_assert(BP_BTN_L3 == BUTTON_THUMB_L, "L3 mask drift");
_Static_assert(BP_BTN_R3 == BUTTON_THUMB_R, "R3 mask drift");
_Static_assert(BP_MISC_SELECT == MISC_BUTTON_SELECT, "select mask drift");
_Static_assert(BP_MISC_START == MISC_BUTTON_START,   "start mask drift");

// Maps each connected Bluetooth controller to a PS2 port (connection order).
static port_router_t s_router;

//
// Platform overrides
//
static void ps2_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shared_input_init();
    port_router_init(&s_router);
    logi("ps2_platform: init()\n");
}

static void ps2_platform_on_init_complete(void) {
    logi("ps2_platform: on_init_complete()\n");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_del_keys_unsafe();
    status_indicator_set(STATUS_SEARCHING);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t ps2_platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    // Plain casts (not BluePad32's ARG_UNUSED, which does sizeof(x)) because
    // `addr` is an array-typed parameter and sizeof on it warns.
    (void)addr;
    (void)name;
    (void)cod;
    (void)rssi;
    return UNI_ERROR_SUCCESS;
}

static void ps2_platform_on_device_connected(uni_hid_device_t* d) {
    logi("ps2_platform: device connected: %p\n", d);
}

static void ps2_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("ps2_platform: device disconnected: %p\n", d);
    int port = port_router_lookup(&s_router, d);
    if (port == PORT_NONE)
        return;                                  // never assigned (both ports were full)
    shared_input_set_connected((unsigned)port, false);
    ps2_device_stop((unsigned)port);             // take this port offline, publish neutral
    port_router_release(&s_router, d);
    // LED reflects the aggregate connected-port count.
    unsigned n = 0;
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++)
        if (shared_input_connected(p)) n++;
    status_indicator_set(n == 0 ? STATUS_SEARCHING
                       : n >= 2 ? STATUS_CONNECTED_2P
                                : STATUS_CONNECTED_1P);
}

static uni_error_t ps2_platform_on_device_ready(uni_hid_device_t* d) {
    logi("ps2_platform: device ready: %p\n", d);
    int port = port_router_assign(&s_router, d);
    if (port == PORT_NONE) {
        logi("ps2_platform: both ports in use; ignoring extra controller\n");
        return UNI_ERROR_SUCCESS;
    }
    shared_input_set_connected((unsigned)port, true);
    ps2_device_start((unsigned)port);            // bring this port online
    unsigned n = 0;
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++)
        if (shared_input_connected(p)) n++;
    status_indicator_set(n >= 2 ? STATUS_CONNECTED_2P : STATUS_CONNECTED_1P);
    return UNI_ERROR_SUCCESS;
}

static void ps2_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;
    int port = port_router_lookup(&s_router, d);
    if (port == PORT_NONE)
        return;                                  // data from an unassigned controller

    const uni_gamepad_t* gp = &ctl->gamepad;
    gamepad_snapshot_t snap = {
        .dpad         = gp->dpad,
        .buttons      = gp->buttons,
        .misc_buttons = gp->misc_buttons,
        .axis_x       = gp->axis_x,
        .axis_y       = gp->axis_y,
        .axis_rx      = gp->axis_rx,
        .axis_ry      = gp->axis_ry,
        .brake        = gp->brake,
        .throttle     = gp->throttle,
    };

    PSXInputState st;
    map_gamepad_to_psx(&snap, &st);
    shared_input_publish((unsigned)port, &st);
}

static const uni_property_t* ps2_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void ps2_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    ARG_UNUSED(event);
    ARG_UNUSED(data);
}

//
// Entry point
//
struct uni_platform* get_ps2_platform(void) {
    static struct uni_platform plat = {
        .name = "PS2 DualShock 2 adapter",
        .init = ps2_platform_init,
        .on_init_complete = ps2_platform_on_init_complete,
        .on_device_discovered = ps2_platform_on_device_discovered,
        .on_device_connected = ps2_platform_on_device_connected,
        .on_device_disconnected = ps2_platform_on_device_disconnected,
        .on_device_ready = ps2_platform_on_device_ready,
        .on_oob_event = ps2_platform_on_oob_event,
        .on_controller_data = ps2_platform_on_controller_data,
        .get_property = ps2_platform_get_property,
    };
    return &plat;
}

bool bp_controller_connected(void) {
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++)
        if (shared_input_connected(p))
            return true;
    return false;
}
