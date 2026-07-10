# Dual Controller Ports Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emulate two independent PS2 DualShock 2 controllers on two console ports, each fed by its own Bluetooth pad, without breaking the core-isolation model.

**Architecture:** Turn today's singletons into two port instances (port 0 = `pio0`/GP5–9, port 1 = `pio1`/GP10–14). One `psxSPI.pio` image, rewritten to relative (IN-base) pin addressing, serves both. core1 is launched **once** and runs a single loop that polls both ports and services whichever the console is clocking (the PS2 SIO selects ports sequentially, so at most one is active at a time). core0/BluePad32 routes two pads to ports by connection order.

**Tech Stack:** C11, Pico SDK 2.3.0 (RP2350), `hardware_pio`, `pico_multicore`, Unity (host tests).

**Spec:** `docs/superpowers/specs/2026-07-09-dual-controller-ports-design.md`

## Global Constraints

- **PIO ownership:** core1 is the **sole owner** of all PS2 PIO (`pio0` + `pio1`). core0 runs BluePad32/Bluetooth only — no PS2-timing work, no PS2 PIO FIFO access. The SEL ISR (core0) sets a per-port `volatile` flag and touches no PIO.
- **Ports:** `PS2_NUM_PORTS == 2`. Port 0 pins **GP5,6,7,8,9** (DAT,CMD,SEL,CLK,ACK) unchanged. Port 1 pins **GP10,11,12,13,14**. Each port's five signals MUST be consecutive (`pin_base` = DAT; CMD=+1, SEL=+2, CLK=+3, ACK=+4) — required by the relative-pin PIO.
- **System clock stays 125 MHz** (`SYS_CLOCK_KHZ` in main.c); `SLOW_CLKDIV 50` → 2.5 MHz PIO unchanged.
- **One PIO program image** (relative pins) for both ports. Do not emit a per-port image unless the relative approach fails bench validation (documented fallback).
- **Reuse `ds2_protocol` unchanged** — one `ds2_state_t` per port; no protocol edits.
- **Buttons active-low**, sticks neutral `0x80` (unchanged `PSXInputState`).
- **No hardware available:** firmware tasks are verified by a clean cross-compile to `build/ps2_controller.uf2`. Only the pure port-router is host-tested. Real two-port bus behavior is validated later per `docs/bringup.md`.
- **Board:** `PICO_BOARD=pimoroni_pico_plus2_w_rp2350` (already set).

## Build & Test Commands

- **Host tests:** `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
- **Firmware:** `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build` → `build/ps2_controller.uf2`.

## File Structure

- `src/input/port_router.{h,c}` — **new**, pure: connection-order device→port assignment. Host-tested.
- `src/input/shared_input.{h,c}` — 1 slot → array of `PS2_NUM_PORTS`.
- `src/ps2_device/psxSPI.pio` — absolute-pin waits → relative (IN-base) waits; init takes `pin_base`.
- `src/ps2_device/ps2_transport.{h,c}` — global statics → `ps2_transport_t` instances + shared SEL-ISR routing by GPIO.
- `src/ps2_device/ps2_device.{h,c}` — 1 `ds2_state` → 2; launch-once core1 loop polling both ports; flag-based start/stop.
- `src/input/bluepad32_platform.c` — device→port routing via `port_router`.
- `src/main.c` — init both transports, `ps2_device_global_init()` (launch core1 once).
- `test/test_port_router.c` — **new** Unity tests.
- `CMakeLists.txt`, `test/CMakeLists.txt` — new sources.

---

### Task 1: Pure `port_router` module — TDD

**Files:**
- Create: `src/input/port_router.h`, `src/input/port_router.c`, `test/test_port_router.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `#define PS2_NUM_PORTS 2`, `#define PORT_NONE (-1)`
  - `typedef struct { const void *owner[PS2_NUM_PORTS]; } port_router_t;`
  - `void port_router_init(port_router_t *);`
  - `int  port_router_assign(port_router_t *, const void *dev);` — lowest free port, or `PORT_NONE` if full (or dev already assigned → returns its port).
  - `int  port_router_lookup(const port_router_t *, const void *dev);` — port or `PORT_NONE`.
  - `void port_router_release(port_router_t *, const void *dev);`

- [ ] **Step 1: Write the header**

Create `src/input/port_router.h`:

```c
#ifndef PORT_ROUTER_H
#define PORT_ROUTER_H

// Connection-order assignment of Bluetooth controllers to PS2 ports.
// Pure and host-testable: devices are opaque non-NULL pointers (NULL = empty).

#define PS2_NUM_PORTS 2
#define PORT_NONE (-1)

typedef struct {
    const void *owner[PS2_NUM_PORTS];
} port_router_t;

void port_router_init(port_router_t *r);

// Assign dev to the lowest free port. If dev is already assigned, returns its
// existing port. Returns PORT_NONE if all ports are taken. NULL dev -> PORT_NONE.
int port_router_assign(port_router_t *r, const void *dev);

// Return the port dev is assigned to, or PORT_NONE.
int port_router_lookup(const port_router_t *r, const void *dev);

// Free whatever port dev holds (no-op if dev is unassigned or NULL).
void port_router_release(port_router_t *r, const void *dev);

#endif // PORT_ROUTER_H
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_port_router.c`:

```c
#include "unity/unity.h"
#include "port_router.h"

void setUp(void) {}
void tearDown(void) {}

static const void *A = (const void *)1;
static const void *B = (const void *)2;
static const void *C = (const void *)3;

static void test_first_gets_port0_second_port1(void) {
    port_router_t r; port_router_init(&r);
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, A));
    TEST_ASSERT_EQUAL_INT(1, port_router_assign(&r, B));
}

static void test_full_returns_none(void) {
    port_router_t r; port_router_init(&r);
    port_router_assign(&r, A);
    port_router_assign(&r, B);
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_assign(&r, C));
}

static void test_reassign_same_device_is_idempotent(void) {
    port_router_t r; port_router_init(&r);
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, A));
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, A));  // same port
}

static void test_lookup(void) {
    port_router_t r; port_router_init(&r);
    port_router_assign(&r, A);
    port_router_assign(&r, B);
    TEST_ASSERT_EQUAL_INT(0, port_router_lookup(&r, A));
    TEST_ASSERT_EQUAL_INT(1, port_router_lookup(&r, B));
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_lookup(&r, C));
}

static void test_release_frees_lowest_slot_for_reuse(void) {
    port_router_t r; port_router_init(&r);
    port_router_assign(&r, A);           // port 0
    port_router_assign(&r, B);           // port 1
    port_router_release(&r, A);          // free port 0
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_lookup(&r, A));
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, C));   // C reuses port 0
    TEST_ASSERT_EQUAL_INT(1, port_router_lookup(&r, B));   // B untouched
}

static void test_null_and_unknown_are_safe(void) {
    port_router_t r; port_router_init(&r);
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_assign(&r, NULL));
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_lookup(&r, NULL));
    port_router_release(&r, NULL);       // no crash
    port_router_release(&r, C);          // unknown: no crash
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_gets_port0_second_port1);
    RUN_TEST(test_full_returns_none);
    RUN_TEST(test_reassign_same_device_is_idempotent);
    RUN_TEST(test_lookup);
    RUN_TEST(test_release_frees_lowest_slot_for_reuse);
    RUN_TEST(test_null_and_unknown_are_safe);
    return UNITY_END();
}
```

- [ ] **Step 3: Wire the test into CMake**

Append to `test/CMakeLists.txt`:

```cmake
add_executable(test_port_router test_port_router.c ../src/input/port_router.c)
target_link_libraries(test_port_router unity)
add_test(NAME test_port_router COMMAND test_port_router)
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `cmake -S test -B test/build && cmake --build test/build`
Expected: link failure — `port_router.c` not implemented (undefined references).

- [ ] **Step 5: Implement**

Create `src/input/port_router.c`:

```c
#include "port_router.h"
#include <stddef.h>

void port_router_init(port_router_t *r) {
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        r->owner[i] = NULL;
}

int port_router_lookup(const port_router_t *r, const void *dev) {
    if (dev == NULL)
        return PORT_NONE;
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        if (r->owner[i] == dev)
            return i;
    return PORT_NONE;
}

int port_router_assign(port_router_t *r, const void *dev) {
    if (dev == NULL)
        return PORT_NONE;
    int existing = port_router_lookup(r, dev);
    if (existing != PORT_NONE)
        return existing;
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        if (r->owner[i] == NULL) {
            r->owner[i] = dev;
            return i;
        }
    return PORT_NONE;
}

void port_router_release(port_router_t *r, const void *dev) {
    if (dev == NULL)
        return;
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        if (r->owner[i] == dev)
            r->owner[i] = NULL;
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: `test_port_router` PASSES (6), other suites still pass.

- [ ] **Step 7: Commit**

```bash
git add src/input/port_router.h src/input/port_router.c test/test_port_router.c test/CMakeLists.txt
git commit -m "feat: pure connection-order port router with host tests"
```

---

### Task 2: Relative-pin PIO + `ps2_transport_t` instances (port 0 only wired)

**Files:**
- Modify: `src/ps2_device/psxSPI.pio`
- Modify: `src/ps2_device/ps2_transport.h`, `src/ps2_device/ps2_transport.c`
- Modify: `src/ps2_device/ps2_device.c` (use one `ps2_transport_t` instance)

**Interfaces:**
- Produces (relied on by Tasks 3–4):
  - `typedef struct { PIO pio; uint sm_cmd, sm_dat; uint off_cmd, off_dat; uint pin_base; void (*sel_hook)(void); } ps2_transport_t;`
  - `void ps2_transport_global_init(void);` — registers the shared SEL GPIO callback once.
  - `void ps2_transport_init(ps2_transport_t *t, PIO pio, uint pin_base);` — claims 2 SMs on `pio`, loads the program (once per PIO), inits SMs for `pin_base`.
  - `bool ps2_try_recv_cmd(ps2_transport_t *t, uint8_t *out);`
  - `bool ps2_try_send(ps2_transport_t *t, uint8_t byte);`
  - `void ps2_restart_pio(ps2_transport_t *t);`
  - `void ps2_transport_set_sel_hook(ps2_transport_t *t, void (*hook)(void));`
  - `void ps2_transport_enable_sel(ps2_transport_t *t, bool enabled);`

**This task keeps only port 0 (pio0/GP5–9) wired**, so it is a faithful refactor: behavior must be identical to today. It is the highest-risk change (timing-critical PIO); flag for careful bench validation.

- [ ] **Step 1: Rewrite `psxSPI.pio` to relative pins**

Replace the three `.program` bodies' absolute `gpio` waits with IN-base-relative `pin` waits, and rewrite the c-sdk init helpers to take `pin_base`. Full new file:

```
;	Interfaces with the modified SPI protocol used by PSX. Rewritten to use
;	IN-base-relative pin addressing so ONE image serves any port whose
;	DAT,CMD,SEL,CLK,ACK are consecutive GPIOs (pin_base = DAT).
;	Offsets from pin_base: DAT 0, CMD 1, SEL 2, CLK 3, ACK 4.

.program cmd_reader
; IN base = CMD (pin_base+1): so SEL = base+1, CLK = base+2, CMD = base+0.
wait 0 pin 1			; wait for SEL (base+1) low
set x, 7
.wrap_target
wait 0 pin 2			; wait for CLK (base+2) to fall
wait 1 pin 2			; wait for rising CLK
in pins 1				; sample 1 bit from CMD (base+0)
.wrap

.program dat_writer
.side_set 1 pindirs
; IN base = SEL (pin_base+2): SEL = base+0, CLK = base+1.
; OUT/SET base = DAT; sideset = ACK.
set pindirs, 0			side 0	; release DAT (Hi-Z = logic 1)
wait 0 pin 0			side 0	; wait for SEL (in_base+0) low
.wrap_target
pull					side 0
nop						side 1 [5]		; start ACK
set x, 7				side 0 [5]
sendbit:
wait 1 pin 1			side 0			; CLK (in_base+1) high
wait 0 pin 1			side 0			; CLK falling edge
out pindirs 1			side 0
jmp x-- sendbit			side 0
.wrap

% c-sdk {
#define SLOW_CLKDIV 50	// 125MHz / 50 = 2.5 MHz

// pin_base = DAT; CMD=+1, SEL=+2, CLK=+3, ACK=+4.
static inline void cmd_reader_program_init(PIO pio, uint sm, uint offset, uint pin_base) {
	pio_sm_config c = cmd_reader_program_get_default_config(offset);
	sm_config_set_in_pins(&c, pin_base + 1);            // IN base = CMD
	pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 5, false);  // all 5 as input initially
	sm_config_set_in_shift(&c, true, true, 8);
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
	sm_config_set_clkdiv_int_frac(&c, SLOW_CLKDIV, 0x00);
	pio_sm_init(pio, sm, offset, &c);
}

static inline void dat_writer_program_init(PIO pio, uint sm, uint offset, uint pin_base) {
	pio_sm_config c = dat_writer_program_get_default_config(offset);
	sm_config_set_out_pins(&c, pin_base + 0, 1);        // OUT base = DAT
	sm_config_set_set_pins(&c, pin_base + 0, 1);        // SET base = DAT
	sm_config_set_sideset_pins(&c, pin_base + 4);       // sideset = ACK
	sm_config_set_in_pins(&c, pin_base + 2);            // IN base = SEL (for CLK/SEL waits)

	// DAT (base+0) and ACK (base+4) open-drain: output-low value, input dir initially.
	pio_sm_set_pins_with_mask(pio, sm, 0, (1u << (pin_base + 0)) | (1u << (pin_base + 4)));
	pio_sm_set_consecutive_pindirs(pio, sm, pin_base + 0, 1, false);
	pio_gpio_init(pio, pin_base + 0);
	pio_sm_set_consecutive_pindirs(pio, sm, pin_base + 4, 1, false);
	pio_gpio_init(pio, pin_base + 4);

	sm_config_set_out_shift(&c, true, true, 8);
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
	sm_config_set_clkdiv_int_frac(&c, SLOW_CLKDIV, 0x00);
	pio_sm_init(pio, sm, offset, &c);
}

static inline uint8_t read_byte_blocking(PIO pio, uint sm) {
	return (uint8_t)(pio_sm_get_blocking(pio, sm) >> 24);
}
static inline void write_byte_blocking(PIO pio, uint sm, uint32_t byte) {
	pio_sm_put_blocking(pio, sm, ~byte & 0xFF);
}
%}
```

NOTE (verify on bench): the `dat_reader` program was unused and is dropped. Relative offsets assume the exact GP5–9 / GP10–14 consecutive layout. If two ports on different `pio` blocks need the same program offset, `pio_add_program` is called once per PIO block (see init below).

- [ ] **Step 2: Rewrite `ps2_transport.h`**

```c
#ifndef PS2_TRANSPORT_H
#define PS2_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/pio.h"

// One PS2 controller-port bus instance. DAT/CMD/SEL/CLK/ACK are consecutive
// GPIOs starting at pin_base. After init, all FIFO access is core1-only.
typedef struct {
    PIO  pio;
    uint sm_cmd;
    uint sm_dat;
    uint off_cmd;
    uint off_dat;
    uint pin_base;               // DAT; CMD=+1, SEL=+2, CLK=+3, ACK=+4
    void (*sel_hook)(void);
} ps2_transport_t;

// Register the shared SEL-rising GPIO callback once (core0), before any init.
void ps2_transport_global_init(void);

// Claim 2 SMs on `pio` and configure them for `pin_base`. The program is added
// to each PIO block on first use.
void ps2_transport_init(ps2_transport_t *t, PIO pio, uint pin_base);

bool ps2_try_recv_cmd(ps2_transport_t *t, uint8_t *out);   // core1
bool ps2_try_send(ps2_transport_t *t, uint8_t byte);       // core1
void ps2_restart_pio(ps2_transport_t *t);                  // core1, between transactions

void ps2_transport_set_sel_hook(ps2_transport_t *t, void (*hook)(void));
void ps2_transport_enable_sel(ps2_transport_t *t, bool enabled);

#endif // PS2_TRANSPORT_H
```

- [ ] **Step 3: Rewrite `ps2_transport.c`**

```c
#include "ps2_transport.h"

#include <stddef.h>
#include "hardware/gpio.h"
#include "pico/platform.h"

#include "psxSPI.pio.h"

#define SEL_OFFSET 2u   // SEL = pin_base + 2

// Registry so the shared SEL ISR can map a GPIO back to its transport. Small
// fixed set (one per port). Registered in ps2_transport_init (core0).
#define MAX_TRANSPORTS 2
static ps2_transport_t *s_registry[MAX_TRANSPORTS];
static size_t s_registry_n;

bool ps2_try_recv_cmd(ps2_transport_t *t, uint8_t *out) {
    if (pio_sm_is_rx_fifo_empty(t->pio, t->sm_cmd))
        return false;
    *out = (uint8_t)(pio_sm_get(t->pio, t->sm_cmd) >> 24);
    return true;
}

bool ps2_try_send(ps2_transport_t *t, uint8_t byte) {
    if (pio_sm_is_tx_fifo_full(t->pio, t->sm_dat))
        return false;
    pio_sm_put(t->pio, t->sm_dat, ~(uint32_t)byte & 0xFFu);
    return true;
}

void __time_critical_func(ps2_restart_pio)(ps2_transport_t *t) {
    const uint32_t mask = (1u << t->sm_cmd) | (1u << t->sm_dat);
    pio_set_sm_mask_enabled(t->pio, mask, false);
    pio_restart_sm_mask(t->pio, mask);
    pio_sm_exec(t->pio, t->sm_cmd, pio_encode_jmp(t->off_cmd));
    pio_sm_exec(t->pio, t->sm_dat, pio_encode_jmp(t->off_dat));
    pio_sm_clear_fifos(t->pio, t->sm_cmd);
    pio_sm_drain_tx_fifo(t->pio, t->sm_dat);
    pio_enable_sm_mask_in_sync(t->pio, mask);
}

// Shared SEL-rising ISR (core0). Routes by GPIO to the owning transport's hook.
// Touches no PIO — core1 stays the sole PIO owner.
static void __time_critical_func(sel_isr)(uint gpio, uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_RISE))
        return;
    for (size_t i = 0; i < s_registry_n; i++) {
        ps2_transport_t *t = s_registry[i];
        if (gpio == t->pin_base + SEL_OFFSET) {
            if (t->sel_hook)
                t->sel_hook();
            return;
        }
    }
}

void ps2_transport_set_sel_hook(ps2_transport_t *t, void (*hook)(void)) {
    t->sel_hook = hook;
}

void ps2_transport_enable_sel(ps2_transport_t *t, bool enabled) {
    gpio_set_irq_enabled(t->pin_base + SEL_OFFSET, GPIO_IRQ_EDGE_RISE, enabled);
}

void ps2_transport_global_init(void) {
    // Register the shared callback once with a disabled dummy pin; per-port pins
    // are enabled later via ps2_transport_enable_sel. Using the SDK shared
    // handler means it ACKs the IRQ for us.
    gpio_set_irq_callback(sel_isr);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

void ps2_transport_init(ps2_transport_t *t, PIO pio, uint pin_base) {
    t->pio = pio;
    t->pin_base = pin_base;
    t->sel_hook = NULL;

    for (uint i = 0; i < 5; i++) {          // DAT..ACK as inputs, no pulls
        gpio_set_dir(pin_base + i, GPIO_IN);
        gpio_disable_pulls(pin_base + i);
    }

    t->sm_cmd = (uint)pio_claim_unused_sm(pio, true);
    t->sm_dat = (uint)pio_claim_unused_sm(pio, true);
    t->off_cmd = (uint)pio_add_program(pio, &cmd_reader_program);
    t->off_dat = (uint)pio_add_program(pio, &dat_writer_program);
    cmd_reader_program_init(pio, t->sm_cmd, t->off_cmd, pin_base);
    dat_writer_program_init(pio, t->sm_dat, t->off_dat, pin_base);

    // Fast, strong DAT drive for a clean falling edge.
    gpio_set_slew_rate(pin_base + 0, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(pin_base + 0, GPIO_DRIVE_STRENGTH_12MA);

    if (s_registry_n < MAX_TRANSPORTS)
        s_registry[s_registry_n++] = t;
}
```

- [ ] **Step 4: Update `ps2_device.c` to hold one transport instance**

In `src/ps2_device/ps2_device.c`, add a file-scope transport and route the existing calls through it. Change the transport-touching lines:

- Add near the top (after includes): `static ps2_transport_t s_transport;`
- `ps2_try_recv_cmd(out)` → `ps2_try_recv_cmd(&s_transport, out)` (in `recv_cmd`)
- `ps2_try_send(byte)` → `ps2_try_send(&s_transport, byte)` (in `send_dat`)
- `ps2_restart_pio()` → `ps2_restart_pio(&s_transport)` (in `ps2_device_thread`)
- In `ps2_device_start()`: replace `ps2_transport_set_sel_hook(ps2_signal_restart)` / `ps2_transport_enable_sel(true)` with `ps2_transport_set_sel_hook(&s_transport, ps2_signal_restart); ps2_transport_enable_sel(&s_transport, true);`
- In `ps2_device_stop()`: `ps2_transport_enable_sel(&s_transport, false); ps2_transport_set_sel_hook(&s_transport, NULL);`
- The transport is initialized in `main.c` (next: temporarily add `ps2_transport_global_init()` + `ps2_transport_init(&s_transport, pio0, 5)` there). Expose it: add `extern ps2_transport_t *ps2_device_transport0(void);` returning `&s_transport`, OR simpler — move `ps2_transport_init` for port 0 into `ps2_device.c` behind a new `ps2_device_global_init()` that Tasks 3–4 will extend. For this task, add:

```c
void ps2_device_global_init(void) {
    ps2_transport_global_init();
    ps2_transport_init(&s_transport, pio0, 5);
}
```
and declare it in `ps2_device.h`.

- In `main.c`: add `#include "ps2_device.h"` (near the other includes) and remove the now-unused `#include "ps2_transport.h"`; replace the old `ps2_transport_init();` call (line ~40) with `ps2_device_global_init();`.

- [ ] **Step 5: Cross-compile**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build`
Expected: `build/ps2_controller.uf2` builds clean. Behavior for port 0 is intended to be identical (relative addressing resolves to the same GP5–9). **Flag in the report that the PIO change requires bench validation.**

- [ ] **Step 6: Commit**

```bash
git add src/ps2_device/psxSPI.pio src/ps2_device/ps2_transport.h src/ps2_device/ps2_transport.c src/ps2_device/ps2_device.c src/ps2_device/ps2_device.h src/main.c
git commit -m "refactor: relative-pin PIO + ps2_transport_t instances (port 0 wired)"
```

---

### Task 3: `shared_input` → per-port slots

**Files:**
- Modify: `src/input/shared_input.h`, `src/input/shared_input.c`
- Modify: `src/ps2_device/ps2_device.c`, `src/input/bluepad32_platform.c` (pass port 0 for now)

**Interfaces:**
- Produces: `shared_input_publish(unsigned port, const PSXInputState*)`, `shared_input_snapshot(unsigned port)`, `shared_input_set_connected(unsigned port, bool)`, `shared_input_connected(unsigned port)`; `shared_input_init(void)` unchanged.

- [ ] **Step 1: Rewrite `shared_input.h`**

```c
#ifndef SHARED_INPUT_H
#define SHARED_INPUT_H
#include <stdbool.h>
#include "input_state.h"
#include "port_router.h"   // PS2_NUM_PORTS

// Per-port cross-core publish/snapshot (seqlock). Single writer per slot
// (core0), single reader per slot (core1). `port` must be < PS2_NUM_PORTS.
void          shared_input_init(void);
void          shared_input_publish(unsigned port, const PSXInputState *s);
PSXInputState shared_input_snapshot(unsigned port);
void          shared_input_set_connected(unsigned port, bool connected);
bool          shared_input_connected(unsigned port);

#endif // SHARED_INPUT_H
```

- [ ] **Step 2: Rewrite `shared_input.c`**

```c
#include "shared_input.h"
#include "hardware/sync.h"   // __dmb()

// One seqlock slot per port. Even seq == stable, odd == write in progress.
typedef struct {
    PSXInputState     state;
    volatile uint32_t seq;
    volatile bool     connected;
} slot_t;

static slot_t s_slot[PS2_NUM_PORTS];

void shared_input_init(void) {
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++) {
        s_slot[p].state = ds2_neutral_state();
        s_slot[p].seq = 0;
        s_slot[p].connected = false;
    }
}

void shared_input_publish(unsigned port, const PSXInputState *s) {
    slot_t *sl = &s_slot[port];
    uint32_t seq = sl->seq + 1u;
    sl->seq = seq;
    __dmb();
    sl->state = *s;
    __dmb();
    sl->seq = seq + 1u;
}

PSXInputState shared_input_snapshot(unsigned port) {
    slot_t *sl = &s_slot[port];
    PSXInputState out;
    uint32_t before, after;
    do {
        before = sl->seq;
        __dmb();
        out = sl->state;
        __dmb();
        after = sl->seq;
    } while ((before & 1u) || before != after);
    return out;
}

void shared_input_set_connected(unsigned port, bool connected) { s_slot[port].connected = connected; }
bool shared_input_connected(unsigned port) { return s_slot[port].connected; }
```

- [ ] **Step 3: Update callers to pass port 0 (temporary)**

- `ps2_device.c` `process_one_transaction`: `shared_input_snapshot()` → `shared_input_snapshot(0)`.
- `ps2_device.c` `ps2_device_stop`: `shared_input_publish(&neutral)` → `shared_input_publish(0, &neutral)`.
- `bluepad32_platform.c`:
  - `ps2_platform_init`: `shared_input_init();` unchanged.
  - `on_device_disconnected`: `shared_input_set_connected(false)` → `shared_input_set_connected(0, false)`.
  - `on_device_ready`: `shared_input_set_connected(true)` → `shared_input_set_connected(0, true)`.
  - `on_controller_data`: `shared_input_publish(&st)` → `shared_input_publish(0, &st)`.
  - `bp_controller_connected`: `shared_input_connected()` → `shared_input_connected(0)`.

- [ ] **Step 4: Cross-compile + host tests**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build` then `ctest --test-dir test/build --output-on-failure`
Expected: firmware builds; host suites still pass (port_router + prior).

- [ ] **Step 5: Commit**

```bash
git add src/input/shared_input.h src/input/shared_input.c src/ps2_device/ps2_device.c src/input/bluepad32_platform.c
git commit -m "refactor: per-port shared_input slots (callers pass port 0)"
```

---

### Task 4: `ps2_device` — two ports, launch-once core1 loop, flag lifecycle

**Files:**
- Modify: `src/ps2_device/ps2_device.h`, `src/ps2_device/ps2_device.c`

**Interfaces:**
- Produces: `void ps2_device_global_init(void);` (inits both transports + launches core1 once), `void ps2_device_start(unsigned port);`, `void ps2_device_stop(unsigned port);`.

Key change: core1 is launched **once** by `ps2_device_global_init()` and runs forever, polling both ports; `start`/`stop` only flip a per-port `active` flag (no `multicore_launch/reset`). Because the PS2 SIO selects ports sequentially, the loop services at most one active transaction at a time; a port with no incoming byte is simply skipped.

- [ ] **Step 1: Rewrite `ps2_device.h`**

```c
#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

#include "port_router.h"   // PS2_NUM_PORTS

// Initialize both port transports and launch the single core1 loop that
// services all ports. Call once at startup from core0, after the system clock
// is set and before controllers connect.
void ps2_device_global_init(void);

// Bring port `port` online / offline (core0, on connect / disconnect): flips the
// port's active flag, (dis)arms its SEL IRQ, and resets/neutralizes its state.
// Does NOT launch or reset core1 (that happens once in ps2_device_global_init).
void ps2_device_start(unsigned port);
void ps2_device_stop(unsigned port);

#endif // PS2_DEVICE_H
```

- [ ] **Step 2: Rewrite `ps2_device.c`**

```c
#include "ps2_device.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"     // tight_loop_contents()
#include "hardware/pio.h"

#include "ds2_protocol.h"
#include "ps2_transport.h"
#include "shared_input.h"

#define PS2_ADDR_CONTROLLER 0x01u   // 0x81 = memory card, ignored (stay Hi-Z)
#define RESP_CAP 32
#define REQ_CAP  32

// Per-port state. port 0 = pio0/GP5-9, port 1 = pio1/GP10-14.
static ps2_transport_t  s_transport[PS2_NUM_PORTS];
static ds2_state_t      s_ds2[PS2_NUM_PORTS];
static volatile bool    s_active[PS2_NUM_PORTS];
static volatile bool    s_restart[PS2_NUM_PORTS];   // set by SEL ISR (core0)

static void ps2_signal_restart_p0(void) { s_restart[0] = true; }
static void ps2_signal_restart_p1(void) { s_restart[1] = true; }
static void (*const s_restart_hook[PS2_NUM_PORTS])(void) = {
    ps2_signal_restart_p0, ps2_signal_restart_p1,
};

// Wait for one CMD byte on `port`, bailing if its transaction ended. core1.
static bool recv_cmd(unsigned port, uint8_t *out) {
    while (!s_restart[port]) {
        if (ps2_try_recv_cmd(&s_transport[port], out))
            return true;
        tight_loop_contents();
    }
    return false;
}

static bool send_dat(unsigned port, uint8_t byte) {
    while (!s_restart[port]) {
        if (ps2_try_send(&s_transport[port], byte))
            return true;
        tight_loop_contents();
    }
    return false;
}

// Continue a transaction on `port` after its address byte was already read.
static void process_transaction(unsigned port, uint8_t addr) {
    if (addr != PS2_ADDR_CONTROLLER)
        return;                                  // 0x81 memcard: stay Hi-Z

    ds2_state_t *st = &s_ds2[port];
    if (!send_dat(port, ds2_id_byte(st)))
        return;
    uint8_t cmd;
    if (!recv_cmd(port, &cmd))
        return;

    PSXInputState in = shared_input_snapshot(port);
    uint8_t resp[RESP_CAP], req[REQ_CAP];
    size_t rn = ds2_response(st, cmd, &in, NULL, 0, resp, sizeof resp);

    size_t ri = 0;
    for (size_t i = 1; i < rn; i++) {
        if (!send_dat(port, resp[i]))
            return;
        uint8_t rx;
        if (!recv_cmd(port, &rx))
            return;
        if (ri < sizeof req)
            req[ri++] = rx;
    }
    ds2_apply_request(st, cmd, req, ri);
}

// Single core1 loop for all ports. The PS2 SIO selects ports sequentially, so
// at most one port has an in-flight transaction; we poll both and service the
// one whose address byte arrived, then re-sync that port after its SEL rise.
static void ps2_device_thread(void) {
    multicore_lockout_victim_init();

    for (;;) {
        for (unsigned p = 0; p < PS2_NUM_PORTS; p++) {
            if (!s_active[p])
                continue;

            uint8_t addr;
            if (!ps2_try_recv_cmd(&s_transport[p], &addr))
                continue;                        // no transaction on this port now

            s_restart[p] = false;
            process_transaction(p, addr);

            // Wait for the console to finish (SEL rise) or the port to go
            // inactive, then re-sync this port's PIO on core1.
            while (!s_restart[p] && s_active[p])
                tight_loop_contents();
            ps2_restart_pio(&s_transport[p]);
            s_restart[p] = false;
        }
    }
}

void ps2_device_global_init(void) {
    ps2_transport_global_init();
    ps2_transport_init(&s_transport[0], pio0, 5);
    ps2_transport_init(&s_transport[1], pio1, 10);
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++) {
        ps2_transport_set_sel_hook(&s_transport[p], s_restart_hook[p]);
        s_active[p] = false;
        s_restart[p] = false;
    }
    multicore_launch_core1(ps2_device_thread);   // one-time; services all ports
}

void ps2_device_start(unsigned port) {
    ds2_init(&s_ds2[port]);
    s_restart[port] = false;
    ps2_transport_enable_sel(&s_transport[port], true);
    s_active[port] = true;
}

void ps2_device_stop(unsigned port) {
    s_active[port] = false;
    ps2_transport_enable_sel(&s_transport[port], false);
    PSXInputState neutral = ds2_neutral_state();
    shared_input_publish(port, &neutral);
}
```

- [ ] **Step 3: Cross-compile**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build`
Expected: builds clean. `ps2_device_start/stop` now take a port; the next task updates the callers. If the platform callers still call the old 0-arg forms the build will fail — that is fixed in Task 5, so if building standalone, expect the platform to be updated together. (Implement Task 5 before re-building if needed.)

- [ ] **Step 4: Commit**

```bash
git add src/ps2_device/ps2_device.h src/ps2_device/ps2_device.c
git commit -m "feat: two-port ps2_device with launch-once core1 loop and flag lifecycle"
```

---

### Task 5: BluePad32 routing + `main.c` wiring

**Files:**
- Modify: `src/input/bluepad32_platform.c`
- Modify: `src/main.c`

**Interfaces:**
- Consumes: `port_router` (Task 1), `ps2_device_global_init/start/stop(port)` (Task 4), `shared_input_*(port)` (Task 3).

- [ ] **Step 1: Add the router and route callbacks in `bluepad32_platform.c`**

Add include (after `#include "ps2_device.h"`): `#include "port_router.h"`

Add a file-scope router after the static_asserts: `static port_router_t s_router;`

In `ps2_platform_init`, after `shared_input_init();` add: `port_router_init(&s_router);`

Rewrite the four callbacks:

```c
static void ps2_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("ps2_platform: device disconnected: %p\n", d);
    int port = port_router_lookup(&s_router, d);
    if (port == PORT_NONE)
        return;
    shared_input_set_connected((unsigned)port, false);
    ps2_device_stop((unsigned)port);
    port_router_release(&s_router, d);
}

static uni_error_t ps2_platform_on_device_ready(uni_hid_device_t* d) {
    logi("ps2_platform: device ready: %p\n", d);
    int port = port_router_assign(&s_router, d);
    if (port == PORT_NONE) {
        logi("ps2_platform: both ports in use; ignoring extra controller\n");
        return UNI_ERROR_SUCCESS;
    }
    shared_input_set_connected((unsigned)port, true);
    ps2_device_start((unsigned)port);
    return UNI_ERROR_SUCCESS;
}

static void ps2_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;
    int port = port_router_lookup(&s_router, d);
    if (port == PORT_NONE)
        return;

    const uni_gamepad_t* gp = &ctl->gamepad;
    gamepad_snapshot_t snap = {
        .dpad = gp->dpad, .buttons = gp->buttons, .misc_buttons = gp->misc_buttons,
        .axis_x = gp->axis_x, .axis_y = gp->axis_y,
        .axis_rx = gp->axis_rx, .axis_ry = gp->axis_ry,
        .brake = gp->brake, .throttle = gp->throttle,
    };
    PSXInputState st;
    map_gamepad_to_psx(&snap, &st);
    shared_input_publish((unsigned)port, &st);
}
```

Update `bp_controller_connected` to report "any port connected":

```c
bool bp_controller_connected(void) {
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++)
        if (shared_input_connected(p))
            return true;
    return false;
}
```

(Note: `ps2_platform_on_controller_data` previously used `ARG_UNUSED(d)` — remove that; `d` is now used.)

- [ ] **Step 2: Wire `main.c`**

`main.c` already calls `ps2_device_global_init()` (added in Task 2 Step 4). Confirm it initializes both transports now (Task 4 rewrote it to init port 0 and port 1 and launch core1). No further change needed beyond ensuring the old `ps2_transport_init()` line is gone. Verify `main.c` reads:

```c
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();
    status_indicator_init();
    if (cyw43_arch_init()) { ... status_indicator_set(STATUS_ERROR); while(true) tight_loop_contents(); }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    ps2_device_global_init();          // inits both transports + launches core1 once
    uni_platform_set_custom(get_ps2_platform());
    uni_init(0, NULL);
    btstack_run_loop_execute();
```

- [ ] **Step 3: Cross-compile + host tests**

Run: `PICO_SDK_PATH=$HOME/pico-sdk cmake -B build && cmake --build build` then `ctest --test-dir test/build --output-on-failure`
Expected: firmware builds clean; all host suites pass (`test_port_router`, `test_status_color`, `test_gamepad_map`, `test_ds2_protocol`, `test_smoke`).

- [ ] **Step 4: Commit**

```bash
git add src/input/bluepad32_platform.c src/main.c
git commit -m "feat: route two Bluetooth pads to two PS2 ports by connection order"
```

---

### Task 6: Wiring + bring-up docs

**Files:**
- Modify: `docs/wiring.md`, `docs/bringup.md`

- [ ] **Step 1: Add the port-1 column to `docs/wiring.md`**

Add a section documenting port 1 (`pio1`, GP10–14) mirroring port 0, and note both ports share GND with the console; the relative-pin PIO requires the consecutive DAT/CMD/SEL/CLK/ACK layout.

```markdown
## Port 1 (second controller)

Same signals as port 0, shifted by 5 GPIOs (the relative-pin PIO requires each
port's DAT/CMD/SEL/CLK/ACK to be consecutive):

| Signal | PS2 pin | Pico GPIO |
|--------|---------|-----------|
| DAT | 1 | GP10 |
| CMD | 2 | GP11 |
| ATT/SEL | 6 | GP12 |
| CLK | 7 | GP13 |
| ACK | 9 | GP14 |

Wire a second console controller port the same way as port 0. Both ports share
GND with the console. core1 owns both `pio0` (port 0) and `pio1` (port 1).
```

- [ ] **Step 2: Add a two-controller section to `docs/bringup.md`**

```markdown
## Two controllers (dual port)

- [ ] Port 0 alone: connect one pad, verify it works on console port 1 (as before).
- [ ] Port 1 alone: connect one pad, verify it works on console port 2 (GP10–14).
- [ ] Both: connect two pads; verify player 1 and player 2 are independent and
      neither drops input while the other is active (the console polls ports
      sequentially — watch for missed ACK / dropped frames on a logic analyzer).
- [ ] Connection order: first pad → port 0, second → port 1; disconnect port 0's
      pad and reconnect — the freed port is reused.
- [ ] Confirm the relative-pin PIO drives both ports correctly (this is the
      highest-risk change; scope CLK/DAT/ACK timing on both ports).
```

- [ ] **Step 3: Commit**

```bash
git add docs/wiring.md docs/bringup.md
git commit -m "docs: dual-port wiring and two-controller bring-up checklist"
```

---

## Self-Review Notes

- **Spec coverage:** two instances (T2 transport, T3 shared_input, T4 ds2/loop); relative-pin single PIO image (T2); launch-once core1 loop polling both ports (T4); connection-order routing via pure host-tested helper (T1, T5); port-1 pins GP10–14 (T2/T4); wiring+bringup (T6). LED-aggregate tweak intentionally deferred (spec) — `bp_controller_connected` is generalized in T5 but the NeoPixel `status_indicator` set-calls stay as-is until that branch lands.
- **Types/signatures consistent:** `ps2_transport_t`, `ps2_device_start/stop(unsigned)`, `shared_input_*(unsigned port, ...)`, `port_router_*` match across tasks.
- **Coupling note:** Tasks 4 and 5 must land together for a green firmware build (Task 4 changes the `ps2_device_start/stop` signature that Task 5's callers use). Build after Task 5.
- **No placeholders:** every code step is concrete. Highest-risk item (relative-pin PIO) is flagged for bench validation in T2 and T6.
