# Hardware bring-up checklist

The firmware cross-compiles and the pure-C core is host-tested, but **none of
the hardware-timed behavior has been verified on real hardware** — there was no
Pico, PS2, or logic analyzer available when it was written. This checklist is
the deferred half of plan tasks 11–15: run it when you have the hardware.

Legend: ⬜ untested · ✅ verified

## 0. Equipment

- Pico 2 W (RP2350) and USB cable
- A BluePad32-supported **Bluetooth Classic** controller (DualShock 4 is the
  reference; DS3, 8BitDo in classic mode, many others also work). Note: BLE-only
  controllers depend on the BLE HID path — see §6.
- A real PS2 (ideally also a PS1), a cut PS2 controller extension cable
- A logic analyzer **or** a second Pico as a bus master

## 1. Build & flash

```sh
export PICO_SDK_PATH=$HOME/pico-sdk
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```
⬜ Hold BOOTSEL, plug in USB, copy `build/ps2_controller.uf2` to the `RP2350`
drive. Open USB serial (`screen /dev/tty.usbmodem* 115200`).

## 2. Bluetooth input (Task 11)

⬜ Pair the controller. Serial should log `device connected` then `device ready`.
⬜ Confirm the mapping by instrumenting `on_controller_data` (temporary
`printf` of the mapped `PSXInputState`): pressing Cross clears the Cross bit in
`buttons2`, the left stick drives `lx` toward `0x00`/`0xFF`, etc. The mapping is
host-tested, so this is really a check that the BluePad32 field semantics
(ranges, misc-button bits) match our assumptions.

## 3. PIO transport on the bench (Task 12)

Drive `ATT/CLK/CMD` from the second Pico / analyzer, sending `01 42 …`.
⬜ The cmd_reader SM samples CMD on the rising clock; dat_writer drives DATA
open-drain.
⬜ **ACK pulse ~2 µs low after each byte, omitted after the last.** This depends
on `clk_sys = 125 MHz` (so `SLOW_CLKDIV 50` → 2.5 MHz SM clock, and the
dat_writer's `[5]` side-set delays → ~2 µs). If the ACK width is wrong, check
the system clock first.
⬜ **cyw43/PIO coexistence:** we claim `pio0` SMs *after* `cyw43_arch_init()`.
Confirm there is no PIO/SM collision on the Pico 2 W (cyw43 uses its own PIO).

## 4. Full poll frame on the bench (Task 13)

Send `01 42 00 00 00 00 00 00 00` with a neutral shared state.
⬜ Expect DATA `FF 73 5A FF FF 80 80 80 80`, ACK after each byte except the last.
⬜ **ID/pipeline ordering:** verify the ID byte (`0x73`) is shifted out during
the *command-byte* exchange, i.e. `ps2_send(ds2_id_byte())` then `ps2_recv_cmd()`
lands the ID opposite `0x42`. If the fork's transport already emits the ID,
adjust so we don't double-send it.
⬜ Config handshake: send `01 43 00 01…` (enter), `01 44 01 03…` (analog+lock),
`01 43 00 00…` (exit); confirm the ID flips to `0x73` and stays.

## 5. core1 PIO ownership & restart timing (Task 14) — highest-risk item

**core1 is the sole owner of the PIO** after init: the non-blocking helpers
(`ps2_try_recv_cmd`/`ps2_try_send`) and `ps2_restart_pio()` all run on core1.
The SEL-rising ISR (core0) does **nothing** but set `volatile bool s_restart` —
it touches no PIO state — so there is no cross-core FIFO race. core1's loop
processes a transaction, waits for the SEL rise (`s_restart`), then calls
`ps2_restart_pio()` itself during the inter-transaction gap. core1 is launched
**once** per connection (never reset/relaunched per transaction).

The one thing to verify on hardware is **restart timing**: core1 must call
`ps2_restart_pio()` in the gap between one transaction's SEL-rise and the next
transaction's first clock. core1's reaction is a few instructions (~tens of ns)
and the PS2 inter-poll gap is ~100 µs+, so the margin is large — but confirm it:
⬜ On the analyzer, over many consecutive polls, every transaction is parsed from
byte 0 (address `0x01`), the response is byte-aligned, and ACK lands correctly —
no cumulative drift.
⬜ Confirm no stale byte from an aborted/short transaction leaks into the next
frame (the per-transaction `ps2_restart_pio()` clears RX and drains TX, but only
core1 does this, so verify the ordering holds under real timing).
⬜ Stress the abort path: a game/console that cuts a transaction short (fewer
bytes than the full frame) must still resync cleanly on the next poll.

## 6. Real console (Task 14) & polish (Task 15)

⬜ Wire the cut cable per [`wiring.md`](wiring.md), USB-power the Pico, plug into
PS2 port 1. Console shows a controller; digital buttons and both analog sticks
work in a game; the pad is recognized as **analog** (analog LED / a game that
shows the analog indicator).
⬜ **Offset-dependent descriptors:** `ds2_response` builds each frame in one shot,
so commands `0x46`/`0x4C` currently serve their *offset-0* form regardless of the
offset byte the console sends. If a game misidentifies the pad, capture the real
`0x46`/`0x4C` probes and either thread the captured offset byte back into a second
`ds2_response` pass or special-case those two commands in `ps2_device_thread`.
⬜ **PS1 edge check:** plug into a PS1 (or a PS1 title on the PS2). If input is
erratic, adjust the `dat_writer`/`cmd_reader` clock edge (`wait 1` vs `wait 0` on
`PIN_CLK`) per the PS1-vs-PS2 sampling quirk; document the working setting here.
⬜ **BLE controllers:** we build BluePad32's BLE HID path (bundled btstack). If a
BLE-only controller fails to connect, verify against a Classic controller first
to isolate BLE from the rest of the stack.

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
