# Dual Controller Ports — Design

**Date:** 2026-07-09
**Status:** Approved (design)
**Target:** RP2350 / Pimoroni Pico Plus 2 W firmware (this repo)
**Sequence:** Subsystem A of a larger effort (A: dual controllers — this doc; B: memory-card emulation — separate spec later).

## Overview

Extend the adapter from one emulated DualShock 2 on one PS2 controller port to
**two independent ports (two players)**, each driven by its own Bluetooth
gamepad. The existing single-port pieces become two instances. core1 remains the
sole owner of all PS2 PIO; core0 stays 100% BluePad32/Bluetooth.

## Goals

- Two PS2 controller ports, each a full DS2 (both sticks, all buttons,
  digital/analog/pressure) — reusing the existing `ds2_protocol`.
- Route two Bluetooth pads to the two ports by **connection order** (first pad →
  Port 0, second → Port 1; a freed slot is reused by the next new pad).
- Preserve the invariant that made the single port reliable: **core1 solely
  owns the PS2 PIO** and core0 never runs PS2-timing-critical work.
- Keep the NeoPixel (on `pio2`) — dual controllers fit without it.
- No regressions to the single-port behavior when only one pad is connected.

## Non-Goals

- Memory-card emulation (`0x81`) — Subsystem B, separate spec.
- MAC-pinned/persistent port assignment (connection-order only for now).
- Manual port swapping UI.
- More than two ports.

## Architecture

Two **port instances**, index 0 and 1. Each owns:

| Piece | Port 0 (existing) | Port 1 (new) |
|-------|-------------------|--------------|
| PIO block | `pio0` | `pio1` |
| DAT / CMD / SEL / CLK / ACK | GP5 / 6 / 7 / 8 / 9 | GP10 / 11 / 12 / 13 / 14 |
| transport (SM pair + SEL hook) | instance 0 | instance 1 |
| `ds2_state` | instance 0 | instance 1 |
| `shared_input` slot | slot 0 | slot 1 |

- **core1** runs a single loop that services **both** ports (round-robin): the
  PIO SMs do all bit-level timing; core1 feeds/drains each port's FIFOs and does
  per-port transaction restart. core1 is the sole owner of `pio0` **and**
  `pio1`.
- **core0 / BluePad32** is unchanged in role: receives controllers, maps input,
  and now publishes to the *correct port's* `shared_input` slot based on the
  device→port assignment.
- `pio2` (NeoPixel) and the CYW43 PIO are untouched.

### PIO budget

`pio0`: CYW43 (1 SM) + port-0 (2 SMs) = 3/4. `pio1`: port-1 (2 SMs) = 2/4.
`pio2`: NeoPixel (1 SM). 6 of 12 SMs; comfortable. (Memory cards later may
change this — handled in Subsystem B, where dropping the NeoPixel PIO is on the
table.)

## Module Changes (singletons → 2 instances)

The current code hard-codes one of everything. Each becomes a small instance
array indexed by port. Public APIs gain a `port` (0/1) parameter.

- **`shared_input.{c,h}`** — today one `static PSXInputState s_state` + seqlock.
  Becomes an array of 2 seqlock slots.
  `shared_input_publish(uint port, const PSXInputState*)`,
  `shared_input_snapshot(uint port)`,
  `shared_input_set_connected(uint port, bool)`, `shared_input_connected(uint port)`.
- **`ps2_transport.{c,h}`** — today hard-codes `pio0` and a single SEL hook.
  Becomes a `ps2_transport_t` instance holding `{PIO pio, uint sm_cmd_reader,
  sm_dat_writer, offsets, uint pin_sel, void(*hook)(void)}`. Two instances are
  initialized (port 0 on `pio0`/GP5–9, port 1 on `pio1`/GP10–14). The SEL ISR
  is one shared GPIO callback that routes by `gpio` number to the right port's
  hook.
- **`ps2_device.{c,h}`** — today one `ds2_state` + one transport + one core1
  thread. Becomes two `ds2_state` instances; `ps2_device_start(uint port)` /
  `ps2_device_stop(uint port)`; the single core1 thread services both.
- **`psxSPI.pio`** — see "PIO program" below.
- **`bluepad32_platform.c`** — device→port routing table (below).
- **`main.c`** — init both transports; launch the one core1 loop.

## Core1 Loop (services both ports)

core1 is launched once at startup (as today) and owns both transports. Each
iteration, for each active port `p`:

1. If port `p` has a pending transaction (its `cmd_reader` RX FIFO is
   non-empty), process it: read the address byte; if `0x01` (controller), stream
   the DS2 response for `p`'s `ds2_state`, capture the request bytes, and apply
   state. `0x81` (memory card) stays Hi-Z (unchanged; Subsystem B territory).
2. Between transactions, if port `p`'s restart flag is set (its SEL rose = end of
   frame), run `ps2_restart_pio(&transport[p])` and clear the flag.

Both ports are independent buses; the console typically polls them sequentially
but may overlap. Round-robin servicing is fast enough (a byte is ~16–32 µs at
250–500 kHz; core1 at 125 MHz has ample cycles), and each port's bit timing is
autonomous in its own SMs. Per-port restart is the only cross-core-sensitive
work and remains core1-only (the SEL ISR merely sets a per-port `volatile` flag,
exactly as today).

### SEL ISR

One `gpio_set_irq_enabled_with_callback` handler for both SEL pins (GP7, GP12).
The callback switches on the `gpio` argument and sets the matching port's restart
flag. It touches no PIO (same rule as today), so no new cross-core hazard.

## PIO Program

`psxSPI.pio` currently waits on **absolute** GPIOs (`wait 0 gpio PIN_SEL`), so a
program image built for port 0's pins can't drive port 1.

**Chosen approach — one relative-pin program image for both ports.** Rewrite the
waits to be **IN-base-relative** (`wait 0 pin N`), exploiting that each port's
DAT/CMD/SEL/CLK/ACK are **consecutive** (port 0: GP5–9, port 1: GP10–14). Each
SM sets its own IN base, so the single loaded program serves either port by
configuration:
- `cmd_reader`: IN base = CMD; SEL = base+1, CLK = base+2 (`in pins 1` reads CMD).
- `dat_writer`: IN base = SEL; CLK = base+1; OUT/SET base = DAT; sideset = ACK.

Rationale: DRY (one image), and it **saves PIO program memory** — which matters
for Subsystem B, where each memory card adds more PIO. This imposes one
requirement, already met: **each port's five signals must be consecutive GPIOs**
(so port 1 = GP10–14 is fixed by this).

Trade-off: it edits the timing-critical bus program, so both ports must be
**bench-validated together** (`docs/bringup.md`). Since no timing is
hardware-proven yet (single-port validation was already deferred), there is no
proven behavior to regress. **Fallback** if the relative rewrite proves
troublesome on the bench: emit a second absolute-pin image for port 1 (more
program memory, but leaves port 0's instructions byte-identical).

## Device → Port Routing (core0)

`bluepad32_platform.c` keeps a 2-entry table mapping a connected
`uni_hid_device_t*` to a port index.

- **`on_device_ready(d)`**: assign the lowest free port; record `d`; call
  `shared_input_set_connected(port, true)` and `ps2_device_start(port)`.
- **`on_device_disconnected(d)`**: find `d`'s port; `shared_input_set_connected(
  port, false)`; `ps2_device_stop(port)`; free the slot.
- **`on_controller_data(d, ctl)`**: find `d`'s port; map and
  `shared_input_publish(port, &st)`.
- A third+ controller that connects while both ports are taken is left
  unassigned (ignored) until a slot frees. Logged.

The assignment helper (find-lowest-free-port / lookup-port-for-device over the
2-entry table) is a **pure function**, extracted so it is host-testable.

## Status LED interaction (deferred integration)

The NeoPixel currently reflects a single connection. With two ports it should
show **SEARCHING** only when *both* ports are empty and **CONNECTED** when *any*
port has a pad; `note_input` is fed from whichever port produced activity. This
is a small change localized to the platform callbacks and is **deferred** until
the NeoPixel branch lands (this subsystem does not depend on it). Tracked as a
follow-up so dual controllers can be built and reviewed independently.

## Testing

- **Host unit tests** on the pure routing helper (`test/test_port_routing.c`):
  first assignment → port 0; second → port 1; disconnect frees the right slot;
  reconnect reuses the lowest free slot; third device while full → unassigned;
  lookup returns the correct port for a device.
- Existing `ds2_protocol` / `gamepad_map` tests are unchanged and must stay green
  (the per-port `ds2_state` reuses the same protocol code).
- The two-instance seqlock, the core1 dual-port loop, both transports, and the
  PIO are **hardware-only**, validated per an expanded `docs/bringup.md`
  (two-controller bring-up: each port independently, then both concurrently).

## Wiring (adds a second console port)

Second PS2 controller port, same mapping as port 0 shifted by 5 GPIOs:

| Signal | Port 1 GPIO | PS2 port pin |
|--------|-------------|--------------|
| DAT | GP10 | 1 |
| CMD | GP11 | 2 |
| SEL | GP12 | 6 |
| CLK | GP13 | 7 |
| ACK | GP14 | 9 |

Common GND shared with the console (and with port 0). Do not wire the console's
3.3 V (pin 5) or 7.6 V (pin 3). `docs/wiring.md` gains the port-1 column.

## Risks

- **Concurrent polling of both ports** — mitigated by autonomous per-port PIO
  timing + fast core1 round-robin; must be confirmed on the bench with two live
  ports.
- **Relative-pin PIO rewrite** — timing-critical; bench-validate both ports.
  Fallback = second absolute-pin image.
- **core1 servicing latency** — with two ports the loop does ~2× work; verify no
  missed ACK deadlines under simultaneous traffic on hardware.
- **Merge with the NeoPixel branch** — small overlaps in `main.c` /
  `bluepad32_platform.c`; the LED-aggregate tweak is the only real integration
  point and is deferred.
