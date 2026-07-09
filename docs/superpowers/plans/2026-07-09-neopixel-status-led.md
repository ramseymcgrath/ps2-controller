# NeoPixel Status LED Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one external WS2812 status LED that shows adapter state (boot / searching / connected / error) and, while connected, an input-paced rainbow whose hue advances with controller activity.

**Architecture:** A pure, host-tested color/activity module (`status_color.c`) plus a firmware module (`status_indicator.c`) that claims one WS2812 state machine on `pio2` using the Pico SDK's own `ws2812.pio`, and renders at 50 Hz from a core0 repeating timer. All state writers are on core0; nothing touches `pio0` or core1, so the PS2 bus invariants are untouched.

**Tech Stack:** C11, Raspberry Pi Pico SDK 2.3.0 (RP2350), `hardware_pio` + `pico_time`, SDK `pico_status_led/ws2812.pio` program, Unity (host unit tests).

**Spec:** `docs/superpowers/specs/2026-07-08-neopixel-status-led-design.md`

## Global Constraints

- **Board:** `PICO_BOARD=pimoroni_pico_plus2_w_rp2350` (Pimoroni Pico Plus 2 W).
- **System clock:** stays 125 MHz (`SYS_CLOCK_KHZ 125000` in `main.c`); do not change it. `ws2812_program_init` derives its own clkdiv from `clk_sys`.
- **PIO ownership:** WS2812 lives on `pio2` only. Never place it on `pio0` (the PS2 bus / core1). Never call PIO functions for the LED from core1.
- **Reuse the SDK WS2812 program** (`${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio`) via `pico_generate_pio_header`; do NOT hand-write a WS2812 PIO program and do NOT link the high-level `pico_status_led` library.
- **Symbol naming:** public functions are `status_indicator_*` (not `status_led_*`, which are SDK symbols).
- **Brightness:** every color-returning function caps each channel at `STATUS_MAX_BRIGHTNESS` (48 of 255, ~19%).
- **Buttons are active-low** in `PSXInputState` (`0xFF` = none pressed; a cleared bit = pressed).
- **Host tests** are pure C (no Pico headers). Firmware modules are verified by cross-compiling the `.uf2`.

## Build & Test Commands

- **Host unit tests (fast loop):**
  ```bash
  cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure
  ```
- **Firmware cross-compile (slow; verifies firmware modules link):**
  ```bash
  PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build
  ```
  Expected artifact: `build/ps2_controller.uf2`.

## File Structure

- `src/status_led/status_color.h` — pure: `status_state_t` enum, constants, and pure-function declarations.
- `src/status_led/status_color.c` — pure: `input_activity`, `hue_to_rgb`, `status_color`. Host-compilable (includes only `input_state.h` + stdlib).
- `src/status_led/status_indicator.h` — firmware public API + `STATUS_LED_PIN`.
- `src/status_led/status_indicator.c` — firmware: WS2812 claim/init on `pio2`, 50 Hz render timer, state/activity globals, API.
- `test/test_status_color.c` — Unity tests for the pure module.
- `CMakeLists.txt` — `PICO_BOARD`, new sources, generate SDK ws2812 header, include dir.
- `test/CMakeLists.txt` — new test executable + include dir.

---

### Task 1: Switch PICO_BOARD to the Pimoroni Pico Plus 2 W

**Files:**
- Modify: `CMakeLists.txt:3`

**Interfaces:**
- Consumes: nothing.
- Produces: correct board provisioning (16 MB flash, 8 MB PSRAM, RP2350B) for all later firmware builds.

- [ ] **Step 1: Change the board type**

In `CMakeLists.txt`, replace line 3:

```cmake
set(PICO_BOARD pico2_w CACHE STRING "Board type")
```

with:

```cmake
set(PICO_BOARD pimoroni_pico_plus2_w_rp2350 CACHE STRING "Board type")
```

- [ ] **Step 2: Configure & build the firmware to verify the board is valid**

Run:
```bash
rm -rf build && PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build
```
Expected: configuration prints the board and completes; build produces `build/ps2_controller.uf2` with no errors. (A fresh `build/` avoids a stale cached `PICO_BOARD`.)

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: target Pimoroni Pico Plus 2 W (correct flash/PSRAM/package)"
```

---

### Task 2: Pure color / activity module (`status_color`) — TDD

**Files:**
- Create: `src/status_led/status_color.h`
- Create: `src/status_led/status_color.c`
- Create: `test/test_status_color.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `PSXInputState` from `src/input/input_state.h` (fields `lx,ly,rx,ry` neutral `0x80`; `buttons1,buttons2` active-low; `ds2_neutral_state()`).
- Produces (relied on by Task 3):
  - `typedef enum { STATUS_BOOT, STATUS_SEARCHING, STATUS_CONNECTED, STATUS_ERROR } status_state_t;`
  - `uint32_t input_activity(const PSXInputState *cur, const PSXInputState *prev);`
  - `uint32_t hue_to_rgb(uint8_t hue);` — returns `0xRRGGBB`, each channel ≤ `STATUS_MAX_BRIGHTNESS`.
  - `uint32_t status_color(status_state_t s, uint16_t phase, uint8_t hue);` — returns `0xRRGGBB`.
  - Constants: `STATUS_MAX_BRIGHTNESS`, `STATUS_BTN_WEIGHT`, `STATUS_RAINBOW_GAIN`, `STATUS_RAINBOW_SHIFT`.

- [ ] **Step 1: Write the header**

Create `src/status_led/status_color.h`:

```c
#ifndef STATUS_COLOR_H
#define STATUS_COLOR_H

#include <stdint.h>
#include "input_state.h"   // PSXInputState

// Public, settable indicator state.
typedef enum {
    STATUS_BOOT = 0,   // pre-BT init
    STATUS_SEARCHING,  // BT up, no gamepad
    STATUS_CONNECTED,  // gamepad paired
    STATUS_ERROR,      // fault
} status_state_t;

// Per-channel brightness cap (of 255). ~19%: a status LED at full white is
// ~60 mA and glaring. Every color function keeps each channel <= this.
#define STATUS_MAX_BRIGHTNESS 48u

// Activity weighting: each newly-pressed button contributes this much "activity"
// (comparable to ~1/4 of a full single-stick deflection of 512).
#define STATUS_BTN_WEIGHT 32u

// Rainbow speed: hue += (activity * GAIN) >> SHIFT per render tick.
#define STATUS_RAINBOW_GAIN  1u
#define STATUS_RAINBOW_SHIFT 6u

// Activity magnitude from one input update: stick deflection from center plus
// newly-pressed-button energy. Neutral sticks + no new presses -> 0.
uint32_t input_activity(const PSXInputState *cur, const PSXInputState *prev);

// HSV wheel at full saturation/value, scaled to STATUS_MAX_BRIGHTNESS.
// Returns 0xRRGGBB; no channel exceeds STATUS_MAX_BRIGHTNESS.
uint32_t hue_to_rgb(uint8_t hue);

// Full palette. `phase` is a free-running millisecond counter (used for
// breathing/blink timing); `hue` is the rainbow phase for CONNECTED.
// Returns 0xRRGGBB.
uint32_t status_color(status_state_t s, uint16_t phase, uint8_t hue);

#endif // STATUS_COLOR_H
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_status_color.c`:

```c
#include "unity/unity.h"
#include "status_color.h"
#include "input_state.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t R(uint32_t c) { return (c >> 16) & 0xFF; }
static uint8_t G(uint32_t c) { return (c >> 8) & 0xFF; }
static uint8_t B(uint32_t c) { return c & 0xFF; }
static int brightness(uint32_t c) { return R(c) + G(c) + B(c); }

// --- input_activity ---

static void test_activity_neutral_is_zero(void) {
    PSXInputState n = ds2_neutral_state();
    TEST_ASSERT_EQUAL_UINT32(0, input_activity(&n, &n));
}

static void test_activity_full_deflection(void) {
    PSXInputState n = ds2_neutral_state();
    PSXInputState cur = n;
    cur.lx = 0x00; cur.ly = 0x00; cur.rx = 0x00; cur.ry = 0x00; // 128*4
    TEST_ASSERT_EQUAL_UINT32(512, input_activity(&cur, &n));
}

static void test_activity_more_deflection_is_larger(void) {
    PSXInputState n = ds2_neutral_state();
    PSXInputState small = n, big = n;
    small.lx = 0x90;                 // |0x90-0x80| = 16
    big.lx   = 0xC0;                 // |0xC0-0x80| = 64
    TEST_ASSERT_TRUE(input_activity(&big, &n) > input_activity(&small, &n));
}

static void test_activity_fresh_press_adds_energy(void) {
    PSXInputState n = ds2_neutral_state();
    PSXInputState cur = n;
    cur.buttons2 = (uint8_t)~0x01;   // one button pressed (bit cleared)
    TEST_ASSERT_EQUAL_UINT32(STATUS_BTN_WEIGHT, input_activity(&cur, &n));
}

static void test_activity_held_button_does_not_readd(void) {
    PSXInputState prev = ds2_neutral_state();
    prev.buttons2 = (uint8_t)~0x01;  // already pressed last frame
    PSXInputState cur = prev;        // still pressed, no new edge
    TEST_ASSERT_EQUAL_UINT32(0, input_activity(&cur, &prev));
}

// --- hue_to_rgb ---

static void test_hue_zero_is_red(void) {
    uint32_t c = hue_to_rgb(0);
    TEST_ASSERT_EQUAL_UINT8(STATUS_MAX_BRIGHTNESS, R(c));
    TEST_ASSERT_EQUAL_UINT8(0, G(c));
    TEST_ASSERT_EQUAL_UINT8(0, B(c));
}

static void test_hue_never_exceeds_cap(void) {
    for (int h = 0; h < 256; h++) {
        uint32_t c = hue_to_rgb((uint8_t)h);
        TEST_ASSERT_TRUE(R(c) <= STATUS_MAX_BRIGHTNESS);
        TEST_ASSERT_TRUE(G(c) <= STATUS_MAX_BRIGHTNESS);
        TEST_ASSERT_TRUE(B(c) <= STATUS_MAX_BRIGHTNESS);
    }
}

// --- status_color ---

static void test_boot_is_off(void) {
    TEST_ASSERT_EQUAL_UINT32(0, status_color(STATUS_BOOT, 0, 0));
}

static void test_connected_is_hue(void) {
    TEST_ASSERT_EQUAL_UINT32(hue_to_rgb(100), status_color(STATUS_CONNECTED, 0, 100));
}

static void test_searching_breathes_monotonic_rising(void) {
    // phase>>3 gives the breathing counter; rising half is counter 0..127.
    int b0   = brightness(status_color(STATUS_SEARCHING, 0,        0));
    int bmid = brightness(status_color(STATUS_SEARCHING, 64 << 3,  0));
    int bhi  = brightness(status_color(STATUS_SEARCHING, 127 << 3, 0));
    TEST_ASSERT_TRUE(b0 < bmid);
    TEST_ASSERT_TRUE(bmid < bhi);
}

static void test_error_blinks_red(void) {
    uint32_t off = status_color(STATUS_ERROR, 0, 0);     // (0>>8)&1 == 0
    uint32_t on  = status_color(STATUS_ERROR, 256, 0);   // (256>>8)&1 == 1
    TEST_ASSERT_EQUAL_UINT32(0, off);
    TEST_ASSERT_EQUAL_UINT8(STATUS_MAX_BRIGHTNESS, R(on));
    TEST_ASSERT_EQUAL_UINT8(0, G(on));
    TEST_ASSERT_EQUAL_UINT8(0, B(on));
}

static void test_all_states_respect_cap(void) {
    status_state_t states[] = {STATUS_BOOT, STATUS_SEARCHING, STATUS_CONNECTED, STATUS_ERROR};
    for (unsigned s = 0; s < 4; s++)
        for (int p = 0; p < 2048; p += 7) {
            uint32_t c = status_color(states[s], (uint16_t)p, (uint8_t)p);
            TEST_ASSERT_TRUE(R(c) <= STATUS_MAX_BRIGHTNESS);
            TEST_ASSERT_TRUE(G(c) <= STATUS_MAX_BRIGHTNESS);
            TEST_ASSERT_TRUE(B(c) <= STATUS_MAX_BRIGHTNESS);
        }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_activity_neutral_is_zero);
    RUN_TEST(test_activity_full_deflection);
    RUN_TEST(test_activity_more_deflection_is_larger);
    RUN_TEST(test_activity_fresh_press_adds_energy);
    RUN_TEST(test_activity_held_button_does_not_readd);
    RUN_TEST(test_hue_zero_is_red);
    RUN_TEST(test_hue_never_exceeds_cap);
    RUN_TEST(test_boot_is_off);
    RUN_TEST(test_connected_is_hue);
    RUN_TEST(test_searching_breathes_monotonic_rising);
    RUN_TEST(test_error_blinks_red);
    RUN_TEST(test_all_states_respect_cap);
    return UNITY_END();
}
```

- [ ] **Step 3: Wire the test into CMake**

In `test/CMakeLists.txt`, add `../src/status_led` to the `include_directories(...)` block (after the `../src/input` line), then append:

```cmake
add_executable(test_status_color test_status_color.c ../src/status_led/status_color.c)
target_link_libraries(test_status_color unity)
add_test(NAME test_status_color COMMAND test_status_color)
```

- [ ] **Step 4: Run the tests to verify they fail**

Run:
```bash
cmake -S test -B test/build && cmake --build test/build
```
Expected: **compile/link failure** — `status_color.c` does not exist yet (undefined references to `input_activity`, `hue_to_rgb`, `status_color`).

- [ ] **Step 5: Write the implementation**

Create `src/status_led/status_color.c`:

```c
#include "status_color.h"
#include <stdlib.h>   // abs

// Amber for the "searching" pulse: full red + ~40% green.
#define AMBER_R STATUS_MAX_BRIGHTNESS
#define AMBER_G ((STATUS_MAX_BRIGHTNESS * 2u) / 5u)

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Scale an 8-bit channel by an 8-bit level (0..255).
static inline uint8_t scale(uint8_t ch, uint8_t level) {
    return (uint8_t)(((uint16_t)ch * level) / 255u);
}

uint32_t input_activity(const PSXInputState *cur, const PSXInputState *prev) {
    int deflection = abs((int)cur->lx - 0x80) + abs((int)cur->ly - 0x80)
                   + abs((int)cur->rx - 0x80) + abs((int)cur->ry - 0x80);
    // Active-low: newly pressed = bit was 1 (released) and is now 0 (pressed).
    uint8_t np1 = (uint8_t)(prev->buttons1 & ~cur->buttons1);
    uint8_t np2 = (uint8_t)(prev->buttons2 & ~cur->buttons2);
    int presses = __builtin_popcount(np1) + __builtin_popcount(np2);
    return (uint32_t)(deflection + presses * (int)STATUS_BTN_WEIGHT);
}

uint32_t hue_to_rgb(uint8_t hue) {
    uint8_t max = STATUS_MAX_BRIGHTNESS;
    uint8_t region = hue / 43u;                 // 0..5
    uint8_t up = scale(max, (uint8_t)((hue % 43u) * 6u));
    uint8_t down = (uint8_t)(max - up);
    switch (region) {
        case 0:  return rgb(max,  up,   0);
        case 1:  return rgb(down, max,  0);
        case 2:  return rgb(0,    max,  up);
        case 3:  return rgb(0,    down, max);
        case 4:  return rgb(up,   0,    max);
        default: return rgb(max,  0,    down);
    }
}

uint32_t status_color(status_state_t s, uint16_t phase, uint8_t hue) {
    switch (s) {
        case STATUS_CONNECTED:
            return hue_to_rgb(hue);
        case STATUS_SEARCHING: {
            uint8_t t = (uint8_t)((phase >> 3) & 0xFF);   // ~2 s period
            uint8_t tri = t < 128 ? (uint8_t)(t * 2) : (uint8_t)((255 - t) * 2);
            return rgb(scale(AMBER_R, tri), scale(AMBER_G, tri), 0);
        }
        case STATUS_ERROR:
            return ((phase >> 8) & 1u) ? rgb(STATUS_MAX_BRIGHTNESS, 0, 0)  // ~2 Hz
                                       : 0;
        case STATUS_BOOT:
        default:
            return 0;
    }
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
cmake --build test/build && ctest --test-dir test/build --output-on-failure
```
Expected: `test_status_color` PASSES (12 tests), and existing tests still pass.

- [ ] **Step 7: Commit**

```bash
git add src/status_led/status_color.h src/status_led/status_color.c test/test_status_color.c test/CMakeLists.txt
git commit -m "feat: pure status-LED color/activity module with host tests"
```

---

### Task 3: Firmware indicator module (`status_indicator`) on pio2

**Files:**
- Create: `src/status_led/status_indicator.h`
- Create: `src/status_led/status_indicator.c`
- Modify: `CMakeLists.txt` (add sources, generate SDK ws2812 header, include dir)

**Interfaces:**
- Consumes: `status_color.h` (`status_state_t`, `status_color`, `input_activity`, `STATUS_RAINBOW_*`), `input_state.h` (`PSXInputState`, `ds2_neutral_state`), and the generated `ws2812.pio.h` (`ws2812_program`, `ws2812_program_init`).
- Produces (relied on by Task 4):
  - `void status_indicator_init(void);`
  - `void status_indicator_set(status_state_t s);`
  - `void status_indicator_note_input(const PSXInputState *s);`
  - `STATUS_LED_PIN` (default `16`).

There is no host unit test for this task (it is hardware I/O); it is verified by cross-compiling the firmware. Real WS2812 output is validated in `docs/bringup.md`.

- [ ] **Step 1: Write the header**

Create `src/status_led/status_indicator.h`:

```c
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
```

- [ ] **Step 2: Write the implementation**

Create `src/status_led/status_indicator.c`:

```c
#include "status_indicator.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/time.h"       // repeating_timer, add_repeating_timer_ms
#include "hardware/pio.h"

#include "ws2812.pio.h"      // SDK program: ws2812_program, ws2812_program_init

#include "status_color.h"

#define STATUS_PIO   pio2
#define WS2812_FREQ  800000

static PIO  s_pio = STATUS_PIO;
static uint s_sm;
static bool s_enabled = false;

static volatile status_state_t s_state = STATUS_BOOT;
static volatile uint32_t s_pending_activity = 0;  // note_input (thread) -> render (IRQ)
static PSXInputState s_prev_input;                // core0 note_input only
static uint8_t s_hue = 0;                         // render tick only

// Pack 0xRRGGBB into the WS2812 wire order (GRB, MSB-first) and push it. The
// SDK ws2812 program left-shifts 24 bits out of the top of each 32-bit word.
static inline void ws2812_put(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    if (!pio_sm_is_tx_fifo_full(s_pio, s_sm))
        pio_sm_put(s_pio, s_sm, grb << 8u);
}

static bool render_cb(repeating_timer_t *t) {
    (void)t;
    if (!s_enabled)
        return true;

    status_state_t st = s_state;
    if (st == STATUS_CONNECTED) {
        uint32_t act = s_pending_activity;
        s_pending_activity -= act;   // subtract consumed (keeps concurrent adds)
        s_hue = (uint8_t)(s_hue +
            (uint8_t)((act * STATUS_RAINBOW_GAIN) >> STATUS_RAINBOW_SHIFT));
    }

    uint16_t phase = (uint16_t)(to_ms_since_boot(get_absolute_time()) & 0xFFFF);
    ws2812_put(status_color(st, phase, s_hue));
    return true;
}

void status_indicator_init(void) {
    s_prev_input = ds2_neutral_state();

    if (!pio_can_add_program(s_pio, &ws2812_program)) {
        printf("status_led: no pio2 program room; LED disabled\n");
        return;
    }
    uint off = pio_add_program(s_pio, &ws2812_program);

    int sm = pio_claim_unused_sm(s_pio, false);  // false: don't panic on failure
    if (sm < 0) {
        printf("status_led: no free pio2 SM; LED disabled\n");
        return;
    }
    s_sm = (uint)sm;
    ws2812_program_init(s_pio, s_sm, off, STATUS_LED_PIN, WS2812_FREQ, false);

    static repeating_timer_t timer;
    if (!add_repeating_timer_ms(-20, render_cb, NULL, &timer)) {  // 50 Hz
        printf("status_led: timer start failed; LED disabled\n");
        return;
    }
    s_enabled = true;
}

void status_indicator_set(status_state_t s) {
    s_state = s;
}

void status_indicator_note_input(const PSXInputState *s) {
    s_pending_activity += input_activity(s, &s_prev_input);
    s_prev_input = *s;
}
```

- [ ] **Step 3: Wire the module into the firmware CMake**

In `CMakeLists.txt`:

(a) Add the two sources to `add_executable(ps2_controller ...)` (after the `src/ps2_device/ps2_device.c` line):
```cmake
    src/status_led/status_color.c
    src/status_led/status_indicator.c
```

(b) After the existing `pico_generate_pio_header(ps2_controller ...psxSPI.pio)` block, add:
```cmake
# Reuse the SDK's WS2812 program for the status LED (ws2812_program + init).
pico_generate_pio_header(ps2_controller
    ${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio)
```

(c) Add the module dir to `target_include_directories(ps2_controller PRIVATE ...)`:
```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/src/status_led
```

(d) Ensure the timer/clock libs are linked (explicit; `hardware_pio` is already
present, the others are otherwise transitive via `pico_stdlib`). Add to
`target_link_libraries(ps2_controller PUBLIC ...)`:
```cmake
    hardware_timer
    hardware_clocks
```

- [ ] **Step 4: Cross-compile to verify the module builds & links**

Run:
```bash
PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build
```
Expected: builds `build/ps2_controller.uf2` with no errors. (`status_indicator_*` are defined but not yet called — that is fine; they link into the image.)

- [ ] **Step 5: Commit**

```bash
git add src/status_led/status_indicator.h src/status_led/status_indicator.c CMakeLists.txt
git commit -m "feat: WS2812 status indicator on pio2 (SDK ws2812 program, 50 Hz timer)"
```

---

### Task 4: Wire the indicator into startup and the BluePad32 platform

**Files:**
- Modify: `src/main.c` (init early; error on cyw43 failure)
- Modify: `src/input/bluepad32_platform.c` (state transitions + activity feed)

**Interfaces:**
- Consumes: `status_indicator_init/set/note_input` and `status_state_t` values from Task 3.
- Produces: a fully driven indicator (end-to-end feature).

Verified by cross-compiling the firmware and by the host test suite (unchanged, still green). Runtime LED behavior is validated in `docs/bringup.md`.

- [ ] **Step 1: Initialize the indicator early in `main.c` and show errors**

In `src/main.c`, add the include near the others (after `#include "ps2_transport.h"`):
```c
#include "status_indicator.h"
```

Then change the body so init happens right after `stdio_init_all()` and the cyw43 failure path shows the error. Replace:
```c
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();

    // Enables Bluetooth too (CYW43_ENABLE_BLUETOOTH). Must precede uni_init().
    if (cyw43_arch_init()) {
        loge("failed to initialise cyw43_arch\n");
        return -1;
    }
```
with:
```c
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();

    // Bring the status LED up first (needs only the clock + pio2). Its render
    // timer runs on the SDK alarm pool, so it works even if BT init fails below.
    status_indicator_init();

    // Enables Bluetooth too (CYW43_ENABLE_BLUETOOTH). Must precede uni_init().
    if (cyw43_arch_init()) {
        loge("failed to initialise cyw43_arch\n");
        status_indicator_set(STATUS_ERROR);
        while (true)
            tight_loop_contents();   // keep the render timer blinking red
    }
```

- [ ] **Step 2: Drive state transitions from the BluePad32 platform**

In `src/input/bluepad32_platform.c`, add the include (after `#include "ps2_device.h"`):
```c
#include "status_indicator.h"
```

In `ps2_platform_on_init_complete`, after `uni_bt_del_keys_unsafe();`, add:
```c
    status_indicator_set(STATUS_SEARCHING);
```

In `ps2_platform_on_device_disconnected`, after `ps2_device_stop();`, add:
```c
    status_indicator_set(STATUS_SEARCHING);
```

In `ps2_platform_on_device_ready`, after `ps2_device_start();` (before `return UNI_ERROR_SUCCESS;`), add:
```c
    status_indicator_set(STATUS_CONNECTED);
```

In `ps2_platform_on_controller_data`, after `shared_input_publish(&st);`, add:
```c
    status_indicator_note_input(&st);
```

- [ ] **Step 3: Cross-compile the firmware**

Run:
```bash
PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build
```
Expected: builds `build/ps2_controller.uf2` with no errors.

- [ ] **Step 4: Run the host test suite (regression check)**

Run:
```bash
ctest --test-dir test/build --output-on-failure
```
Expected: all tests pass (`test_smoke`, `test_ds2_protocol`, `test_gamepad_map`, `test_status_color`).

- [ ] **Step 5: Commit**

```bash
git add src/main.c src/input/bluepad32_platform.c
git commit -m "feat: drive status LED from startup and BluePad32 lifecycle"
```

---

### Task 5: Document the LED in bring-up notes

**Files:**
- Modify: `docs/bringup.md`

**Interfaces:**
- Consumes: the finished feature.
- Produces: a manual hardware-validation checklist (there is no automated test for real WS2812 output).

- [ ] **Step 1: Append a status-LED section to `docs/bringup.md`**

Add:
```markdown
## Status LED (WS2812 on GP16)

Wire a WS2812/WS2812B/SK6812 data-in to GP16 (5 V pixels need a level shifter),
common ground, and appropriate supply. Then verify:

- [ ] At power-on before BT is up: LED off (BOOT).
- [ ] After BT init: slow amber breathing (SEARCHING).
- [ ] Force a cyw43 init failure (e.g. no RM2 module): steady ~2 Hz red blink (ERROR).
- [ ] Connect a controller: LED leaves amber; parks on a static color when idle (CONNECTED).
- [ ] Move sticks / mash buttons: hue flows (rainbow); faster with more movement, parks when hands off.
- [ ] Confirm the PS2 side still works (LED shares no resources with pio0/core1):
      controller input reaches the console unchanged.
```

- [ ] **Step 2: Commit**

```bash
git add docs/bringup.md
git commit -m "docs: status-LED hardware bring-up checklist"
```

---

## Self-Review Notes

- **Spec coverage:** board config (T1); pure `input_activity`/`hue_to_rgb`/`status_color` + tests (T2); SDK ws2812 reuse, pio2 placement, 50 Hz timer, best-effort init, brightness cap (T2/T3); early init + error path, connect/disconnect/activity wiring (T4); bring-up checklist (T5). SEL/PS2_ACTIVE deliberately absent (spec non-goal).
- **Types consistent:** `status_state_t` and all three public signatures match across `status_color.h`, `status_indicator.h`, and the call sites.
- **No placeholders:** every code and command step is concrete.
