# LED Matrix Status Display — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the WS2812 NeoPixel status indicator with an Adafruit 8×8 bicolor LED matrix (HT16K33) on hardware I2C + DMA, showing iconographic status (searching orbit / green player-count digit / red error X).

**Architecture:** A pure `matrix_render` layer composes a 16-byte HT16K33 frame from a status enum (host-tested); a `matrix_driver` pushes frames over `i2c0` via DMA; `status_indicator` keeps its existing public API and drives them on a 50 Hz timer. The WS2812/`pio2` path is deleted; both PS2 ports shift up one GPIO to free GP4/GP5 for the matrix.

**Tech Stack:** RP2350 / Pimoroni Pico Plus 2 W, Pico SDK 2.3.0 (C11), `hardware_i2c` + `hardware_dma`, Unity host tests, font8x8 glyph data.

## Global Constraints

- Pico SDK 2.x (2.3.0); build with `PICO_SDK_PATH=$HOME/pico-sdk`; board `pimoroni_pico_plus2_w_rp2350` (set in `CMakeLists.txt`).
- **System clock stays 125 MHz** (PS2 ACK timing depends on it). This feature never touches `set_sys_clock_khz`.
- **core1 remains the sole owner of the PS2 PIO.** The only PS2 change here is two `pin_base` constants; no PS2 runtime logic changes.
- Matrix bus: **`i2c0`, SDA = GP4, SCL = GP5, 400000 baud, HT16K33 addr `0x70`, brightness `0x0F`.**
- PS2 ports after the shift: **port 0 `pin_base = 6` (GP6–10), port 1 `pin_base = 11` (GP11–15).** DAT=base, CMD=+1, SEL=+2, CLK=+3, ACK=+4.
- Panel column-bit fixup is `(x + 7) & 7` — a single bench-tunable point (hardware open-item #1).
- Glyph bytes (row-major, LSB = leftmost column):
  - `GLYPH_1 = {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}` (font8x8 "1")
  - `GLYPH_2 = {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}` (font8x8 "2")
  - `GLYPH_X = {0x41,0x22,0x14,0x08,0x14,0x22,0x41,0x00}` (built-in error cross)
- Firmware build: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build` (redirect output to a file; it is thousands of lines).
- Host tests: `cmake -S test -B test/build && ctest --test-dir test/build --output-on-failure`.
- Conventional commits; end every commit message with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

### Task 1: Shift both PS2 ports up one GPIO

Frees GP4/GP5 for the matrix. Pure config change — verified by build + the unchanged host suite (no PS2 pin has a host unit test).

**Files:**
- Modify: `src/ps2_device/ps2_device.c` (the two `ps2_transport_init` calls)
- Modify: `src/main.c` (the pin comment near `ps2_device_global_init`)
- Modify: `docs/wiring.md` (port 0 and port 1 GPIO tables)
- Modify: `docs/bringup.md` ("Two controllers" pin reference)

**Interfaces:**
- Consumes: `ps2_transport_init(ps2_transport_t*, PIO, uint pin_base)` (unchanged signature).
- Produces: nothing new; downstream tasks assume GP4/GP5 are free.

- [ ] **Step 1: Move the pin bases.** In `src/ps2_device/ps2_device.c`, `ps2_device_global_init()`, change:

```c
    ps2_transport_init(&s_transport[0], pio0, 5);
    ps2_transport_init(&s_transport[1], pio1, 10);
```

to:

```c
    ps2_transport_init(&s_transport[0], pio0, 6);   // DAT=GP6 CMD=7 SEL=8 CLK=9 ACK=10
    ps2_transport_init(&s_transport[1], pio1, 11);  // DAT=GP11 CMD=12 SEL=13 CLK=14 ACK=15
```

- [ ] **Step 2: Fix the main.c comment.** In `src/main.c`, change the comment `// Init both port transports (pio0 GP5-9, pio1 GP10-14) after cyw43 has taken` to `// Init both port transports (pio0 GP6-10, pio1 GP11-15) after cyw43 has taken`.

- [ ] **Step 3: Update `docs/wiring.md`.** In the port 0 "GPIO map" table, change the Pico GPIO cells DAT→**GP6**, CMD→**GP7**, ATT/SEL→**GP8**, CLK→**GP9**, ACK→**GP10**. In the "Port 1 (second controller)" table, change DAT→GP11, CMD→GP12, ATT/SEL→GP13, CLK→GP14, ACK→GP15. Add one line under the port 0 table:

```
> GP4 (SDA) and GP5 (SCL) are reserved for the status-LED matrix on the board's
> STEMMA QT connector (`i2c0`); PS2 port 0 therefore starts at GP6.
```

- [ ] **Step 4: Update `docs/bringup.md`.** In the "Two controllers (dual port)" section, change the two references to `GP10–14` to `GP11–15`.

- [ ] **Step 5: Build firmware and run host tests.**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build >/tmp/m_build.log 2>&1 && cmake --build build >>/tmp/m_build.log 2>&1; tail -n 1 /tmp/m_build.log; grep -iE 'error:' /tmp/m_build.log || echo NO_ERRORS`
Expected: build succeeds, `NO_ERRORS`.
Run: `cmake -S test -B test/build >/dev/null && ctest --test-dir test/build --output-on-failure`
Expected: all suites PASS (5/5, unchanged).

- [ ] **Step 6: Commit.**

```bash
git add src/ps2_device/ps2_device.c src/main.c docs/wiring.md docs/bringup.md
git commit -m "feat: shift PS2 ports +1 GPIO to free GP4/GP5 for the LED matrix

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Remove the WS2812 path; add the status enum; stub the indicator

Excise the NeoPixel, introduce the expanded state enum in its own header, reduce `status_indicator` to a linkable no-op (the matrix backend arrives in Task 5), and rewire `main.c`/the platform to the new enum with `note_input` dropped. After this task the firmware builds and runs with a dark LED.

**Files:**
- Create: `src/status_led/status_state.h`
- Modify: `src/status_led/status_indicator.h`, `src/status_led/status_indicator.c`
- Delete: `src/status_led/status_color.h`, `src/status_led/status_color.c`, `test/test_status_color.c`
- Modify: `src/input/bluepad32_platform.c`, `src/main.c`
- Modify: `CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Produces: `status_state_t {STATUS_BOOT, STATUS_SEARCHING, STATUS_CONNECTED_1P, STATUS_CONNECTED_2P, STATUS_ERROR}` in `status_state.h`; `status_indicator_init(void)` and `status_indicator_set(status_state_t)` in `status_indicator.h` (no `note_input`).
- Consumes: `shared_input_connected(unsigned port)` (existing), `PS2_NUM_PORTS` (existing).

- [ ] **Step 1: Create `src/status_led/status_state.h`.**

```c
#ifndef STATUS_STATE_H
#define STATUS_STATE_H

// Public status vocabulary for the LED indicator. The player count is encoded
// in the state (CONNECTED_1P / _2P) so the render layer needs no separate arg.
typedef enum {
    STATUS_BOOT = 0,       // pre-BT init
    STATUS_SEARCHING,      // BT up, no player connected (animated)
    STATUS_CONNECTED_1P,   // exactly one port connected
    STATUS_CONNECTED_2P,   // both ports connected
    STATUS_ERROR,          // fault (blinking)
} status_state_t;

#endif // STATUS_STATE_H
```

- [ ] **Step 2: Replace `src/status_led/status_indicator.h`** with the slimmed API (no `note_input`, no `input_state.h`, no `status_color.h`):

```c
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
```

- [ ] **Step 3: Replace `src/status_led/status_indicator.c`** with a no-op stub (the matrix backend is wired in Task 5):

```c
#include "status_indicator.h"

// Stub: the matrix backend is wired in a later task. Keeps the public API
// linkable so main.c and the platform build and run unchanged (LED dark).
void status_indicator_init(void) {}
void status_indicator_set(status_state_t s) { (void)s; }
```

- [ ] **Step 4: Delete the WS2812 module and its test.**

```bash
git rm src/status_led/status_color.h src/status_led/status_color.c test/test_status_color.c
```

- [ ] **Step 5: Rewire `src/input/bluepad32_platform.c`.**
  - Remove the `status_indicator_note_input(&st);` line in `ps2_platform_on_controller_data`.
  - In `ps2_platform_on_device_ready`, replace `status_indicator_set(STATUS_CONNECTED);` with a count-based set:

```c
    unsigned n = 0;
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++)
        if (shared_input_connected(p)) n++;
    status_indicator_set(n >= 2 ? STATUS_CONNECTED_2P : STATUS_CONNECTED_1P);
```

  - In `ps2_platform_on_device_disconnected`, replace the `bool any = …; if (!any) status_indicator_set(STATUS_SEARCHING);` tail with:

```c
    unsigned n = 0;
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++)
        if (shared_input_connected(p)) n++;
    status_indicator_set(n == 0 ? STATUS_SEARCHING
                       : n >= 2 ? STATUS_CONNECTED_2P
                                : STATUS_CONNECTED_1P);
```

- [ ] **Step 6: Fix the `main.c` LED comment.** Change `// Bring the status LED up first (needs only the clock + pio2). Its render` to `// Bring the status LED up first (needs only the clock + i2c0). Its render`.

- [ ] **Step 7: Update `CMakeLists.txt`.** In `add_executable(ps2_controller …)` remove the line `src/status_led/status_color.c`. Delete the WS2812 PIO generation block:

```cmake
# Reuse the SDK's WS2812 program for the status LED (ws2812_program + init).
pico_generate_pio_header(ps2_controller
    ${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio)
```

- [ ] **Step 8: Update `test/CMakeLists.txt`.** Remove the `test_status_color` block:

```cmake
add_executable(test_status_color test_status_color.c ../src/status_led/status_color.c)
target_link_libraries(test_status_color unity)
add_test(NAME test_status_color COMMAND test_status_color)
```

- [ ] **Step 9: Build + host tests.**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build >/tmp/m_build.log 2>&1 && cmake --build build >>/tmp/m_build.log 2>&1; tail -n 1 /tmp/m_build.log; grep -iE 'error:' /tmp/m_build.log || echo NO_ERRORS`
Expected: build succeeds, `NO_ERRORS`.
Run: `rm -rf test/build && cmake -S test -B test/build >/dev/null && ctest --test-dir test/build --output-on-failure`
Expected: 4 suites PASS (smoke, ds2_protocol, gamepad_map, port_router); `test_status_color` is gone.

- [ ] **Step 10: Commit.**

```bash
git add -A
git commit -m "refactor: remove WS2812 LED; add status enum + no-op indicator stub

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Pure matrix render layer + host tests (TDD)

The frame-composition logic — glyph plotting, plane packing, the column fixup, the searching orbit, the error blink — with full host coverage. No hardware.

**Files:**
- Create: `src/matrix/matrix_glyphs.h`, `src/matrix/matrix_render.h`, `src/matrix/matrix_render.c`
- Create: `test/test_matrix_render.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `status_state_t` (from `status_state.h`).
- Produces: `matrix_clear`, `matrix_set_pixel`, `matrix_get_pixel`, `matrix_render_frame`, `MATRIX_FRAME_BYTES`, `MATRIX_OFF/GREEN/RED/YELLOW` (in `matrix_render.h`) — consumed by the driver-wiring task.

- [ ] **Step 1: Create the glyph header `src/matrix/matrix_glyphs.h`.**

```c
#ifndef MATRIX_GLYPHS_H
#define MATRIX_GLYPHS_H

#include <stdint.h>

// 8x8 glyph bitmaps: 8 bytes, one per row (y=0 top). Bit b of a row byte is
// column x=b, i.e. LSB = leftmost column (font8x8 convention).
//
// Digits are from font8x8 by Daniel Hepper (public domain,
// https://github.com/dhepper/font8x8); only the digits the display can show
// (1 and 2 players) are vendored. The error "X" is a built-in symmetric cross.
static const uint8_t GLYPH_1[8] = {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00};
static const uint8_t GLYPH_2[8] = {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00};
static const uint8_t GLYPH_X[8] = {0x41, 0x22, 0x14, 0x08, 0x14, 0x22, 0x41, 0x00};

#endif // MATRIX_GLYPHS_H
```

- [ ] **Step 2: Create the render header `src/matrix/matrix_render.h`.**

```c
#ifndef MATRIX_RENDER_H
#define MATRIX_RENDER_H

#include <stdint.h>
#include "status_state.h"

#define MATRIX_FRAME_BYTES 16u

enum { MATRIX_OFF = 0, MATRIX_GREEN = 1, MATRIX_RED = 2, MATRIX_YELLOW = 3 };

// Zero all 16 bytes (both color planes).
void matrix_clear(uint8_t frame[MATRIX_FRAME_BYTES]);

// Set pixel (x,y), 0..7, to color (0=off,1=green,2=red,3=yellow). Off clears
// both planes. Out-of-range is ignored. Applies the panel column-bit fixup.
void matrix_set_pixel(uint8_t frame[MATRIX_FRAME_BYTES], unsigned x, unsigned y, unsigned color);

// Read back the color at (x,y). Out-of-range returns MATRIX_OFF.
unsigned matrix_get_pixel(const uint8_t frame[MATRIX_FRAME_BYTES], unsigned x, unsigned y);

// Compose the full frame for a status state. `phase` is a free-running
// millisecond counter driving the searching orbit and the error blink.
void matrix_render_frame(status_state_t s, uint16_t phase, uint8_t frame[MATRIX_FRAME_BYTES]);

#endif // MATRIX_RENDER_H
```

- [ ] **Step 3: Write the failing test `test/test_matrix_render.c`.**

```c
#include "unity/unity.h"
#include "matrix_render.h"
#include "matrix_glyphs.h"

void setUp(void) {}
void tearDown(void) {}

static unsigned count_lit(const uint8_t f[16]) {
    unsigned n = 0;
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 8; x++)
            if (matrix_get_pixel(f, x, y) != MATRIX_OFF) n++;
    return n;
}

static void assert_glyph(const uint8_t f[16], const uint8_t g[8], unsigned color) {
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 8; x++) {
            unsigned want = (g[y] & (1u << x)) ? color : MATRIX_OFF;
            TEST_ASSERT_EQUAL_UINT(want, matrix_get_pixel(f, x, y));
        }
}

static void test_clear_all_off(void) {
    uint8_t f[16]; for (int i = 0; i < 16; i++) f[i] = 0xAB;
    matrix_clear(f);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_UINT8(0, f[i]);
}

static void test_set_get_colors(void) {
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 1, 2, MATRIX_GREEN);
    matrix_set_pixel(f, 3, 4, MATRIX_RED);
    matrix_set_pixel(f, 5, 6, MATRIX_YELLOW);
    TEST_ASSERT_EQUAL_UINT(MATRIX_GREEN,  matrix_get_pixel(f, 1, 2));
    TEST_ASSERT_EQUAL_UINT(MATRIX_RED,    matrix_get_pixel(f, 3, 4));
    TEST_ASSERT_EQUAL_UINT(MATRIX_YELLOW, matrix_get_pixel(f, 5, 6));
    TEST_ASSERT_EQUAL_UINT(MATRIX_OFF,    matrix_get_pixel(f, 0, 0));
}

static void test_set_off_clears(void) {
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 2, 2, MATRIX_YELLOW);
    matrix_set_pixel(f, 2, 2, MATRIX_OFF);
    TEST_ASSERT_EQUAL_UINT(MATRIX_OFF, matrix_get_pixel(f, 2, 2));
}

static void test_out_of_range_noop(void) {
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 8, 0, MATRIX_GREEN);
    matrix_set_pixel(f, 0, 8, MATRIX_GREEN);
    TEST_ASSERT_EQUAL_UINT(0, count_lit(f));
    TEST_ASSERT_EQUAL_UINT(MATRIX_OFF, matrix_get_pixel(f, 8, 0));
}

static void test_col_fixup_pinned(void) {
    // Pins the panel column mapping: logical x=0 -> bit (0+7)&7 = 7. If the
    // bench needs a different rotation, change col_fixup and this test together.
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 0, 0, MATRIX_GREEN);
    TEST_ASSERT_EQUAL_UINT8(0x80, f[0]);   // green plane, row 0, bit 7
    TEST_ASSERT_EQUAL_UINT8(0x00, f[1]);   // red plane, row 0
}

static void test_boot_blank(void) {
    uint8_t f[16]; matrix_render_frame(STATUS_BOOT, 12345, f);
    TEST_ASSERT_EQUAL_UINT(0, count_lit(f));
}

static void test_1p_digit_1(void) {
    uint8_t f[16]; matrix_render_frame(STATUS_CONNECTED_1P, 0, f);
    assert_glyph(f, GLYPH_1, MATRIX_GREEN);
}

static void test_2p_digit_2(void) {
    uint8_t f[16]; matrix_render_frame(STATUS_CONNECTED_2P, 0, f);
    assert_glyph(f, GLYPH_2, MATRIX_GREEN);
}

static void test_searching_orbits(void) {
    uint8_t f[16];
    matrix_render_frame(STATUS_SEARCHING, 0, f);
    TEST_ASSERT_EQUAL_UINT(1, count_lit(f));
    TEST_ASSERT_EQUAL_UINT(MATRIX_GREEN, matrix_get_pixel(f, 0, 0));   // RING[0]
    matrix_render_frame(STATUS_SEARCHING, 64, f);                      // pos = (64>>6)%28 = 1
    TEST_ASSERT_EQUAL_UINT(1, count_lit(f));
    TEST_ASSERT_EQUAL_UINT(MATRIX_GREEN, matrix_get_pixel(f, 1, 0));   // RING[1]
}

static void test_error_blinks(void) {
    uint8_t f[16];
    matrix_render_frame(STATUS_ERROR, 0, f);        // (0>>9)&1 == 0 -> blank
    TEST_ASSERT_EQUAL_UINT(0, count_lit(f));
    matrix_render_frame(STATUS_ERROR, 512, f);      // (512>>9)&1 == 1 -> X
    assert_glyph(f, GLYPH_X, MATRIX_RED);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clear_all_off);
    RUN_TEST(test_set_get_colors);
    RUN_TEST(test_set_off_clears);
    RUN_TEST(test_out_of_range_noop);
    RUN_TEST(test_col_fixup_pinned);
    RUN_TEST(test_boot_blank);
    RUN_TEST(test_1p_digit_1);
    RUN_TEST(test_2p_digit_2);
    RUN_TEST(test_searching_orbits);
    RUN_TEST(test_error_blinks);
    return UNITY_END();
}
```

- [ ] **Step 4: Wire the test into `test/CMakeLists.txt`.** Add `../src/matrix` to `include_directories(...)`, and add:

```cmake
add_executable(test_matrix_render test_matrix_render.c ../src/matrix/matrix_render.c)
target_link_libraries(test_matrix_render unity)
add_test(NAME test_matrix_render COMMAND test_matrix_render)
```

- [ ] **Step 5: Run the test to confirm it fails.**

Run: `rm -rf test/build && cmake -S test -B test/build >/dev/null 2>&1 && cmake --build test/build 2>&1 | tail -5`
Expected: link/compile FAIL — `matrix_render.c` doesn't exist yet.

- [ ] **Step 6: Implement `src/matrix/matrix_render.c`.**

```c
#include "matrix_render.h"
#include "matrix_glyphs.h"

// HT16K33 bicolor RAM: row y uses frame[y*2+0]=green mask, frame[y*2+1]=red mask.
// Column-bit fixup: the Adafruit 8x8 bicolor maps logical column x to bit
// (x+7)&7. Single bench-tunable point (hardware open-item #1).
static inline unsigned col_fixup(unsigned x) { return (x + 7u) & 7u; }

void matrix_clear(uint8_t f[MATRIX_FRAME_BYTES]) {
    for (unsigned i = 0; i < MATRIX_FRAME_BYTES; i++) f[i] = 0;
}

void matrix_set_pixel(uint8_t f[MATRIX_FRAME_BYTES], unsigned x, unsigned y, unsigned color) {
    if (x >= 8u || y >= 8u) return;
    uint8_t bit = (uint8_t)(1u << col_fixup(x));
    uint8_t *g = &f[y * 2u + 0u];
    uint8_t *r = &f[y * 2u + 1u];
    *g = (uint8_t)(*g & ~bit);
    *r = (uint8_t)(*r & ~bit);
    if (color & MATRIX_GREEN) *g |= bit;
    if (color & MATRIX_RED)   *r |= bit;
}

unsigned matrix_get_pixel(const uint8_t f[MATRIX_FRAME_BYTES], unsigned x, unsigned y) {
    if (x >= 8u || y >= 8u) return MATRIX_OFF;
    uint8_t bit = (uint8_t)(1u << col_fixup(x));
    unsigned c = 0;
    if (f[y * 2u + 0u] & bit) c |= MATRIX_GREEN;
    if (f[y * 2u + 1u] & bit) c |= MATRIX_RED;
    return c;
}

static void draw_glyph(uint8_t f[MATRIX_FRAME_BYTES], const uint8_t g[8], unsigned color) {
    for (unsigned y = 0; y < 8u; y++)
        for (unsigned x = 0; x < 8u; x++)
            if (g[y] & (1u << x))
                matrix_set_pixel(f, x, y, color);
}

// 28-cell perimeter ring, clockwise from top-left.
static const uint8_t RING[28][2] = {
    {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0},
    {7,1},{7,2},{7,3},{7,4},{7,5},{7,6},{7,7},
    {6,7},{5,7},{4,7},{3,7},{2,7},{1,7},{0,7},
    {0,6},{0,5},{0,4},{0,3},{0,2},{0,1},
};

void matrix_render_frame(status_state_t s, uint16_t phase, uint8_t f[MATRIX_FRAME_BYTES]) {
    matrix_clear(f);
    switch (s) {
    case STATUS_BOOT:
        break;                                       // blank
    case STATUS_SEARCHING: {
        unsigned pos = (unsigned)((phase >> 6) % 28u);   // ~1.8 s / revolution
        matrix_set_pixel(f, RING[pos][0], RING[pos][1], MATRIX_GREEN);
        break;
    }
    case STATUS_CONNECTED_1P:
        draw_glyph(f, GLYPH_1, MATRIX_GREEN);
        break;
    case STATUS_CONNECTED_2P:
        draw_glyph(f, GLYPH_2, MATRIX_GREEN);
        break;
    case STATUS_ERROR:
        if ((phase >> 9) & 1u)                       // ~1 Hz blink
            draw_glyph(f, GLYPH_X, MATRIX_RED);
        break;
    }
}
```

- [ ] **Step 7: Run the test to confirm it passes.**

Run: `cmake --build test/build 2>&1 | tail -3 && ctest --test-dir test/build -R test_matrix_render --output-on-failure`
Expected: `test_matrix_render` PASS (10 tests).

- [ ] **Step 8: Commit.**

```bash
git add src/matrix/matrix_glyphs.h src/matrix/matrix_render.h src/matrix/matrix_render.c test/test_matrix_render.c test/CMakeLists.txt
git commit -m "feat: pure matrix render layer (glyphs, planes, orbit) + tests

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: HT16K33 driver (I2C init + DMA frame push)

Hardware-only module: I2C/GPIO setup, the HT16K33 init sequence, and a DMA frame write with STOP on the last byte (blocking fallback behind a compile flag). Not host-testable — verified by build; behavior deferred to `docs/bringup.md`.

**Files:**
- Create: `src/matrix/matrix_driver.h`, `src/matrix/matrix_driver.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `matrix_driver_init(i2c_inst_t*, unsigned sda, unsigned scl, unsigned baud, uint8_t addr) -> bool`, `matrix_driver_set_brightness(unsigned)`, `matrix_driver_show(const uint8_t[16])` — consumed by Task 5.

- [ ] **Step 1: Create `src/matrix/matrix_driver.h`.**

```c
#ifndef MATRIX_DRIVER_H
#define MATRIX_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

// Init the I2C bus + GPIO, run the HT16K33 init sequence (osc on, display on,
// brightness 0x0F), and claim a DMA channel. Returns true on success; on
// failure the driver stays disabled and show()/set_brightness() are no-ops.
// Blocking; call once on core0 after the system clock is set.
bool matrix_driver_init(i2c_inst_t *i2c, unsigned sda, unsigned scl, unsigned baud, uint8_t addr);

// Set HT16K33 brightness 0..15 (blocking single-byte write). For bring-up.
void matrix_driver_set_brightness(unsigned level);

// Push a 16-byte frame to display RAM as a 17-byte [0x00, d0..d15] transaction.
// Waits for any prior DMA transfer to finish first.
void matrix_driver_show(const uint8_t frame[16]);

#endif // MATRIX_DRIVER_H
```

- [ ] **Step 2: Create `src/matrix/matrix_driver.c`.**

```c
#include "matrix_driver.h"

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/regs/i2c.h"   // I2C_IC_DATA_CMD_STOP_BITS, I2C_IC_ENABLE_ENABLE_BITS

// HT16K33 command bytes.
#define HT16K33_OSC_ON   0x21u   // system setup: oscillator on
#define HT16K33_DISP_ON  0x81u   // display on, blink off
#define HT16K33_DIM_BASE 0xE0u   // | brightness(0..15)

static i2c_inst_t *s_i2c;
static uint8_t     s_addr;
static bool        s_ok;
#ifndef MATRIX_SHOW_BLOCKING
static int         s_dma = -1;
static uint16_t    s_tx[17];     // [ptr, 16 data] as IC_DATA_CMD command words
#endif

static bool wr(uint8_t byte) {   // one blocking command byte
    return i2c_write_blocking(s_i2c, s_addr, &byte, 1, false) == 1;
}

bool matrix_driver_init(i2c_inst_t *i2c, unsigned sda, unsigned scl, unsigned baud, uint8_t addr) {
    s_i2c = i2c; s_addr = addr; s_ok = false;

    i2c_init(i2c, baud);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);

    if (!wr(HT16K33_OSC_ON) || !wr(HT16K33_DISP_ON) || !wr(HT16K33_DIM_BASE | 0x0Fu)) {
        printf("matrix: HT16K33 not responding; LED disabled\n");
        return false;
    }

#ifndef MATRIX_SHOW_BLOCKING
    // Pin the DMA target in IC_TAR (I2C must be disabled to change it) so DMA
    // writes to IC_DATA_CMD address the HT16K33 without per-transfer setup.
    i2c_hw_t *hw = i2c_get_hw(i2c);
    hw->enable = 0;
    hw->tar = addr;
    hw->enable = I2C_IC_ENABLE_ENABLE_BITS;

    s_dma = dma_claim_unused_channel(false);   // false: don't panic on failure
    if (s_dma < 0) {
        printf("matrix: no free DMA channel; LED disabled\n");
        return false;
    }
#endif

    s_ok = true;
    return true;
}

void matrix_driver_set_brightness(unsigned level) {
    if (!s_ok) return;
    wr((uint8_t)(HT16K33_DIM_BASE | (level & 0x0Fu)));
}

#ifdef MATRIX_SHOW_BLOCKING
void matrix_driver_show(const uint8_t frame[16]) {
    if (!s_ok) return;
    uint8_t buf[17];
    buf[0] = 0x00;                              // display RAM pointer
    for (unsigned i = 0; i < 16u; i++) buf[i + 1u] = frame[i];
    i2c_write_blocking(s_i2c, s_addr, buf, 17, false);
}
#else
void matrix_driver_show(const uint8_t frame[16]) {
    if (!s_ok) return;
    dma_channel_wait_for_finish_blocking(s_dma);   // ensure prior transfer done

    s_tx[0] = 0x00;                                // display RAM pointer
    for (unsigned i = 0; i < 16u; i++)
        s_tx[i + 1u] = frame[i];
    s_tx[16] |= I2C_IC_DATA_CMD_STOP_BITS;         // assert STOP after last byte

    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, i2c_get_dreq(s_i2c, true));
    dma_channel_configure(s_dma, &c,
        &i2c_get_hw(s_i2c)->data_cmd,   // dst: I2C data/command register
        s_tx,                           // src: 17 command words
        17,                             // count
        true);                          // start now
}
#endif
```

- [ ] **Step 3: Add the driver to `CMakeLists.txt`.** In `add_executable(ps2_controller …)` add `src/matrix/matrix_driver.c`. In `target_include_directories(...)` add `${CMAKE_CURRENT_SOURCE_DIR}/src/matrix`. In `target_link_libraries(...)` add `hardware_i2c` and `hardware_dma`.

- [ ] **Step 4: Build firmware.**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build >/tmp/m_build.log 2>&1 && cmake --build build >>/tmp/m_build.log 2>&1; tail -n 1 /tmp/m_build.log; grep -iE 'error:' /tmp/m_build.log || echo NO_ERRORS`
Expected: build succeeds, `NO_ERRORS`. (`matrix_driver.c` compiles though nothing calls it yet — that arrives in Task 5.)

- [ ] **Step 5: Commit.**

```bash
git add src/matrix/matrix_driver.h src/matrix/matrix_driver.c CMakeLists.txt
git commit -m "feat: HT16K33 I2C driver with DMA frame push (blocking fallback flag)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Wire the matrix into `status_indicator`; firmware + docs

Replace the no-op stub with the real render-timer backend, add `matrix_render.c` to the firmware, and document wiring + bring-up.

**Files:**
- Modify: `src/status_led/status_indicator.c`
- Modify: `CMakeLists.txt`
- Modify: `docs/wiring.md`, `docs/bringup.md`

**Interfaces:**
- Consumes: `matrix_render_frame`, `matrix_clear` (Task 3); `matrix_driver_init/show/set_brightness` (Task 4); `status_state_t` (Task 2).

- [ ] **Step 1: Replace `src/status_led/status_indicator.c`** with the matrix backend:

```c
#include "status_indicator.h"

#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/time.h"        // repeating_timer, add_repeating_timer_ms
#include "hardware/i2c.h"

#include "matrix_render.h"
#include "matrix_driver.h"

#define MATRIX_I2C    i2c0
#define MATRIX_SDA    4u
#define MATRIX_SCL    5u
#define MATRIX_BAUD   400000u
#define MATRIX_ADDR   0x70u

static bool s_enabled = false;
// s_state is shared between core0 thread context (set) and the core0 render
// timer IRQ; relaxed atomics make the access well-defined (mirrors the prior
// WS2812 module). s_frame is touched only by the render timer.
static status_state_t s_state = STATUS_BOOT;
static uint8_t s_frame[MATRIX_FRAME_BYTES];

static bool animated(status_state_t s) {
    return s == STATUS_SEARCHING || s == STATUS_ERROR;
}

static bool render_cb(repeating_timer_t *t) {
    (void)t;
    if (!s_enabled) return true;

    status_state_t st = __atomic_load_n(&s_state, __ATOMIC_RELAXED);
    uint16_t phase = (uint16_t)(to_ms_since_boot(get_absolute_time()) & 0xFFFF);

    uint8_t next[MATRIX_FRAME_BYTES];
    matrix_render_frame(st, phase, next);

    // Push on change, or every tick while the state animates.
    bool changed = animated(st);
    if (!changed)
        for (unsigned i = 0; i < MATRIX_FRAME_BYTES; i++)
            if (next[i] != s_frame[i]) { changed = true; break; }

    if (changed) {
        for (unsigned i = 0; i < MATRIX_FRAME_BYTES; i++) s_frame[i] = next[i];
        matrix_driver_show(s_frame);
    }
    return true;
}

void status_indicator_init(void) {
    if (s_enabled) return;                       // idempotent

    if (!matrix_driver_init(MATRIX_I2C, MATRIX_SDA, MATRIX_SCL, MATRIX_BAUD, MATRIX_ADDR)) {
        printf("status_led: matrix init failed; LED disabled\n");
        return;                                  // driver logged the cause
    }

    matrix_clear(s_frame);
    matrix_driver_show(s_frame);                 // blank (BOOT)

    static repeating_timer_t timer;
    if (!add_repeating_timer_ms(-20, render_cb, NULL, &timer)) {   // 50 Hz
        printf("status_led: timer start failed; LED disabled\n");
        return;
    }
    s_enabled = true;
}

void status_indicator_set(status_state_t s) {
    if (!s_enabled) return;                      // no-op when disabled
    __atomic_store_n(&s_state, s, __ATOMIC_RELAXED);
}
```

- [ ] **Step 2: Add `matrix_render.c` to the firmware.** In `CMakeLists.txt` `add_executable(ps2_controller …)` add `src/matrix/matrix_render.c`.

- [ ] **Step 3: Build firmware + host tests.**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build >/tmp/m_build.log 2>&1 && cmake --build build >>/tmp/m_build.log 2>&1; tail -n 1 /tmp/m_build.log; grep -iE 'error:' /tmp/m_build.log || echo NO_ERRORS`
Expected: build succeeds, `NO_ERRORS`.
Run: `ctest --test-dir test/build --output-on-failure`
Expected: all suites PASS (smoke, ds2_protocol, gamepad_map, port_router, matrix_render).

- [ ] **Step 4: Add the matrix wiring section to `docs/wiring.md`.** Append:

```markdown
## Status LED — 8×8 bicolor matrix (I2C)

An Adafruit 8×8 bicolor LED matrix (HT16K33 backpack, addr `0x70`) plugs into the
board's Qwiic/STEMMA QT connector — `i2c0`, **SDA = GP4, SCL = GP5**, 3V3, GND.
Solder-free with a JST-SH cable. The STEMMA breakout carries its own SDA/SCL
pull-ups; if wiring bare, add ~2.2–4.7 kΩ pull-ups for 400 kHz.
```

- [ ] **Step 5: Replace the WS2812 section in `docs/bringup.md`.** Replace the entire `## Status LED (WS2812 on GP16)` section (through its checklist) with:

```markdown
## Status LED — 8×8 bicolor matrix (HT16K33 on i2c0)

Plug the Adafruit 8×8 bicolor matrix into the STEMMA QT connector (GP4/GP5, addr
`0x70`). Then verify:

- [ ] Power-on before BT is up: panel blank (BOOT).
- [ ] After BT init: a single green pixel orbits the perimeter (~1.8 s/rev) (SEARCHING).
- [ ] Force a cyw43 init failure (e.g. no RM2 module): red "X" blinks ~1 Hz (ERROR).
- [ ] Connect one controller: steady green "1"; connect a second: steady green "2".
- [ ] Disconnect one of two: falls back to "1"; disconnect the last: back to the orbit.
- [ ] **Column orientation (open-item #1):** confirm digits/X are upright and not
      mirrored/shifted. If wrong, adjust `col_fixup()` in `matrix_render.c` and
      the `test_col_fixup_pinned` assertion together.
- [ ] **DMA STOP (open-item #2):** confirm frames land and consecutive updates
      don't stall. If the first update works but the next hangs, the STOP bit
      isn't landing — rebuild with `-DMATRIX_SHOW_BLOCKING` (add
      `target_compile_definitions(ps2_controller PRIVATE MATRIX_SHOW_BLOCKING)`)
      to use the blocking path, and file the DMA STOP issue.
- [ ] **Connector pins (open-item #3):** confirm GP4/GP5 are the physical Qwiic
      bus on your board revision.
- [ ] Confirm the PS2 side still works (LED shares no resources with pio0/1/core1).
```

- [ ] **Step 6: Commit.**

```bash
git add src/status_led/status_indicator.c CMakeLists.txt docs/wiring.md docs/bringup.md
git commit -m "feat: drive the 8x8 matrix from status_indicator; wiring + bring-up docs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Notes for the executor

- **No PS2 hardware here** — like the rest of this project, everything is compile-gated. Tasks 4 and 5 are build-verified only; the matrix's actual display, the DMA STOP behavior, and the column orientation are validated per `docs/bringup.md`. Do not claim hardware behavior is verified.
- **Firmware build output is huge** (BluePad32 + btstack). Always redirect to a file and inspect `tail -n 1` + `grep -iE 'error:'`, never stream it — it has overflowed subagent context before.
- **Branch:** work lands on `feat/led-matrix-status` (already created off `feat/dual-controller-ports`). The branch/PR strategy vs the open NeoPixel PR #1 is decided at finish time, not here.
