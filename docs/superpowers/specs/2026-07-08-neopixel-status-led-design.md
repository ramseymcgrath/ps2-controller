# NeoPixel Status LED — Design

**Date:** 2026-07-08
**Status:** Approved (design)
**Target:** RP2350 / **Pimoroni Pico Plus 2 W** firmware (this repo)

## Overview

Add a single external WS2812 ("NeoPixel") as a status indicator. It shows the
adapter's connection/fault state and, while a gamepad is connected, an
**input-paced rainbow** whose hue advances with how much the player is moving.

WS2812 mechanics reuse the **Pico SDK's own WS2812 PIO code** — no custom PIO
program, bit timing, or color packing. The only authored logic is the pure
state→color and activity functions. The pixel is driven from core0 on a
dedicated `pio2` state machine, fully isolated from the time-critical PS2 bus on
core1.

The board has **no onboard NeoPixel** (its only onboard LED is the single-color
one on the CYW43 chip), so this is a single external pixel. `pio2` keeps 3 SMs
free if a second indicator is ever added.

## Board Configuration

Set `PICO_BOARD=pimoroni_pico_plus2_w_rp2350` (currently `pico2_w`). Beyond
correctness of intent, this fixes an under-provisioned build: the Plus 2 W has
**16 MB flash**, **8 MB PSRAM**, and the **RP2350B** package, none of which the
`pico2_w` board type reflects. The board header defines no `PICO_DEFAULT_LED_PIN`
or `PICO_DEFAULT_WS2812_PIN` (LED is on the CYW43 chip), which is why the status
pixel is external with an explicit pin.

## Goals

- One external WS2812 showing: boot, searching, connected (with input-paced
  rainbow), and error.
- **Reuse SDK code; hand-roll nothing** for WS2812: the SDK's `ws2812.pio`
  (`ws2812_program`, `ws2812_program_init`), SDK PIO claim/add functions, and
  the SDK repeating timer. Authored code = pure color/activity logic only.
- Never perturb PS2 protocol timing or the "core1 is the sole owner of `pio0`"
  invariant; add no new cross-core signals.
- Best-effort: if the pixel can't be initialized, the adapter still works.
- Keep all color/activity logic host-testable as pure functions.

## Non-Goals

- Multi-pixel strips, an onboard/second indicator, or per-button lighting.
- Battery / charge indication.
- Runtime-configurable colors (palette is a compile-time constant).
- Rumble-reactive lighting (would need a core1→core0 rumble signal; deferred).
- Reflecting console poll state (SEL activity) on the LED — the rainbow is
  driven by controller input, so the SEL path is left untouched.

## Hardware

- One WS2812 / WS2812B / SK6812 pixel, single-wire on `STATUS_LED_PIN`
  (default `GP16`; free — PS2 owns GP5–GP9, CYW43 owns GP23–GP25/GP29; not ADC).
- A 5 V pixel needs a data-line level shifter (board concern, out of scope).
- Can be disabled at compile time (`STATUS_LED_ENABLED`), in which case the
  module compiles to no-ops.

## SDK Reuse — WS2812 mechanics

- `pico_generate_pio_header()` on
  `${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio`, giving
  `ws2812_program`, `ws2812_program_get_default_config`, and
  `ws2812_program_init(pio, sm, offset, pin, freq, rgbw)`.
- `ws2812_program_init` derives its clock divider from `clk_sys` (125 MHz),
  independent of the PS2 bus's fixed `SLOW_CLKDIV`. No clock conflict.
- Color words use the standard SDK-example convention: an `0xRRGGBB` value is
  packed as `((g << 16) | (r << 8) | b) << 8u` and pushed to the TX FIFO.

### Why not the high-level `pico_status_led` library

The SDK also ships a turnkey `colored_status_led_*` API. We deliberately do
**not** use it. First, it only turns a pixel on/off with a fixed color — it has
no animation, so it cannot produce the rainbow. Second, its `status_led_init()`
claims the SM with `pio_claim_free_sm_and_add_program_for_gpio_range()`, which
auto-selects the first PIO with a free SM that can reach the pin. `pio0` has 2
free SMs, so the WS2812 can land on `pio0`. Our `ps2_restart_pio()` (core1)
mutates `pio0` via `pio_set_sm_mask_enabled()`, a **non-atomic
read-modify-write on the shared per-PIO `CTRL` register**
(`pio->ctrl = (pio->ctrl & ~mask) | ...`). A WS2812 SM on `pio0` toggled from
core0 would race that RMW and clobber enable bits — the same cross-core class
already fixed in this firmware. So we call the SDK's lower-level primitives and
place the SM on `pio2` ourselves.

## PIO Placement

- The WS2812 runs on **`pio2`**, driven by **core0**:
  `off = pio_add_program(pio2, &ws2812_program)`, `sm =
  pio_claim_unused_sm(pio2, false)`, `ws2812_program_init(pio2, sm, off,
  STATUS_LED_PIN, 800000, false)`.
- Uses 1 program slot + 1 of `pio2`'s 4 SMs.
- Ownership stays clean: `pio0` = PS2 bus (core1 exclusive), CYW43 = whichever
  PIO its driver auto-claims, `pio2` = status LED (core0).
- **Init ordering:** initialize **early** — right after `set_sys_clock` /
  `stdio_init_all`, *before* `cyw43_arch_init()`. The LED needs only the system
  clock and `pio2` (both available then), and the render timer runs on the SDK
  alarm pool independent of the BT run loop. Initializing first lets a
  cyw43/BT init failure show a red error blink. `pio2` stays free for us because
  cyw43's auto-claim takes `pio0` first; we take `pio2` explicitly.

## Module Layout — `src/status_led/`

- `status_indicator.h` / `status_indicator.c` — public API, the render timer,
  the WS2812 claim/init (via the SDK's `ws2812_program_init`), and the input
  activity accumulator.
- Pure, host-tested helpers (no hardware deps):
  - `uint32_t input_activity(const PSXInputState *cur, const PSXInputState *prev)`
    — stick deflection + newly-pressed-button energy.
  - `uint32_t hue_to_rgb(uint8_t hue)` — HSV wheel at full sat/val, scaled by the
    brightness cap; returns `0xRRGGBB`.
  - `uint32_t status_color(status_state_t s, uint16_t phase, uint8_t hue)`
    — the full palette (see below).
- No `.pio` file of our own — the WS2812 header is generated from the SDK's.

Named `status_indicator_*` to avoid colliding with the SDK's `status_led_*`
symbols.

## Public API

```c
#include "input/input_state.h"   // PSXInputState

typedef enum {
    STATUS_BOOT = 0,       // pre-BT init
    STATUS_SEARCHING,      // BT up, no gamepad
    STATUS_CONNECTED,      // gamepad paired
    STATUS_ERROR,          // fault
} status_state_t;

void status_indicator_init(void);                 // claim SM on pio2, start render timer
void status_indicator_set(status_state_t s);      // core0: connection/fault state
void status_indicator_note_input(const PSXInputState *s); // core0 input publish: activity
```

## State Model

Writers on **core0** (BluePad32 callbacks, fault sites); the render timer also
runs on core0, so plain `volatile` words suffice — no seqlock:

- `volatile status_state_t g_state`
- `volatile uint32_t g_pending_activity` — accumulated by `note_input`,
  consumed by the render tick
- `g_hue` (uint8_t) — rainbow phase; written and read **only** by the render
  tick, so it needs no `volatile` and crosses no context boundary

### Palette — `status_color(state, phase, hue)`

| state | treatment |
|---|---|
| `BOOT` | off |
| `SEARCHING` | amber breathing (phase-modulated brightness) |
| `CONNECTED` | input-paced rainbow `hue_to_rgb(hue)` (parks when still) |
| `ERROR` | red blink (~2 Hz, from `phase`) |

Brightness cap (`STATUS_MAX_BRIGHTNESS`, ~15–20%) applied inside `status_color`
and `hue_to_rgb`; no returned channel exceeds it.

## Input-Paced Rainbow

- `input_activity(cur, prev)` = sum of stick deflections
  `|lx-0x80|+|ly-0x80|+|rx-0x80|+|ry-0x80|` (0–512) plus
  `newly_pressed_count * STATUS_BTN_WEIGHT`, where newly-pressed = bits that went
  1→0 (active-low) in `buttons1`/`buttons2` from `prev` to `cur`
  (`prev & ~cur`).
- `status_indicator_note_input()` runs on each BluePad32 publish (core0);
  computes `input_activity` against the module's previous snapshot and does
  `g_pending_activity += activity`. Capturing at publish time (not sampling in
  the timer) means fast taps between render ticks are not missed.
- The 50 Hz render tick advances `g_hue += (STATUS_RAINBOW_GAIN *
  g_pending_activity) >> SHIFT` (only while `CONNECTED`), then clears
  `g_pending_activity`. Neutral sticks + no presses → activity 0 → hue parks.
- `g_pending_activity` is touched by a core0 thread (`note_input`) and the core0
  timer IRQ (render). A dropped/duplicated increment is cosmetically invisible;
  the render tick's read-add-clear briefly masks its own re-entry, sufficient on
  one core.

## Data Flow

1. BluePad32 connect/disconnect → `status_indicator_set(CONNECTED | SEARCHING)`.
2. BluePad32 input publish → `status_indicator_note_input(&s)` (activity).
3. 50 Hz `repeating_timer` (SDK `add_repeating_timer_ms`):
   - advance `g_hue` from `g_pending_activity` (if `CONNECTED`);
   - compute `phase` from a millisecond clock;
   - `color = status_color(g_state, phase, g_hue)`;
   - pack to WS2812 wire order and push one word (3 non-blocking FIFO writes).

Nothing blocks, touches `pio0`, or crosses to core1.

## Animation / Driver

SDK `add_repeating_timer_ms` at ~50 Hz (20 ms) on core0. Chosen over polling the
BluePad32/btstack loop (cadence not guaranteed) and the high-level library's
async_context path (avoided for the PIO reason above). WS2812 timing is handled
by the SDK PIO program; the callback only writes a color word, safe in
timer-IRQ context.

## Power / Brightness

Capped at ~15–20% (`STATUS_MAX_BRIGHTNESS`). Bounds current (a WS2812 at full
white is ~60 mA) and glare.

## Error Handling

Best-effort. If `pio_claim_unused_sm` (non-panicking) or the program add fails,
the module flags itself disabled and all calls become no-ops; the adapter runs
normally. Timer-start failure is treated the same way. Never fatal.

## Testing

Host unit tests (`test/test_status_indicator.c`) on the pure functions:

- `input_activity`: neutral+no-buttons → 0; full deflection → near max; monotonic
  in deflection; a fresh press adds energy, a held button does not re-add
  (edge, not level).
- `hue_to_rgb`: primary hues land on expected channels; continuous around the
  wheel; no channel exceeds `STATUS_MAX_BRIGHTNESS`.
- `status_color`: each state → expected hue/treatment; `SEARCHING` brightness
  monotonic over pulse phase; brightness cap respected for all states/phases.

PIO init, the timer, and real WS2812 output stay hardware-only (bring-up
checklist, `docs/bringup.md`).

## Integration Points (files touched)

- `src/status_led/*` — new module (`status_indicator.c/.h`).
- `src/main.c` — `status_indicator_init()` right after `stdio_init_all()`
  (before `cyw43_arch_init()`); initial state `STATUS_BOOT`. On cyw43-init
  failure, `status_indicator_set(STATUS_ERROR)` and spin so the red blink shows.
- `src/input/bluepad32_platform.c` — `status_indicator_set(STATUS_SEARCHING)` in
  `on_init_complete`; `STATUS_CONNECTED` in `on_device_ready`;
  `STATUS_SEARCHING` in `on_device_disconnected`;
  `status_indicator_note_input(&st)` after `shared_input_publish` in
  `on_controller_data`.
- Fault sites (e.g. BT init failure) — `status_indicator_set(STATUS_ERROR)`.
- `CMakeLists.txt` — set `PICO_BOARD pimoroni_pico_plus2_w_rp2350`; add
  `status_indicator.c`; `pico_generate_pio_header(<target>
  ${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio)`; link
  `hardware_pio`, `hardware_timer`, `hardware_clocks`. Do **not** link
  `pico_status_led`.
- `test/CMakeLists.txt` — add `test_status_indicator`.

## Risks

- **SM/PIO contention with CYW43** — none: cyw43's auto-claim takes `pio0`
  first, and we take `pio2` explicitly; `pio2` is otherwise untouched.
- **Timer-IRQ contention with btstack** — WS2812 push is a few FIFO writes;
  negligible. Fallback: render in the core0 main loop.
- **`PICO_BOARD` change affects the whole build** — flash/PSRAM/package differ
  from `pico2_w`; verify the firmware still links and the `.uf2` boots (bring-up
  checklist) after the switch.
