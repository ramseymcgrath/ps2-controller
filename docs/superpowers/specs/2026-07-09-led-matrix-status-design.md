# LED Matrix Status Display — Design

**Date:** 2026-07-09
**Status:** Approved (design)
**Target:** RP2350 / Pimoroni Pico Plus 2 W firmware (this repo)
**Sequence:** Revision of the status-LED subsystem — **supersedes the WS2812 NeoPixel**
(feat/neopixel-status-led, PR #1). Frees `pio2` for Subsystem B (memory-card
emulation), which comes next.

## Overview

Replace the single external WS2812 NeoPixel status indicator with an **Adafruit
8×8 bicolor LED matrix** (HT16K33 backpack) driven over **hardware I2C with DMA**.
The HT16K33 performs all row scanning, PWM dimming, and constant-current LED
drive on-chip; the CPU only writes the controller's 16-byte display RAM when the
displayed image changes, and even that write is carried by DMA. The panel shows
**iconographic status** — a searching animation, a green player-count digit, and
a red error glyph — rendered from the public-domain **font8x8** glyph set.

This is a "fully offloaded" indicator by construction: a steady image (idle "1",
idle "2", blank) costs **zero** ongoing CPU because the HT16K33 holds and refreshes
it autonomously. Only one state (searching) animates, and its frames are pushed by
DMA.

## Goals

- Drive the matrix over hardware `i2c0` on the board's STEMMA QT connector
  (GP4 = SDA, GP5 = SCL) at 400 kHz, with DMA frame writes and send-on-change.
- Iconographic status covering **BOOT / SEARCHING / 1-player / 2-player / ERROR**,
  using vendored font8x8 glyphs (digits + a red error glyph).
- Preserve the existing `status_indicator` public API shape (`init` / `set`) so
  the firmware's integration points barely move.
- Keep the current **pure/hardware split** so the glyph→frame logic is
  host-unit-tested exactly like `status_color.c` is today.
- Free **`pio2`** entirely (WS2812 removed) for the memory-card subsystem.
- Resolve the resulting **GP5 pin conflict** by shifting both PS2 ports up one
  GPIO (the STEMMA connector's SCL is GP5, which was PS2 port-0's DAT).

## Non-Goals

- Rainbow / arbitrary-RGB effects — the panel is green/red/yellow only.
- Input-paced activity animation — the NeoPixel's rainbow "bling" is **dropped**
  with this change (a bicolor panel can't rainbow, and the chosen direction is
  iconographic). `status_indicator_note_input` is removed.
- Per-controller *identity* on-panel by default — the panel shows a player
  **count** ("1"/"2"), not which physical port. (Identity is a documented
  variant; see Status Model.)
- Scrolling text / multi-glyph messages.
- Runtime brightness UI — brightness is fixed; `matrix_set_brightness` exists for
  bring-up only.
- Memory-card emulation — Subsystem B, separate spec.

## Hardware

- **MCU:** RP2350B, Pimoroni Pico Plus 2 W, Pico SDK 2.x (2.3.0 here).
- **Display:** Adafruit 8×8 bicolor LED matrix with HT16K33 backpack, connected
  via the board's Qwiic/STEMMA QT connector (JST-SH: SDA/SCL/3V3/GND).
- **Bus:** hardware `i2c0` (the board default — `PICO_DEFAULT_I2C 0`,
  `PICO_DEFAULT_I2C_SDA_PIN 4`, `PICO_DEFAULT_I2C_SCL_PIN 5`), 400 kHz.
  **No PIO.**
- **Address:** HT16K33 default `0x70`.

## Pin Plan (resolves the GP5 conflict)

The STEMMA QT connector's SCL (GP5) collides with PS2 port-0's DAT (GP5-9). Both
PS2 ports shift **up one GPIO**; the relative-pin PIO is base-agnostic and SEL is
derived (`pin_base + 2`), so this is a two-constant edit plus docs.

| Signal group | GPIOs (new) | GPIOs (old) | Notes |
|---|---|---|---|
| Matrix I2C (`i2c0`, STEMMA QT) | GP4 SDA / GP5 SCL | — | board default bus |
| PS2 port 0 (`pio0`) DAT/CMD/SEL/CLK/ACK | **GP6/7/8/9/10** | GP5/6/7/8/9 | `pin_base` 5 → 6 |
| PS2 port 1 (`pio1`) DAT/CMD/SEL/CLK/ACK | **GP11/12/13/14/15** | GP10/11/12/13/14 | `pin_base` 10 → 11 |
| CYW43 (BT/Wi-Fi) | GP23–25, 29 | unchanged | on-package |
| `pio2` | **freed** | WS2812 (GP16) | reclaimed for Subsystem B |

SEL interrupt GPIOs become **GP8** (port 0) and **GP13** (port 1), derived
automatically from `pin_base + 2`; the SEL ISR routes by GPIO number, so no
routing-table edit beyond the two `pin_base` values.

## Architecture — Module Layout

Mirrors the current `status_color` (pure) / `status_indicator` (hardware) split,
adding a driver module because I2C+DMA is more than the WS2812 one-liner.

| File | Role | Tested |
|---|---|---|
| `src/matrix/matrix_glyphs.h` | Glyph bitmaps: digits from font8x8 (Daniel Hepper, public domain) — the subset the display uses (`1`, `2`) — plus a built-in 8×8 error "X". 8 bytes/glyph, row-major, **LSB = leftmost column**. | — |
| `src/matrix/matrix_render.{c,h}` | **Pure.** Owns all frame bit-logic: `matrix_clear`, `matrix_set_pixel` (plane packing + column-bit fixup), `matrix_get_pixel`, and `matrix_render_frame(state, phase) → 16-byte frame`. No hardware includes. | host unit tests |
| `src/matrix/matrix_driver.{c,h}` | Hardware only. `i2c0` + GPIO init, HT16K33 init sequence, DMA channel claim, `matrix_driver_show(frame)`, `matrix_driver_set_brightness`. | bench only |
| `src/status_led/status_indicator.{c,h}` | Public status API (`init`/`set`), reimplemented on the matrix. Owns the render timer that ticks only the one animated state. | — |

**Deleted:** `src/status_led/status_color.{c,h}`, the WS2812 body of
`status_indicator.c`, the `ws2812.pio` `pico_generate_pio_header` in CMake, and
the `test/test_status_color.c` suite (replaced by matrix render tests).

## Status Model & Glyphs

Public state enum expands to encode the player count:

```c
typedef enum {
    STATUS_BOOT = 0,     // pre-BT init
    STATUS_SEARCHING,    // BT up, no player connected  (animated)
    STATUS_CONNECTED_1P, // exactly one port connected
    STATUS_CONNECTED_2P, // both ports connected
    STATUS_ERROR,        // fault
} status_state_t;
```

Glyphs (`.`=off, `G`=green, `R`=red; digits from font8x8, error "X" is a built-in 8×8 bitmap):

```
SEARCHING (0 players)   1 PLAYER = green "1"   2 PLAYERS = green "2"   ERROR = red "X"
 green pixel orbits      . . G G . . . .        . G G G G . . .        R . . . . . R .
 the ring, ~8 Hz         . G G G . . . .        G G . . G G . .        . R . . . R . .
 (the ONLY animated      . . G G . . . .        . . . . G G . .        . . R . R . . .
  state; DMA-carried)    . . G G . . . .        . . G G G . . .        . . . R . . . .
                         . . G G . . . .        . G G . . . . .        . . R . R . . .
                         . . G G . . . .        G G . . G G . .        . R . . . R . .
                         G G G G G G . .        G G G G G G . .        R . . . . . R .
                         . . . . . . . .        . . . . . . . .        . . . . . . . .
```

- **BOOT** → blank panel (all pixels off) until BT init completes.
- **Connection state** is a **static frame written once per transition** — a
  steady "1"/"2"/blank costs zero CPU (the HT16K33 holds it).
- **ERROR** slow-blinks the red "X" (~1–2 Hz) so a fault reads as a fault.

**Resolved design decisions** (approved; recorded here as the binding choices):

1. **Two players → count digit.** `STATUS_CONNECTED_2P` shows a static "2", not
   per-port identity. *Rejected variant:* alternate "1"/"2" every ~1 s to show P1
   vs P2 identity (adds a second animated state) — not chosen.
2. **Searching = slow orbit animation** (~8 Hz, a single green pixel sweeping the
   perimeter). *Rejected variant:* a static "looking" glyph for absolute-zero CPU
   while searching — not chosen; the orbit is the friendly scan cue and DMA makes
   it nearly free.
3. **No activity/bling feed.** `status_indicator_note_input` is removed; the panel
   is pure static icons. *Rejected variant:* a yellow accent pixel flickering on
   input — not chosen.

## HT16K33 Driver

Transcribed from the approved driver spec; unchanged in intent.

**Init (blocking, one-time):** `0x21` (oscillator on), `0x81` (display on, blink
off), `0xE0 | brightness` (brightness 0–15; use `0x0F`).

**Display RAM layout (bicolor):** 16 bytes from pointer `0x00`; each of the 8 rows
uses two bytes — `RAM[row*2+0]` = green column bitmask, `RAM[row*2+1]` = red
column bitmask. Green + red on the same pixel = yellow. The physical panel may
need a per-row column bit rotation/shift (Adafruit's library rotates the column
bits); this fixup lives in `matrix_render` and is validated on hardware.

**Frame write path (DMA, the hot path):**
- Maintain a 17-byte TX buffer `[0x00, d0..d15]`.
- DMA channel: 8-bit transfers, read-increment over the buffer, write to the I2C
  data register, paced by the I2C TX DREQ (`i2c_get_dreq(i2c, true)`).
- The final byte asserts I2C STOP (set the STOP bit in the I2C command framing for
  the last byte). A missing STOP hangs the next transaction — confirm on bench.
- Kick DMA and return; the core is idle for the ~475 µs transfer.
- Before mutating the buffer for the next frame, `dma_channel_wait_for_finish_
  blocking` (or gate on a flag). Send **on change only**; static images need no
  periodic refresh.

**Frame API (pure, in `matrix_render`):** `matrix_clear(uint8_t frame[16])`,
`matrix_set_pixel(uint8_t frame[16], uint x, uint y, uint color)` (color 0=off,
1=green, 2=red, 3=yellow), and `matrix_get_pixel(...)`. These do the plane packing
and the column-bit fixup and are host-tested.

**Driver API (hardware, in `matrix_driver`):**
- `matrix_driver_init(i2c_inst_t *i2c, uint sda, uint scl, uint baud, uint8_t addr)`
- `matrix_driver_show(const uint8_t frame[16])` — DMA the 17-byte `[0x00, …16]`
- `matrix_driver_set_brightness(uint level)`

Control writes (init, brightness) are blocking; the 17-byte frame write uses DMA
(16-bit command words to `IC_DATA_CMD`, `I2C_IC_DATA_CMD_STOP_BITS` set on the last
word). Rationale: DMA on one 17-byte transfer is marginal savings for a static
indicator, but it keeps the core free during the searching animation and scales
cleanly. **Documented fallback** (a compile flag) if the STOP-on-last-DMA-byte
proves troublesome on the bench: a blocking `i2c_write_blocking` of the 17-byte
frame (~475 µs at 400 kHz, ~2% of one 20 ms tick) — behaviorally identical.

## Render Layer (pure, host-tested)

`matrix_render_frame(status_state_t s, uint16_t phase, uint8_t out[16])` composes
a frame: pick the glyph for the state (font8x8 for the digits, a built-in 8×8
bitmap for the error "X"), draw its lit bits into the green (or red, for ERROR)
plane via the same pixel model as `matrix_set_pixel`, and apply the column-bit
fixup. The player count is carried by the state itself (`CONNECTED_1P` vs
`CONNECTED_2P`), so no separate count argument is needed. `phase` (free-running
ms) drives the searching orbit position and the error blink. No hardware
dependencies, so host tests assert the exact 16 output bytes per state — glyph
correctness, plane packing, and the column fixup all covered.

## Integration

- **`status_indicator.{c,h}`** keeps `status_indicator_init(void)` and
  `status_indicator_set(status_state_t)`; **removes**
  `status_indicator_note_input`. `init` sets up `matrix_driver`, seeds a blank
  frame, and starts a 50 Hz repeating timer (SDK alarm pool, core0) that renders
  and shows **only when the frame changed or the state animates** (searching/error).
  Best-effort/idempotent, same as today: on I2C init failure the module disables
  itself and all calls become no-ops.
- **`bluepad32_platform.c`** computes the player count from
  `shared_input_connected(0/1)` and calls `status_indicator_set(SEARCHING /
  CONNECTED_1P / CONNECTED_2P)` on connect/disconnect; the `note_input` call at
  the publish site is deleted. The `_Static_assert` block and routing are
  untouched.
- **`main.c`** keeps `status_indicator_init()` and the `STATUS_ERROR` path on
  cyw43 init failure; comment updated (no more pio2/WS2812).
- **`ps2_device.c`** — the two `ps2_transport_init` calls change `pin_base`
  5 → 6 and 10 → 11.
- **CMake** — add `src/matrix/matrix_render.c` and `src/matrix/matrix_driver.c`
  (and the render test); drop `status_color.c` and the `ws2812.pio` generation;
  add `hardware_i2c` and `hardware_dma` (`hardware_pio` stays — PS2 still uses it);
  add `src/matrix` to includes.

## Testing

- **Host unit tests** (`test/test_matrix_render.c`, replacing
  `test_status_color.c`): for each `(state, players)` assert the exact 16 frame
  bytes for each state — blank BOOT, orbit position at a given `phase`, "1"/"2"
  digit bitmaps in the green plane, "X" in the red plane, the column-bit fixup,
  and yellow packing (a pixel set green+red). Pure, no hardware.
- Existing `ds2_protocol` / `gamepad_map` / `port_router` suites stay green.
- **Bench-only**: HT16K33 init, DMA frame path + STOP assertion, brightness, the
  column orientation, the STEMMA connector pins, and the shifted PS2 ports —
  validated per an expanded `docs/bringup.md`.

## Wiring

- **Matrix:** plug the Adafruit 8×8 bicolor's STEMMA QT into the board's Qwiic/
  STEMMA connector (`i2c0`, GP4/GP5, 3V3, GND) — solder-free. HT16K33 at `0x70`.
- **PS2 ports:** `docs/wiring.md` updated to the shifted GPIOs (port 0 GP6–10,
  port 1 GP11–15). Common GND shared with each console; do not wire the console's
  3.3 V (pin 5) or 7.6 V (pin 3).

## Risks / Hardware Open-Items

1. **Column bit orientation/rotation** for the specific matrix board (the per-row
   fixup in `matrix_render`) — confirm displayed image isn't shifted/mirrored.
2. **STOP assertion on the final DMA byte** with RP2350 hardware I2C — confirm the
   transaction terminates cleanly and the next write isn't stalled. Blocking
   fallback documented above.
3. **STEMMA connector pin routing** — the SDK default I2C (GP4/GP5) is Pimoroni's
   Qwiic bus; confirm against the board pinout at wiring time.
4. **Shifted PS2 pins need re-bench** — the dual-controller ports have never been
   hardware-validated; validate at the new GPIOs (docs/bringup.md).
5. **I2C pull-ups** — the STEMMA QT breakout carries its own SDA/SCL pull-ups; if
   wiring bare, add pull-ups (RP2350 internal pulls are weak for 400 kHz).

## Relationship to Existing Branches

- **feat/neopixel-status-led (PR #1)** — this design **supersedes** it. The WS2812
  module it introduced is deleted here.
- **feat/dual-controller-ports (PR #2)** — this design **edits** it (the PS2
  `pin_base` shift) and depends on its `shared_input_connected` per-port API for
  the player count.
- The **branch/merge strategy** (whether to build on the current stack, retarget,
  or supersede PR #1) is a git-logistics decision resolved at planning/execution
  time, not part of this design.
