# PS2 DualShock 2 Bluetooth Adapter — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build firmware for a Raspberry Pi Pico 2 W that reads a Bluetooth controller (via BluePad32) and emulates a Sony DualShock 2 to a real PlayStation 2 console.

**Architecture:** Dual-core RP2350. Core 0 runs BluePad32 (BTstack + cyw43) and maps the connected gamepad into a shared `PSXInputState`. Core 1 runs the PS2 controller-port slave: a PIO program (`psxSPI.pio`, 2.5 MHz, open-drain DATA + side-set ACK) does the byte-level SPI-slave shifting, and a pure-C DualShock protocol state machine builds responses. The protocol logic is extracted into pure functions so it is unit-testable on the host — that is the TDD core; the timing-critical PIO/transport is copied from the DS4toPS2 fork and verified on hardware.

**Tech Stack:** C (firmware + tests), Raspberry Pi Pico SDK ≥2.1 (RP2350/`pico2_w`), PIO assembly, BluePad32 (Apache-2.0) on BTstack + cyw43, Unity (host unit tests), CMake.

## Global Constraints

Copied verbatim from the spec (`docs/superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md`). Every task implicitly includes these.

- **Board:** `PICO_BOARD=pico2_w` (RP2350 + CYW43439). Requires Pico SDK ≥2.1.
- **PIO clock:** `SLOW_CLKDIV 50` → 2.5 MHz, so the SM ignores faster PS2 clocks meant for other peripherals. Do not change without re-verifying on a real PS2.
- **Bus format:** SPI Mode 3, **LSB-first**, 8-bit, full-duplex. Address `0x01` = controller (respond), `0x81` = memory card (stay Hi-Z, ignore).
- **Open-drain:** RP2350 has no OD pad mode — DATA and ACK are driven via `pindirs` (drive-low = logic 0, release-to-Hi-Z = logic 1). **Never drive the line high.**
- **Buttons are active-low** (`1` bit = released). Sticks are `0x00..0xFF`, **neutral `0x80`**.
- **ACK:** pulse low ~2 µs after each byte; **omit after the last byte**; console times out at ~60–100 µs.
- **MVP mode:** analog DualShock (`0x73`). Pressure (`0x79`), rumble (`0x4D`), multitap, and memory-card emulation are out of scope for this plan.
- **Licensing:** project is **GPL-3.0** (inherits from the DS4toPS2 / `psxSPI.pio` lineage). BluePad32 is Apache-2.0 (GPL-compatible).
- **Timing:** the SEL ISR and PIO-restart path must live in RAM (`__time_critical_func`).

## Prerequisites (one-time, before Task 1)

Confirm these exist on the build machine; they are not created by this plan:

- `arm-none-eabi-gcc` toolchain, `cmake` ≥3.13, `ninja` (or `make`), `git`, host `gcc`/`cc`.
- Pico SDK ≥2.1 available, with `PICO_SDK_PATH` exported. (The plan vendors `pico_sdk_import.cmake`, which locates the SDK via that variable.)
- Hardware for the hardware-verified tasks: a Pico 2 W, a BluePad32-supported Bluetooth controller (e.g. DualShock 4, 8BitDo, Xbox), a real PS2 (and ideally a PS1), a cut PS2 controller extension cable, and a logic analyzer **or** a second Pico to act as a bus master.

Reference sources are cloned in the session scratchpad and cited by task:
`.../scratchpad/DS4toPS2/` (fork base), `.../scratchpad/PicoGamepadConverter/`, `.../scratchpad/PicoMemcard/`.

## File Structure

```
ps2-controller/
├── CMakeLists.txt                     # firmware build (pico2_w) + bluepad32 component
├── pico_sdk_import.cmake              # copied from Pico SDK
├── memmap.ld                          # RAM placement for time-critical funcs (copied from fork)
├── LICENSE                            # GPL-3.0
├── README.md
├── src/
│   ├── main.c                         # core0 init, launch/reset core1, connect lifecycle
│   ├── ps2_device/
│   │   ├── psxSPI.pio                 # copied from fork, pins remapped
│   │   ├── ds2_ids.h                  # mode/command constants + button bit masks (pure)
│   │   ├── ds2_protocol.h / .c        # PURE-C DualShock state machine + response builder
│   │   └── ps2_transport.h / .c       # PIO init, FIFO send/recv, SEL ISR + restart (hardware)
│   └── input/
│       ├── input_state.h              # PSXInputState struct + ds2_neutral_state() (pure)
│       ├── gamepad_map.h / .c         # PURE-C uni_gamepad snapshot → PSXInputState
│       └── bluepad32_platform.h / .c  # uni_platform glue (hardware)
├── test/
│   ├── CMakeLists.txt                 # host-native test build (no Pico SDK)
│   ├── unity/                         # vendored Unity (unity.c/.h/_internals.h)
│   ├── test_ds2_protocol.c
│   └── test_gamepad_map.c
└── docs/
    ├── superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md
    ├── superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md   # this file
    └── wiring.md                      # pin map + cable pinout (Task 12)
```

**Pin assignment (chosen once, in Task 12).** Default GPIOs, avoiding cyw43-reserved pins on the Pico 2 W: `PIN_DAT=5, PIN_CMD=6, PIN_SEL=7, PIN_CLK=8, PIN_ACK=9` (same as DS4toPS2's default — convenient and known-good). Documented in `docs/wiring.md`.

---

## Phase M0 — Project & test scaffolding

### Task 1: Firmware skeleton that builds for `pico2_w`

**Files:**
- Create: `LICENSE`, `README.md`, `pico_sdk_import.cmake`, `memmap.ld`, `CMakeLists.txt`, `src/main.c`

**Interfaces:**
- Consumes: Pico SDK (`PICO_SDK_PATH`).
- Produces: a flashable `build/ps2_controller.uf2`; `main.c` with `int main(void)`.

- [ ] **Step 1: Add the GPL-3.0 LICENSE**

Copy the fork's license text (identical GPL-3.0):

```bash
cp "$SCRATCH/DS4toPS2/LICENSE" LICENSE
cp "$SCRATCH/DS4toPS2/pico_sdk_import.cmake" pico_sdk_import.cmake
cp "$SCRATCH/DS4toPS2/memmap.ld" memmap.ld
```

(`$SCRATCH` = the scratchpad path in Prerequisites. If unavailable, fetch `pico_sdk_import.cmake` from `$PICO_SDK_PATH/external/pico_sdk_import.cmake` and the GPL-3.0 text from https://www.gnu.org/licenses/gpl-3.0.txt.)

- [ ] **Step 2: Write minimal `src/main.c`**

```c
#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    while (true) {
        printf("ps2-controller: alive on pico2_w\n");
        sleep_ms(1000);
    }
    return 0;
}
```

- [ ] **Step 3: Write top-level `CMakeLists.txt`** (BluePad32 wiring added in Task 11)

```cmake
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)

project(ps2_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ps2_controller
    src/main.c
)

target_link_libraries(ps2_controller PUBLIC
    pico_stdlib
)

pico_enable_stdio_usb(ps2_controller 1)
pico_enable_stdio_uart(ps2_controller 0)
pico_add_extra_outputs(ps2_controller)
```

- [ ] **Step 4: Configure and build**

Run:
```bash
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```
Expected: build succeeds; `build/ps2_controller.uf2` exists (`ls build/ps2_controller.uf2`).

- [ ] **Step 5: Commit**

```bash
git add LICENSE README.md pico_sdk_import.cmake memmap.ld CMakeLists.txt src/main.c
git commit -m "M0: firmware skeleton builds for pico2_w"
```

---

### Task 2: Host unit-test harness (Unity)

**Files:**
- Create: `test/unity/unity.c`, `test/unity/unity.h`, `test/unity/unity_internals.h`, `test/CMakeLists.txt`, `test/test_smoke.c`

**Interfaces:**
- Produces: a native `ctest` build under `build-test/` that compiles pure `src/**` modules against Unity. Later protocol/mapping tasks add test files here.

- [ ] **Step 1: Vendor Unity**

```bash
git clone --depth 1 https://github.com/ThrowTheSwitch/Unity.git /tmp/unity_src
mkdir -p test/unity
cp /tmp/unity_src/src/unity.c test/unity/
cp /tmp/unity_src/src/unity.h test/unity/
cp /tmp/unity_src/src/unity_internals.h test/unity/
rm -rf /tmp/unity_src
```

- [ ] **Step 2: Write `test/test_smoke.c`**

```c
#include "unity/unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_harness_runs(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_harness_runs);
    return UNITY_END();
}
```

- [ ] **Step 3: Write `test/CMakeLists.txt`** (native, no Pico SDK)

```cmake
cmake_minimum_required(VERSION 3.13)
project(ps2_controller_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

# Include dirs for the pure modules under test.
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps2_device
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/input
)

add_library(unity STATIC unity/unity.c)

# One executable per test file. More are added in later tasks.
add_executable(test_smoke test_smoke.c)
target_link_libraries(test_smoke unity)
add_test(NAME test_smoke COMMAND test_smoke)
```

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake -S test -B build-test
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add test/
git commit -m "M0: host unit-test harness (Unity) green"
```

---

## Phase M1a — DS2 protocol core (pure C, TDD)

> This is the TDD heart. Every task here is host-tested via Unity. No hardware needed.

### Task 3: Shared types and constants

**Files:**
- Create: `src/ps2_device/ds2_ids.h`, `src/input/input_state.h`, `test/test_ds2_protocol.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: `PSXInputState` struct; `ds2_neutral_state()`; `MODE_*`/`CMD_*` and `PS_*` bit-mask macros.

- [ ] **Step 1: Write the failing test** — append to `test/test_ds2_protocol.c`

```c
#include "unity/unity.h"
#include "input_state.h"
#include "ds2_ids.h"

void setUp(void) {}
void tearDown(void) {}

static void test_neutral_state_defaults(void) {
    PSXInputState s = ds2_neutral_state();
    TEST_ASSERT_EQUAL_HEX8(0xFF, s.buttons1);   // all released (active-low)
    TEST_ASSERT_EQUAL_HEX8(0xFF, s.buttons2);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.rx);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.ry);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.lx);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.ly);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_state_defaults);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the test and run to verify it fails to compile**

Add to `test/CMakeLists.txt`:
```cmake
add_executable(test_ds2_protocol test_ds2_protocol.c)
target_link_libraries(test_ds2_protocol unity)
add_test(NAME test_ds2_protocol COMMAND test_ds2_protocol)
```
Run: `cmake -S test -B build-test && cmake --build build-test`
Expected: FAIL — `input_state.h` / `ds2_ids.h` not found.

- [ ] **Step 3: Write `src/input/input_state.h`**

```c
#ifndef INPUT_STATE_H
#define INPUT_STATE_H
#include <stdint.h>

// DualShock controller state, already in PS2 wire encoding.
typedef struct {
    uint8_t buttons1;  // BTNL, active-low: Select,L3,R3,Start, Up,Right,Down,Left
    uint8_t buttons2;  // BTNH, active-low: L2,R2,L1,R1, Tri,Circle,Cross,Square
    uint8_t rx, ry;    // right stick, 0x00..0xFF, neutral 0x80
    uint8_t lx, ly;    // left  stick, 0x00..0xFF, neutral 0x80
    uint8_t l2, r2;    // analog trigger pressure (used only in deferred 0x79 mode)
} PSXInputState;

static inline PSXInputState ds2_neutral_state(void) {
    PSXInputState s;
    s.buttons1 = 0xFF; s.buttons2 = 0xFF;
    s.rx = s.ry = s.lx = s.ly = 0x80;
    s.l2 = s.r2 = 0x00;
    return s;
}
#endif // INPUT_STATE_H
```

- [ ] **Step 4: Write `src/ps2_device/ds2_ids.h`** (ported from `DS4toPS2/src/controller_simulator.h:13-42`)

```c
#ifndef DS2_IDS_H
#define DS2_IDS_H

// Controller ID byte (also = current mode)
#define MODE_DIGITAL          0x41
#define MODE_ANALOG           0x73
#define MODE_ANALOG_PRESSURE  0x79
#define MODE_CONFIG           0xF3

// Console command byte (2nd byte of a packet)
#define CMD_PRES_CONFIG        0x40
#define CMD_POLL_CONFIG_STATUS 0x41
#define CMD_POLL               0x42
#define CMD_CONFIG             0x43
#define CMD_ANALOG_SWITCH      0x44
#define CMD_STATUS             0x45
#define CMD_CONST_46           0x46
#define CMD_CONST_47           0x47
#define CMD_CONST_4C           0x4C
#define CMD_ENABLE_RUMBLE      0x4D
#define CMD_POLL_CONFIG        0x4F

// buttons1 (BTNL) masks — active-low
#define PS_SELECT 0x01
#define PS_L3     0x02
#define PS_R3     0x04
#define PS_START  0x08
#define PS_UP     0x10
#define PS_RIGHT  0x20
#define PS_DOWN   0x40
#define PS_LEFT   0x80

// buttons2 (BTNH) masks — active-low
#define PS_L2   0x01
#define PS_R2   0x02
#define PS_L1   0x04
#define PS_R1   0x08
#define PS_TRI  0x10
#define PS_CIR  0x20
#define PS_X    0x40
#define PS_SQU  0x80

#endif // DS2_IDS_H
```

- [ ] **Step 5: Rebuild and run to verify pass**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: `test_ds2_protocol` PASS.

- [ ] **Step 6: Commit**

```bash
git add src/input/input_state.h src/ps2_device/ds2_ids.h test/test_ds2_protocol.c test/CMakeLists.txt
git commit -m "M1a: PSXInputState + DS2 constants with neutral-state test"
```

---

### Task 4: Analog poll response builder

**Files:**
- Create: `src/ps2_device/ds2_protocol.h`, `src/ps2_device/ds2_protocol.c`
- Modify: `test/test_ds2_protocol.c`, `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void ds2_init(ds2_state_t *st)` — resets to digital, not-config, poll_config all-zero (so `detectAnalog()` yields `MODE_ANALOG`, keeping MVP out of pressure mode).
  - `size_t ds2_response(const ds2_state_t *st, uint8_t cmd, const PSXInputState *in, const uint8_t *req, size_t req_len, uint8_t *out, size_t cap)` — writes the full DATA frame (`out[0]`=ID, `out[1]`=0x5A, then payload); returns byte count.
  - `ds2_state_t { uint8_t mode; bool config; bool analog_lock; uint8_t poll_config[4]; uint8_t motor_bytes[6]; }`.

- [ ] **Step 1: Write the failing test** — add to `test/test_ds2_protocol.c` (and `RUN_TEST` it in `main`)

```c
#include "ds2_protocol.h"

static void test_analog_poll_neutral(void) {
    ds2_state_t st;
    ds2_init(&st);
    st.mode = MODE_ANALOG;         // pretend a game already enabled analog
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    // FF is the transport idle byte; the frame from ID onward is 8 bytes:
    const uint8_t expect[] = {0x73, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80};
    TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-test`
Expected: FAIL — `ds2_protocol.h` not found.

- [ ] **Step 3: Write `src/ps2_device/ds2_protocol.h`**

```c
#ifndef DS2_PROTOCOL_H
#define DS2_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "input_state.h"

typedef struct {
    uint8_t mode;            // MODE_DIGITAL / MODE_ANALOG / MODE_ANALOG_PRESSURE
    bool    config;          // in config/escape mode
    bool    analog_lock;
    uint8_t poll_config[4];
    uint8_t motor_bytes[6];
} ds2_state_t;

void ds2_init(ds2_state_t *st);

// Build the full DATA response frame for `cmd` given current state + input.
// out[0]=ID (0xF3 in config, else mode), out[1]=0x5A, then payload.
// `req` = the console's payload bytes after the command byte (for offset-
// dependent constants); may be NULL when req_len==0. Returns bytes written.
size_t ds2_response(const ds2_state_t *st, uint8_t cmd,
                    const PSXInputState *in,
                    const uint8_t *req, size_t req_len,
                    uint8_t *out, size_t cap);

// Apply a completed packet's request bytes: config enter/exit, mode switch,
// poll-config, motor mapping, and poll's implicit config-exit. Call at packet end.
void ds2_apply_request(ds2_state_t *st, uint8_t cmd,
                       const uint8_t *req, size_t req_len);

#endif // DS2_PROTOCOL_H
```

- [ ] **Step 4: Write `src/ps2_device/ds2_protocol.c`** (minimal — poll only; more commands in later tasks)

```c
#include "ds2_protocol.h"
#include "ds2_ids.h"
#include <string.h>

void ds2_init(ds2_state_t *st) {
    st->mode = MODE_DIGITAL;
    st->config = false;
    st->analog_lock = false;
    memset(st->poll_config, 0x00, sizeof st->poll_config); // -> detectAnalog() == ANALOG
    memset(st->motor_bytes, 0xFF, sizeof st->motor_bytes);
}

static uint8_t id_byte(const ds2_state_t *st) {
    return st->config ? MODE_CONFIG : st->mode;
}

size_t ds2_response(const ds2_state_t *st, uint8_t cmd,
                    const PSXInputState *in,
                    const uint8_t *req, size_t req_len,
                    uint8_t *out, size_t cap) {
    (void)req; (void)req_len;
    size_t n = 0;
    if (cap < 2) return 0;
    out[n++] = id_byte(st);
    out[n++] = 0x5A;

    switch (cmd) {
        case CMD_POLL: {
            if (st->mode == MODE_DIGITAL) {
                out[n++] = in->buttons1;
                out[n++] = in->buttons2;
            } else { // ANALOG (pressure handled in a later task)
                out[n++] = in->buttons1;
                out[n++] = in->buttons2;
                out[n++] = in->rx;
                out[n++] = in->ry;
                out[n++] = in->lx;
                out[n++] = in->ly;
            }
            break;
        }
        default:
            break;
    }
    return n;
}

void ds2_apply_request(ds2_state_t *st, uint8_t cmd,
                       const uint8_t *req, size_t req_len) {
    (void)req; (void)req_len;
    if (cmd == CMD_POLL) st->config = false;
}
```

- [ ] **Step 5: Add `ds2_protocol.c` to the test target**

In `test/CMakeLists.txt`, change the `test_ds2_protocol` target to compile the source:
```cmake
add_executable(test_ds2_protocol test_ds2_protocol.c ../src/ps2_device/ds2_protocol.c)
target_link_libraries(test_ds2_protocol unity)
add_test(NAME test_ds2_protocol COMMAND test_ds2_protocol)
```

- [ ] **Step 6: Rebuild and run to verify pass**

Run: `cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: both tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ps2_device/ds2_protocol.h src/ps2_device/ds2_protocol.c test/test_ds2_protocol.c test/CMakeLists.txt
git commit -m "M1a: analog poll response builder (TDD)"
```

---

### Task 5: Digital poll + config-mode ID byte

**Files:**
- Modify: `test/test_ds2_protocol.c`

**Interfaces:**
- Consumes: `ds2_response` from Task 4. No new signatures — validates existing behavior for `MODE_DIGITAL` and the config ID byte.

- [ ] **Step 1: Write the failing tests** — add and `RUN_TEST` both

```c
static void test_digital_poll_neutral(void) {
    ds2_state_t st; ds2_init(&st);          // mode defaults to DIGITAL
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    const uint8_t expect[] = {0x41, 0x5A, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

static void test_config_mode_id_is_F3(void) {
    ds2_state_t st; ds2_init(&st);
    st.mode = MODE_ANALOG;
    st.config = true;                       // in config/escape mode
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL_HEX8(0xF3, out[0]);   // ID reflects config, not mode
}
```

- [ ] **Step 2: Run to verify** — these should already PASS given Task 4's implementation.

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: PASS. (If digital returns 8 bytes instead of 4, the `mode==MODE_DIGITAL` branch is wrong — fix it.)

- [ ] **Step 3: Commit**

```bash
git add test/test_ds2_protocol.c
git commit -m "M1a: digital poll + config ID byte tests"
```

---

### Task 6: Input propagation into poll frame

**Files:**
- Modify: `test/test_ds2_protocol.c`

**Interfaces:**
- Consumes: `ds2_response`. Validates that button/stick fields flow through unchanged.

- [ ] **Step 1: Write the failing tests**

```c
static void test_analog_poll_reflects_input(void) {
    ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG;
    PSXInputState in = ds2_neutral_state();
    in.buttons2 &= (uint8_t)~PS_X;   // press Cross (active-low: clear the bit)
    in.lx = 0x00;                    // left stick full left
    in.ry = 0xFF;                    // right stick full down
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(8, n);
    TEST_ASSERT_EQUAL_HEX8(in.buttons1, out[2]);
    TEST_ASSERT_EQUAL_HEX8(in.buttons2, out[3]);   // Cross bit cleared
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);          // ry
    TEST_ASSERT_EQUAL_HEX8(0x00, out[6]);          // lx
}
```

- [ ] **Step 2: Run to verify** — should PASS with Task 4's implementation.

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add test/test_ds2_protocol.c
git commit -m "M1a: input propagation into poll frame test"
```

---

### Task 7: Config enter/exit (`0x43`)

**Files:**
- Modify: `src/ps2_device/ds2_protocol.c`, `test/test_ds2_protocol.c`

**Interfaces:**
- Extends `ds2_apply_request`: for `CMD_CONFIG`, `req[1]==0x01` enters config, `req[1]==0x00` exits. (`req` = console bytes after the command byte; the enter/exit flag is the 2nd such byte, matching `DS4toPS2/src/controller_simulator.cpp:164-175`.)
- Extends `ds2_response`: `CMD_CONFIG` payload is `{0x5A, 0,0,0,0,0,0}` (6 trailing zeros).

- [ ] **Step 1: Write the failing tests**

```c
static void test_enter_config_sets_flag(void) {
    ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG;
    const uint8_t req[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00};  // enter
    ds2_apply_request(&st, CMD_CONFIG, req, sizeof req);
    TEST_ASSERT_TRUE(st.config);
}

static void test_exit_config_clears_flag(void) {
    ds2_state_t st; ds2_init(&st); st.config = true;
    const uint8_t req[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // exit
    ds2_apply_request(&st, CMD_CONFIG, req, sizeof req);
    TEST_ASSERT_FALSE(st.config);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: FAIL — `st.config` unchanged (apply_request ignores `0x43`).

- [ ] **Step 3: Extend `ds2_apply_request`** in `src/ps2_device/ds2_protocol.c`

```c
void ds2_apply_request(ds2_state_t *st, uint8_t cmd,
                       const uint8_t *req, size_t req_len) {
    switch (cmd) {
        case CMD_POLL:
            st->config = false;
            break;
        case CMD_CONFIG:
            if (req_len > 1) st->config = (req[1] == 0x01);
            break;
        default:
            break;
    }
}
```

- [ ] **Step 4: Extend `ds2_response`** — add a `CMD_CONFIG` case before `default`:

```c
        case CMD_CONFIG: {
            for (int i = 0; i < 6 && n < cap; i++) out[n++] = 0x00;
            break;
        }
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ps2_device/ds2_protocol.c test/test_ds2_protocol.c
git commit -m "M1a: config enter/exit (0x43)"
```

---

### Task 8: Analog switch + lock (`0x44`)

**Files:**
- Modify: `src/ps2_device/ds2_protocol.c`, `test/test_ds2_protocol.c`

**Interfaces:**
- Extends `ds2_apply_request` for `CMD_ANALOG_SWITCH`: `req[1]==0x01` → analog (via `detectAnalog`), else digital; `req[2]==0x03` → `analog_lock=true`. Only honored while `st->config`. Mirrors `controller_simulator.cpp:223-250`.
- Adds internal `detectAnalog(st)`: `sum(poll_config) > 0 ? MODE_ANALOG_PRESSURE : MODE_ANALOG`.

- [ ] **Step 1: Write the failing tests**

```c
static void test_analog_switch_sets_analog_and_lock(void) {
    ds2_state_t st; ds2_init(&st); st.config = true;   // must be in config
    const uint8_t req[] = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00}; // analog + lock
    ds2_apply_request(&st, CMD_ANALOG_SWITCH, req, sizeof req);
    TEST_ASSERT_EQUAL_HEX8(MODE_ANALOG, st.mode);
    TEST_ASSERT_TRUE(st.analog_lock);
}

static void test_analog_switch_ignored_outside_config(void) {
    ds2_state_t st; ds2_init(&st); st.config = false;  // NOT in config
    const uint8_t req[] = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00};
    ds2_apply_request(&st, CMD_ANALOG_SWITCH, req, sizeof req);
    TEST_ASSERT_EQUAL_HEX8(MODE_DIGITAL, st.mode);     // unchanged
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: FAIL — mode not updated.

- [ ] **Step 3: Extend the implementation** in `src/ps2_device/ds2_protocol.c`

Add above `ds2_apply_request`:
```c
static uint8_t detect_analog(const ds2_state_t *st) {
    int sum = st->poll_config[0] + st->poll_config[1]
            + st->poll_config[2] + st->poll_config[3];
    return (sum > 0) ? MODE_ANALOG_PRESSURE : MODE_ANALOG;
}
```
Add a case in `ds2_apply_request`:
```c
        case CMD_ANALOG_SWITCH:
            if (st->config && req_len > 2) {
                st->mode = (req[1] == 0x01) ? detect_analog(st) : MODE_DIGITAL;
                st->analog_lock = (req[2] == 0x03);
            }
            break;
```
Also add the `CMD_ANALOG_SWITCH` response payload to `ds2_response` (fixed `{0x00,0x00,0x00,0x00,0x00}` after the `0x5A`), before `default`:
```c
        case CMD_ANALOG_SWITCH: {
            for (int i = 0; i < 5 && n < cap; i++) out[n++] = 0x00;
            break;
        }
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ps2_device/ds2_protocol.c test/test_ds2_protocol.c
git commit -m "M1a: analog switch + lock (0x44)"
```

---

### Task 9: Config-mode constants for PS2 recognition

**Files:**
- Modify: `src/ps2_device/ds2_protocol.c`, `test/test_ds2_protocol.c`

**Interfaces:**
- Extends `ds2_response` with the config-mode descriptor commands the PS2 probes so it recognizes a locked analog pad: `0x45` status, `0x41` poll-config-status, `0x40`, `0x46`, `0x47`, `0x4C`, `0x4F`. Byte tables ported verbatim from `controller_simulator.cpp:108-337`. These respond only while `st->config`.
- Extends `ds2_apply_request` for `0x4F` (poll-config → sets `poll_config[0..3]` from `req[1..4]`, recomputes mode) and `0x4D` (motor mapping → `motor_bytes` from `req[1..6]`).

- [ ] **Step 1: Write the failing tests**

```c
static void test_status_45_in_analog_config(void) {
    ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG; st.config = true;
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_STATUS, &in, NULL, 0, out, sizeof out);
    // ID(F3) 5A 03 02 01 02 01 00  (byte[4]=0x01 => analog; 0x00 => digital)
    const uint8_t expect[] = {0xF3, 0x5A, 0x03, 0x02, 0x01, 0x02, 0x01, 0x00};
    TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

static void test_pollconfig_4F_enables_pressure_mode(void) {
    ds2_state_t st; ds2_init(&st); st.config = true; st.mode = MODE_ANALOG;
    // req[1..4] = the 4 config bytes; nonzero sum => pressure mode
    const uint8_t req[] = {0x00, 0xFF, 0xFF, 0x03, 0x00, 0x00};
    ds2_apply_request(&st, CMD_POLL_CONFIG, req, sizeof req);
    TEST_ASSERT_EQUAL_HEX8(MODE_ANALOG_PRESSURE, st.mode);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: FAIL — `0x45` returns only `{ID,0x5A}`; `0x4F` doesn't change mode.

- [ ] **Step 3: Extend `ds2_response`** — add these cases before `default` (ported tables)

```c
        case CMD_STATUS: {  // 0x45
            const uint8_t tail[] = {0x03, 0x02,
                (uint8_t)(st->mode == MODE_DIGITAL ? 0x00 : 0x01),
                0x02, 0x01, 0x00};
            for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            break;
        }
        case CMD_POLL_CONFIG_STATUS: {  // 0x41
            bool dig = (st->mode == MODE_DIGITAL);
            const uint8_t tail[] = {
                (uint8_t)(dig ? 0x00 : 0xFF), (uint8_t)(dig ? 0x00 : 0xFF),
                (uint8_t)(dig ? 0x00 : 0x03), (uint8_t)(dig ? 0x00 : 0x00),
                0x00, (uint8_t)(dig ? 0x00 : 0x5A)};
            for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            break;
        }
        case CMD_PRES_CONFIG: {  // 0x40
            const uint8_t tail[] = {0x00, 0x00, 0x02, 0x00, 0x00, 0x5A};
            for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            break;
        }
        case CMD_CONST_46: {  // offset-dependent (req[1] = offset)
            uint8_t off = (req && req_len > 1) ? req[1] : 0x00;
            const uint8_t tail[] = {0x00, 0x01,
                (uint8_t)(off == 0x00 ? 0x02 : 0x01),
                (uint8_t)(off == 0x00 ? 0x00 : 0x01), 0x0F};
            for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            break;
        }
        case CMD_CONST_47: {
            const uint8_t tail[] = {0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
            for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            break;
        }
        case CMD_CONST_4C: {  // offset-dependent
            uint8_t off = (req && req_len > 1) ? req[1] : 0x00;
            const uint8_t tail[] = {0x00, 0x00,
                (uint8_t)(off == 0x00 ? 0x04 : 0x07), 0x00, 0x00};
            for (size_t i = 0; i < sizeof tail && n < cap; i++) out[n++] = tail[i];
            break;
        }
        case CMD_POLL_CONFIG:    // 0x4F — response is fixed 5A + 5 zeros
        case CMD_ENABLE_RUMBLE:  // 0x4D — response is fixed 5A + 5 zeros
            for (int i = 0; i < 5 && n < cap; i++) out[n++] = 0x00;
            break;
```

Guard the config-only commands: at the top of `ds2_response`, right after writing the `0x5A`, return early for descriptor commands when not in config (matches the fork's `if (!config) return;`). Simplest: only emit the payloads above when `st->config` for these commands — wrap each config-only case body in `if (st->config) { ... }`, or add `if (!st->config && cmd != CMD_POLL && cmd != CMD_CONFIG) return n;` before the switch.

- [ ] **Step 4: Extend `ds2_apply_request`** — add cases

```c
        case CMD_POLL_CONFIG:
            if (st->config && req_len > 4) {
                for (int i = 0; i < 4; i++) st->poll_config[i] = req[1 + i];
                int sum = st->poll_config[0] + st->poll_config[1]
                        + st->poll_config[2] + st->poll_config[3];
                st->mode = (sum != 0) ? MODE_ANALOG_PRESSURE : MODE_ANALOG;
            }
            break;
        case CMD_ENABLE_RUMBLE:
            if (st->config && req_len > 6)
                for (int i = 0; i < 6; i++) st->motor_bytes[i] = req[1 + i];
            break;
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: PASS (all protocol tests green).

- [ ] **Step 6: Commit**

```bash
git add src/ps2_device/ds2_protocol.c test/test_ds2_protocol.c
git commit -m "M1a: config-mode descriptor constants + poll-config/rumble state"
```

---

## Phase M1b — Input mapping (pure C, TDD)

### Task 10: Gamepad → PSXInputState mapping

**Files:**
- Create: `src/input/gamepad_map.h`, `src/input/gamepad_map.c`, `test/test_gamepad_map.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `gamepad_snapshot_t { uint8_t dpad; uint32_t buttons; int32_t axis_x, axis_y, axis_rx, axis_ry; int32_t brake, throttle; }` — a Pico/BluePad32-independent mirror of the fields we read from `uni_gamepad_t` (axes −512..511, triggers 0..1023, `dpad`/`buttons` bitmasks per BluePad32's `DPAD_*`/`BUTTON_*`).
  - `void map_gamepad_to_psx(const gamepad_snapshot_t *g, PSXInputState *out)` — pure, fills active-low buttons, sticks (neutral 0x80), and L2/R2 threshold bits.
- Neutral input maps to `ds2_neutral_state()`.

- [ ] **Step 1: Write the failing tests** — `test/test_gamepad_map.c`

```c
#include "unity/unity.h"
#include "gamepad_map.h"
#include "ds2_ids.h"

void setUp(void) {}
void tearDown(void) {}

// BluePad32 bit values (from uni_gamepad.h) mirrored for the host test.
#define DPAD_UP 0x01
#define BP_BTN_X 0x0008  // square on DS
#define BP_BTN_A 0x0001  // cross

static gamepad_snapshot_t neutral_snapshot(void) {
    gamepad_snapshot_t g = {0};
    return g; // all zero: no dpad, no buttons, sticks centered at 0, triggers 0
}

static void test_neutral_maps_to_neutral(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.buttons1);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.buttons2);
    TEST_ASSERT_EQUAL_HEX8(0x80, out.lx);
    TEST_ASSERT_EQUAL_HEX8(0x80, out.ly);
}

static void test_dpad_up_clears_up_bit(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.dpad = DPAD_UP;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_UP); // active-low: pressed => 0
}

static void test_left_stick_full_left(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.axis_x = -512;                 // BluePad32 min
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.lx);
}

static void test_trigger_threshold_sets_l2(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.brake = 1023;                  // full left trigger
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_L2); // pressed => bit clear
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_maps_to_neutral);
    RUN_TEST(test_dpad_up_clears_up_bit);
    RUN_TEST(test_left_stick_full_left);
    RUN_TEST(test_trigger_threshold_sets_l2);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the test target and run to verify it fails**

Add to `test/CMakeLists.txt`:
```cmake
add_executable(test_gamepad_map test_gamepad_map.c ../src/input/gamepad_map.c)
target_link_libraries(test_gamepad_map unity)
add_test(NAME test_gamepad_map COMMAND test_gamepad_map)
```
Run: `cmake -S test -B build-test && cmake --build build-test`
Expected: FAIL — `gamepad_map.h` not found.

- [ ] **Step 3: Write `src/input/gamepad_map.h`**

```c
#ifndef GAMEPAD_MAP_H
#define GAMEPAD_MAP_H
#include <stdint.h>
#include "input_state.h"

// Board-independent mirror of the uni_gamepad_t fields we consume.
typedef struct {
    uint8_t  dpad;                       // BluePad32 DPAD_* bitmask
    uint32_t buttons;                    // BluePad32 BUTTON_* bitmask
    int32_t  axis_x, axis_y;             // left  stick, -512..511
    int32_t  axis_rx, axis_ry;           // right stick, -512..511
    int32_t  brake, throttle;            // L2/R2 analog, 0..1023
} gamepad_snapshot_t;

void map_gamepad_to_psx(const gamepad_snapshot_t *g, PSXInputState *out);

#endif // GAMEPAD_MAP_H
```

- [ ] **Step 4: Write `src/input/gamepad_map.c`**

```c
#include "gamepad_map.h"
#include "ds2_ids.h"

// BluePad32 uni_gamepad.h values (mirrored; keep in sync with the SDK header).
#define BP_DPAD_UP    0x01
#define BP_DPAD_DOWN  0x02
#define BP_DPAD_RIGHT 0x04
#define BP_DPAD_LEFT  0x08
#define BP_BTN_A      0x0001  // Cross
#define BP_BTN_B      0x0002  // Circle
#define BP_BTN_X      0x0008  // Square
#define BP_BTN_Y      0x0010  // Triangle
#define BP_BTN_L1     0x0020  // was 0x0010? verify against SDK in Task 11
#define BP_BTN_R1     0x0040
#define BP_BTN_L3     0x0100
#define BP_BTN_R3     0x0200
// misc buttons live in a separate BluePad32 field; wired up in Task 11.

static uint8_t axis_to_u8(int32_t v) {
    // BluePad32 axis range -512..511 -> 0..255, neutral 0x80.
    int32_t s = (v + 512) >> 2;         // 0..255
    if (s < 0) s = 0; if (s > 255) s = 255;
    return (uint8_t)s;
}

void map_gamepad_to_psx(const gamepad_snapshot_t *g, PSXInputState *out) {
    uint8_t b1 = 0xFF, b2 = 0xFF;       // active-low, start all-released
    #define CLR(mask, cond) do { if (cond) b_target &= (uint8_t)~(mask); } while (0)

    // buttons1 (BTNL): dpad + Start/Select/L3/R3 (Start/Select added in Task 11)
    { uint8_t b_target = b1;
      if (g->dpad & BP_DPAD_UP)    b_target &= (uint8_t)~PS_UP;
      if (g->dpad & BP_DPAD_DOWN)  b_target &= (uint8_t)~PS_DOWN;
      if (g->dpad & BP_DPAD_LEFT)  b_target &= (uint8_t)~PS_LEFT;
      if (g->dpad & BP_DPAD_RIGHT) b_target &= (uint8_t)~PS_RIGHT;
      if (g->buttons & BP_BTN_L3)  b_target &= (uint8_t)~PS_L3;
      if (g->buttons & BP_BTN_R3)  b_target &= (uint8_t)~PS_R3;
      b1 = b_target; }

    // buttons2 (BTNH): face + shoulders + triggers-as-digital
    { uint8_t b_target = b2;
      if (g->buttons & BP_BTN_A) b_target &= (uint8_t)~PS_X;
      if (g->buttons & BP_BTN_B) b_target &= (uint8_t)~PS_CIR;
      if (g->buttons & BP_BTN_X) b_target &= (uint8_t)~PS_SQU;
      if (g->buttons & BP_BTN_Y) b_target &= (uint8_t)~PS_TRI;
      if (g->buttons & BP_BTN_L1) b_target &= (uint8_t)~PS_L1;
      if (g->buttons & BP_BTN_R1) b_target &= (uint8_t)~PS_R1;
      if (g->brake    > 512)      b_target &= (uint8_t)~PS_L2;  // analog->digital threshold
      if (g->throttle > 512)      b_target &= (uint8_t)~PS_R2;
      b2 = b_target; }
    #undef CLR

    out->buttons1 = b1;
    out->buttons2 = b2;
    out->lx = axis_to_u8(g->axis_x);
    out->ly = axis_to_u8(g->axis_y);
    out->rx = axis_to_u8(g->axis_rx);
    out->ry = axis_to_u8(g->axis_ry);
    out->l2 = (uint8_t)(g->brake    >> 2);
    out->r2 = (uint8_t)(g->throttle >> 2);
}
```

Note: the `BP_BTN_*` values above are placeholders to keep the host test self-contained; Task 11 replaces them with `#include <uni.h>`'s real `BUTTON_*`/`DPAD_*` macros and re-runs these tests to confirm the mapping still holds.

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: all test executables PASS.

- [ ] **Step 6: Commit**

```bash
git add src/input/gamepad_map.h src/input/gamepad_map.c test/test_gamepad_map.c test/CMakeLists.txt
git commit -m "M1b: pure gamepad->PSXInputState mapping (TDD)"
```

---

## Phase M1c — BluePad32 integration (hardware-verified)

### Task 11: Bring up a Bluetooth controller and dump mapped state

> Hardware-verified (no host unit test): requires a Pico 2 W + a Bluetooth controller.

**Files:**
- Create: `src/input/bluepad32_platform.h`, `src/input/bluepad32_platform.c`
- Modify: `CMakeLists.txt`, `src/main.c`
- Add: `bluepad32` as a submodule/component

**Interfaces:**
- Consumes: `map_gamepad_to_psx` (Task 10), `PSXInputState`.
- Produces: `volatile PSXInputState g_shared_input;` written from BluePad32's `on_controller_data`; `bool bp_controller_connected(void)`.

- [ ] **Step 1: Add BluePad32 and wire the build**

```bash
git submodule add https://github.com/ricardoquesada/bluepad32.git external/bluepad32
git -C external/bluepad32 submodule update --init --depth 1
```
Follow BluePad32's Pico W guide (`external/bluepad32/docs/plat_picow.md`). In `CMakeLists.txt` add, before `add_executable`:
```cmake
set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/external/bluepad32)
set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)
```
and after `pico_sdk_init()`:
```cmake
target_link_libraries(ps2_controller PUBLIC
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    pico_multicore
    bluepad32
)
add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)
```
Add the new sources to `add_executable`: `src/input/bluepad32_platform.c src/input/gamepad_map.c`.

- [ ] **Step 2: Write `src/input/bluepad32_platform.{h,c}`**

Model on `external/bluepad32/examples/pico_w/src/my_platform.c`. The `.h`:
```c
#ifndef BLUEPAD32_PLATFORM_H
#define BLUEPAD32_PLATFORM_H
#include "input_state.h"
#include <stdbool.h>
extern volatile PSXInputState g_shared_input;
bool bp_controller_connected(void);
struct uni_platform *get_ps2_platform(void);
#endif
```
In the `.c`, implement `on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)`: build a `gamepad_snapshot_t` from `ctl->gamepad` (`gp->dpad`, `gp->buttons`, `gp->misc_buttons` for Start/Select, `gp->axis_x/y/rx/ry`, `gp->brake`, `gp->throttle`), call `map_gamepad_to_psx`, and store into `g_shared_input`. Replace the placeholder `BP_BTN_*` in `gamepad_map.c` with real `BUTTON_*`/`DPAD_*`/`MISC_BUTTON_*` includes; keep Start/Select mapping to `PS_START`/`PS_SELECT` here. Track connect/disconnect to back `bp_controller_connected()`.

- [ ] **Step 3: Update `src/main.c` to run BluePad32 and print mapped state**

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "uni.h"
#include "input/bluepad32_platform.h"

int main(void) {
    stdio_init_all();
    if (cyw43_arch_init()) { printf("cyw43 init failed\n"); return -1; }
    uni_platform_set_custom(get_ps2_platform());
    uni_init(0, NULL);
    btstack_run_loop_execute();   // BluePad32 owns the run loop on core0
    return 0;
}
```
(For this bring-up task, print `g_shared_input` from the platform's data callback via `printf` so state is visible over USB serial.)

- [ ] **Step 4: Re-run host tests to confirm mapping still holds after real macros**

Run: `cmake --build build-test && ctest --test-dir build-test --output-on-failure`
Expected: `test_gamepad_map` PASS (mapping unchanged by swapping in real BluePad32 macro values).

- [ ] **Step 5: Build firmware, flash, and verify on hardware**

Run:
```bash
cmake --build build
```
Flash `build/ps2_controller.uf2` (hold BOOTSEL, copy to the RPI-RP2 drive). Open USB serial (`screen /dev/tty.usbmodem* 115200` or `minicom`). Pair the controller.
Expected: serial shows "connected" and live button/stick values change as you move the controller — e.g. pressing Cross clears the Cross bit in `buttons2`, left stick moves `lx` toward 0x00/0xFF.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/main.c src/input/bluepad32_platform.h src/input/bluepad32_platform.c src/input/gamepad_map.c .gitmodules external/bluepad32
git commit -m "M1c: BluePad32 controller input mapped to shared PSXInputState"
```

---

## Phase M2 — PS2 transport / PIO (hardware-verified)

### Task 12: PIO SPI-slave transport

> Hardware-verified: needs a logic analyzer or a second Pico as bus master.

**Files:**
- Create: `src/ps2_device/psxSPI.pio`, `src/ps2_device/ps2_transport.h`, `src/ps2_device/ps2_transport.c`, `docs/wiring.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `void ps2_transport_init(void)`, `uint8_t ps2_recv_cmd(void)`, `void ps2_send(uint8_t)`, `void ps2_restart(void)` (`__time_critical_func`), and installs the SEL-rising ISR. Uses `pio0` (cyw43 uses `pio1` on the Pico W — verify no collision).

- [ ] **Step 1: Copy the PIO program and remap pins**

```bash
cp "$SCRATCH/DS4toPS2/psxSPI.pio" src/ps2_device/psxSPI.pio
```
Confirm the pin `.define`s (top of the file) match `docs/wiring.md`: `PIN_DAT 5, PIN_CMD 6, PIN_SEL 7, PIN_CLK 8, PIN_ACK 9`. The `SLOW_CLKDIV 50` and the `dat_writer` side-set-ACK/open-drain logic stay **unchanged** (Global Constraints).

- [ ] **Step 2: Write `docs/wiring.md`**

Document the PS2 controller-cable pinout mapped to the 5 GPIOs above, plus GND, the open-drain note, and dev-power via USB. (Pinout reference: psx-spx "Controllers and Memory Cards".)

- [ ] **Step 3: Write `ps2_transport.{h,c}`** by extracting the transport-only parts of `controller_simulator.cpp` (`init_pio`, `restart_pio_sm`, `selCallback`, `SEND`/`RECV_CMD`, the `read_byte_blocking`/`write_byte_blocking` from the `.pio` c-sdk block). Keep `restart_pio_sm` and the SEL callback in RAM (`__time_critical_func`). Do **not** include the protocol state machine here.

- [ ] **Step 4: Wire the PIO header generation into `CMakeLists.txt`**

```cmake
pico_generate_pio_header(ps2_controller ${CMAKE_CURRENT_SOURCE_DIR}/src/ps2_device/psxSPI.pio)
target_link_libraries(ps2_controller PUBLIC hardware_pio)
# add src/ps2_device/ps2_transport.c to add_executable(...)
pico_set_linker_script(ps2_controller ${CMAKE_CURRENT_SOURCE_DIR}/memmap.ld)
```

- [ ] **Step 5: Build; bench-verify the SM shifts and ACKs**

Run: `cmake --build build`, flash. Drive `ATT/CLK/CMD` from a second Pico (or capture with a logic analyzer) sending `01 42 …`.
Expected on the analyzer: the SM samples CMD on rising clock, drives DATA open-drain, and pulses ACK low ~2 µs after each byte. (Full protocol frame content comes in Task 13.)

- [ ] **Step 6: Commit**

```bash
git add src/ps2_device/psxSPI.pio src/ps2_device/ps2_transport.h src/ps2_device/ps2_transport.c docs/wiring.md CMakeLists.txt
git commit -m "M2: PIO SPI-slave transport (copied from fork, pins mapped)"
```

---

### Task 13: Core-1 protocol loop on the bench

> Hardware-verified: logic analyzer / second Pico.

**Files:**
- Create: `src/ps2_device/ps2_device.h`, `src/ps2_device/ps2_device.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ps2_transport_*` (Task 12), `ds2_response`/`ds2_apply_request` (M1a), `g_shared_input` (Task 11).
- Produces: `void ps2_device_thread(void)` for `multicore_launch_core1`.

- [ ] **Step 1: Write `ps2_device.c`** — the per-packet loop

```c
#include "ps2_device.h"
#include "ps2_transport.h"
#include "ds2_protocol.h"
#include "ds2_ids.h"
#include "input/bluepad32_platform.h"
#include "pico/multicore.h"

static ds2_state_t s_state;

void ps2_device_thread(void) {
    multicore_lockout_victim_init();
    ds2_init(&s_state);
    uint8_t resp[32];
    uint8_t req[32];
    while (true) {
        if (ps2_recv_cmd() != 0x01)   // address byte; ignore 0x81 (memory card)
            continue;
        uint8_t cmd = ps2_recv_cmd();
        PSXInputState in = g_shared_input;   // snapshot
        size_t rn = ds2_response(&s_state, cmd, &in, req, 0, resp, sizeof resp);
        // resp[0] is the ID byte, already implicitly sent as the leading byte by
        // the transport pipeline; send from resp[1] onward, capturing request bytes.
        size_t ri = 0;
        for (size_t i = 1; i < rn; i++) {
            ps2_send(resp[i]);
            if (ri < sizeof req) req[ri++] = ps2_recv_cmd();
        }
        ds2_apply_request(&s_state, cmd, req, ri);
    }
}
```
Note: reconcile the one-byte-ahead pipeline with `ps2_transport` exactly as `process_joy_req` does in the fork (`controller_simulator.cpp:53-55` sends the ID byte first). If the transport already sends the ID during the command-byte exchange, drop `resp[0]` here; if not, send `resp[0]` first. Verify against the fork's ordering during bring-up.

- [ ] **Step 2: Launch core1 from `main.c`** (temporary, for bench test — permanent lifecycle in Task 14)

Add after `cyw43_arch_init()` and `ps2_transport_init()`:
```c
    multicore_launch_core1(ps2_device_thread);
```

- [ ] **Step 3: Build; bench-verify the full poll frame**

Run: `cmake --build build`, flash. From the bus master send `01 42 00 00 00 00 00 00 00` with a centered/neutral shared state.
Expected DATA on the analyzer: `FF 73 5A FF FF 80 80 80 80` with an ACK low pulse after each byte **except the last**. Then send the config sequence (`01 43 00 01…`, `01 44 01 03…`, `01 43 00 00…`) and confirm the ID flips to `0x73` and stays (analog + lock).

- [ ] **Step 4: Commit**

```bash
git add src/ps2_device/ps2_device.h src/ps2_device/ps2_device.c CMakeLists.txt src/main.c
git commit -m "M2: core-1 DS2 protocol loop verified on bench"
```

---

## Phase M3/M4 — Real console integration & polish (hardware-verified)

### Task 14: Full lifecycle on a real PS2

**Files:**
- Modify: `src/main.c`

**Interfaces:**
- Consumes: everything above. Produces the shipping `main()`.

- [ ] **Step 1: Implement the connect/disconnect lifecycle** in `main.c`, modeled on `controller_simulator.cpp:404-462`: set the system clock (`set_sys_clock_khz(240000, true)` — verify it yields a clean 2.5 MHz PIO clkdiv), init cyw43 + BluePad32 on core0, `ps2_transport_init()`, enable the SEL IRQ and `multicore_launch_core1(ps2_device_thread)` when a controller connects, and `multicore_reset_core1()` + disable IRQ on disconnect. On disconnect, ensure `g_shared_input` is reset to `ds2_neutral_state()` so the console sees a centered, all-released pad rather than a dropout.

- [ ] **Step 2: Set DAT drive strength/slew** (from the fork)

```c
    gpio_set_slew_rate(PIN_DAT, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_DAT, GPIO_DRIVE_STRENGTH_12MA);
```

- [ ] **Step 3: Build, flash, and verify on a real PS2**

Wire the cut cable to the pins in `docs/wiring.md`, power the Pico via USB, plug into PS2 port 1.
Expected: the console shows a controller connected; in a game, digital buttons work and the analog sticks move. The pad is recognized as analog (test a game that shows the analog indicator).

- [ ] **Step 4: Commit**

```bash
git add src/main.c
git commit -m "M3: full lifecycle working on a real PS2"
```

---

### Task 15: Mapping completeness, PS1 edge check, docs

**Files:**
- Modify: `src/input/gamepad_map.c`, `src/ps2_device/psxSPI.pio` (only if the PS1 edge needs tweaking), `test/test_gamepad_map.c`, `README.md`

- [ ] **Step 1: Fill remaining button mappings + tests**

Add host tests for Start/Select/L1/R1/Triangle/Circle/Square/right-stick, then extend `map_gamepad_to_psx` until green. Run: `ctest --test-dir build-test --output-on-failure` → PASS.

- [ ] **Step 2: PS1 sample-edge check**

Plug into a PS1 (or run a PS1 title on the PS2). If input is erratic, adjust the `dat_writer`/`cmd_reader` clock-edge (`wait 1 gpio PIN_CLK` vs `wait 0`) per the documented PS1-vs-PS2 sampling quirk; re-verify on both consoles. Document the working setting in `docs/wiring.md`.

- [ ] **Step 3: Write `README.md`** — build/flash instructions, wiring, supported controllers, GPL-3.0 notice, and credits (DS4toPS2, PicoMemcard, BluePad32, BlueRetro).

- [ ] **Step 4: Commit**

```bash
git add src/input/gamepad_map.c test/test_gamepad_map.c src/ps2_device/psxSPI.pio README.md docs/wiring.md
git commit -m "M4: complete mapping, PS1 edge check, docs"
```

---

## Self-Review

**1. Spec coverage:**
- Dual-core architecture → Tasks 11 (core0/BluePad32), 13 (core1/protocol), 14 (lifecycle). ✓
- PIO shift + open-drain + ACK @2.5 MHz → Task 12 (copied `psxSPI.pio`). ✓
- Pure host-testable protocol core → Tasks 3–9. ✓
- BluePad32 input + mapping → Tasks 10 (pure), 11 (integration). ✓
- Analog poll frame `FF 73 5A …` → Task 4 + bench Task 13. ✓
- Config handshake (`0x43`/`0x44` + descriptor constants) → Tasks 7, 8, 9. ✓
- Address gating `0x01`/ignore `0x81` → Task 13 loop. ✓
- Transaction reset via SEL ISR → Task 12. ✓
- Neutral-on-disconnect → Task 14. ✓
- Error handling / RAM-resident timing → Tasks 12, 14. ✓
- Testing strategy (host unit + bench + in-game) → Tasks 2–10 (host), 12–15 (hardware). ✓
- Wiring / pins → Task 12 `docs/wiring.md`. ✓
- GPL-3.0 → Task 1. ✓
- Deferred (pressure/rumble/multitap) correctly excluded; `ds2_init` biases to analog (Task 4). ✓

**2. Placeholder scan:** The `BP_BTN_*` values in Task 10 are explicitly labeled placeholders for host self-containment and are replaced with real SDK macros in Task 11 (which re-runs the tests) — this is a deliberate, documented step, not a gap. No "TBD"/"add error handling"/"write tests for the above" left.

**3. Type consistency:** `ds2_state_t`, `PSXInputState`, `gamepad_snapshot_t`, and the `ds2_response`/`ds2_apply_request`/`map_gamepad_to_psx` signatures are used identically across the tasks that define and consume them. `MODE_*`/`CMD_*`/`PS_*` macros are defined once in `ds2_ids.h`. The one open reconciliation — whether the transport sends the ID byte itself (Task 13 note) — is flagged explicitly to check against the fork's ordering during bring-up.
