# PS2 DualShock 2 Bluetooth Adapter — Design Spec

- **Date:** 2026-07-07
- **Status:** Approved (design); pending implementation plan
- **Author:** ramseymcgrath (with Claude Code)
- **License:** GPL-3.0 (see Licensing)

## 1. Overview

Build a wireless controller adapter on a **Raspberry Pi Pico 2 W (RP2350)** that takes a
modern **Bluetooth** controller as input and **emulates a Sony DualShock 2** to a real
PlayStation 2 (and PlayStation 1) console. The MCU is the *device/slave* on the console's
controller bus; the console is the SPI master polling it.

This is a "USB/Bluetooth → PS2" adapter (wireless in, PS2-controller-port out).

### Goal (MVP)

One paired Bluetooth controller drives a PS2 game as a **stable analog DualShock (mode
`0x73`)** — d-pad, face buttons, shoulders, Start/Select, L3/R3, and both analog sticks —
with the config-mode handshake so the console recognizes a locked analog pad.

### Non-goals (deferred, post-MVP)

- Pressure-sensitive buttons (DS2 mode `0x79`)
- Rumble feedback (console → controller)
- Multiple controllers / multitap
- Memory-card emulation (address `0x81`)
- On-device pairing UI beyond BluePad32 defaults

## 2. Decisions log

| Decision | Choice | Rationale |
|---|---|---|
| Protocol meaning | PlayStation 2 gamepad (DualShock 2) | User intent |
| Role | USB/BT → PS2 adapter (emulate controller to console) | User intent |
| MCU | RP2350 / Pico 2 W | PIO for timing-critical bus; CYW43439 for wireless; RP2350 headroom |
| PS2 bus impl | PIO (shift + ACK + open-drain) + C protocol on core1 (blocking FIFO) | Proven pattern in reference repos; DMA not needed |
| Bluetooth input | BluePad32 | Broad controller support (DS4/5, Xbox, Switch, 8BitDo) on Pico 2 W |
| DS2 fidelity (MVP) | Analog + digital (mode `0x73`) | Enough to play most games; simplest correct target |
| Licensing / strategy | Fork DS4toPS2 (GPL-3.0), adapt to RP2350 + BluePad32 | Fastest, most proven; least risk |

## 3. Architecture

Dual-core split on the RP2350:

- **Core 0 — Bluetooth.** BluePad32 (atop BTstack + the cyw43 driver) owns the radio,
  scanning, pairing, and the controller database. Its per-report callback maps the
  connected gamepad into the shared `PSXInputState`. Later: sends rumble back to the pad.
- **Core 1 — PS2 slave.** A tight loop using **blocking PIO FIFO** reads/writes drives the
  DualShock protocol state machine, reading the shared `PSXInputState`.
- **PIO.** Two state machines on a PIO block **not** used by cyw43:
  - `cmd_reader` — samples CMD (console→controller) on the clock, autopush every 8 bits.
  - `dat_writer` — drives DATA (controller→console) **open-drain via `out pindirs`** (drive
    low = logic 0; release to Hi-Z = logic 1; never drive high) and emits the **/ACK** pulse
    via `side-set` on the ACK pin's pindir.
  - Both run at a **2.5 MHz** PIO clock (`SLOW_CLKDIV` ≈ 50 from a 125 MHz base) so the SM
    does not react to faster PS2 clocks meant for other peripherals.
- **Shared state.** One packed `PSXInputState` global. Core 0 writes; core 1 reads. MVP:
  no mutex (small struct). If tearing appears, switch to double-buffering.
- **Transaction reset.** RAM-resident GPIO ISR on **SEL/ATT rising edge** restarts both PIO
  SMs (restart, drain TX FIFO / OSR, clear RX FIFO, reset PC) and `multicore_reset_core1()`
  + relaunch. This cleanly recovers from aborted/short transactions (e.g. the console
  polling and cutting the packet short), which the PS2 does frequently.

```
        Bluetooth pad                         PS2 console
             │                                     │
        (BLE/BR-EDR)                        (SPI-like bus: DATA/CMD/CLK/ATT/ACK)
             │                                     │
   ┌─────────▼──────────┐   PSXInputState  ┌───────▼───────────────┐
   │ Core 0: BluePad32  │ ───────────────► │ Core 1: DS2 protocol  │
   │ (BTstack + cyw43)  │  (shared global) │  (blocking PIO FIFO)  │
   └────────────────────┘                  └───────┬───────────────┘
                                                   │ TX/RX FIFO
                                          ┌────────▼────────┐
                                          │ PIO: cmd_reader │
                                          │      dat_writer │  (2.5 MHz, open-drain, ACK)
                                          └─────────────────┘
```

## 4. Components (module boundaries)

Restructured from the DS4toPS2 fork so each unit has one responsibility and a clear
interface.

| File | Responsibility | Depends on |
|---|---|---|
| `src/ps2_device/psxSPI.pio` | Reused PIO program; 5 pins remapped | hardware_pio |
| `src/ps2_device/ps2_transport.{c,h}` | PIO init, FIFO send/recv/ACK helpers, SEL ISR + restart | psxSPI.pio, hardware_pio, pico_multicore |
| `src/ps2_device/ds2_protocol.{c,h}` | DualShock state machine: address gating (`0x01`), command table (`0x42`, `0x43`, `0x44`, `0x45`, needed constants), config-mode (ID `0xF3`). **Pure response-builder** where possible. | input_state.h |
| `src/input/input_state.h` | `PSXInputState` struct — the single interface between BT and PS2 halves | — |
| `src/input/bluepad32_platform.{c,h}` | Our `uni_platform`; `on_controller_data(uni_gamepad_t → PSXInputState)` with the button/axis mapping table | bluepad32, input_state.h |
| `src/main.c` | Clock setup, cyw43/BluePad32 init on core0, launch core1, connect/disconnect lifecycle | all |
| `CMakeLists.txt`, `pico_sdk_import.cmake`, `memmap.ld` | Build for `PICO_BOARD=pico2_w`; BluePad32 component; RAM placement for time-critical funcs | pico-sdk, bluepad32 |
| `LICENSE` | GPL-3.0 (inherited from DS4toPS2 / psxSPI.pio lineage) | — |

**Key interface — `PSXInputState`** (conceptual; final field set in implementation):

```c
typedef struct {
    uint8_t buttons_lo;   // BTNL, active-low: Select,L3,R3,Start, Up,Right,Down,Left
    uint8_t buttons_hi;   // BTNH, active-low: L2,R2,L1,R1, Tri,Circle,Cross,Square
    uint8_t rx, ry;       // right stick,  0x00..0xFF, neutral 0x80
    uint8_t lx, ly;       // left stick,   0x00..0xFF, neutral 0x80
    // deferred: uint8_t pressure[12]; rumble_small/large;
} PSXInputState;
```

## 5. Data flow

1. Controller connects via BluePad32 (core 0).
2. Each BT report → `on_controller_data` maps `uni_gamepad_t` into `PSXInputState`:
   buttons **active-low**, sticks scaled to `0x00..0xFF` (neutral `0x80`). In MVP analog
   mode (`0x73`) there are no pressure bytes, so the analog triggers are **thresholded to
   the digital L2/R2 button bits** (BTNH bit0/bit1); full analog pressure arrives with the
   deferred `0x79` mode.
3. Core 1 PS2 loop: ATT low → PIO clocks bytes. Read address byte; if `0x01` (controller)
   respond, if `0x81` (memory card) stay Hi-Z and ignore.
4. On command byte `0x42` (poll), build the response from the **current** `PSXInputState`:
   `FF 73 5A BTNL BTNH RX RY LX LY` (9 bytes on the bus).
5. Config commands (`0x43` enter/exit config → ID `0xF3`; `0x44 01 03` analog + lock;
   `0x45` status; descriptor constants) are honored so the PS2 recognizes a stable analog
   pad. Mode changes take effect at **packet end** (matching real hardware).
6. **/ACK** is pulsed low (~2–4 µs) after each byte and **omitted after the last byte**.
7. *(Post-MVP)* rumble: console rumble bytes in the `0x42` request → decode → core 0 →
   BluePad32 `play_dual_rumble(...)`.

## 6. PS2 protocol reference (implementation-critical facts)

Sourced from curiousinventor, gamesx/psx-spx, and BlueRetro `main/wired/ps_spi.c`.

- **Bus:** SPI-like, **Mode 3** (CPOL=1/CPHA=1, clock idles high), **LSB-first**, 8-bit,
  full-duplex. Console drives CLK/CMD/ATT; DATA and ACK are **open-drain**, device-driven
  only. Clock ~250 kHz typical, tolerate up to 500 kHz.
- **Address gating:** first byte after ATT low selects device: `0x01` = controller,
  `0x81` = memory card. Reply only if addressed; otherwise keep DATA/ACK Hi-Z.
- **One-byte-ahead pipelining:** the device shifts out byte N's DATA while receiving byte
  N's CMD, so responses are prepared one byte ahead. First DATA out = `0xFF`; ID byte goes
  out during the `0x42` byte; `0x5A` during the next.
- **Analog poll frame (mode `0x73`):**
  - Console CMD: `01 42 00 00 00 00 00 00 00`
  - Device DATA: `FF 73 5A BTNL BTNH RX RY LX LY`
  - ID low nibble = number of 16-bit words following the header (`0x73` → 3 words → 6 bytes
    of buttons+sticks). `0x41` = digital (1 word), `0x79` = DS2 pressure (9 words).
- **Button bytes (active-low, `1` = released):**
  - BTNL bit0..7: Select, L3, R3, Start, Up, Right, Down, Left
  - BTNH bit0..7: L2, R2, L1, R1, Triangle, Circle, Cross, Square
- **Stick axes:** one byte each, neutral `0x80`; order RX, RY, LX, LY.
- **Minimum analog init:** honor `0x43` (enter/exit config), `0x44 01 03` (analog + lock).
  Booting straight up as ID `0x73` and honoring these is sufficient for many games; the
  PS2 also probes descriptor constants (`0x45`/`0x47`/`0x4C`/`0x4F`) — include enough of the
  table (already present in the reference code) that the PS2 does not misidentify the pad.
- **/ACK timing:** pull low ≥2 µs after each byte; release; **omit on the last byte**.
  Console times out if ACK not seen within ~60–100 µs of the last clock.
- **RP2350 specifics:** no open-drain pad mode — emulate via `out pindirs` (drive-low vs
  release-to-Hi-Z). Use ATT-rising as packet-end / index-reset. **Sample-edge quirk differs
  subtly between PS1 and PS2** — make the PIO sample edge easy to tweak and test on both.

## 7. Hardware / wiring

- **Interface:** a cut PS2 controller extension cable exposing DATA, CMD, CLK, ATT, ACK,
  GND. All lines are 3.3 V logic; DATA and ACK are open-drain with the console providing
  pull-ups.
- **Power:** dev via **USB**. For a standalone adapter, tap the controller port's 3.3 V
  logic rail later (port also carries ~7.6 V for rumble motors and GND).
- **Pin mapping:** 5 GPIOs for DAT/CMD/SEL/CLK/ACK, chosen to avoid cyw43-reserved pins on
  the Pico 2 W; documented in `docs/wiring.md` during M2. Reference pinouts: DS4toPS2 used
  DAT5/CMD6/SEL7/CLK8/ACK9; PicoGamepadConverter used DAT19/CMD20/SEL21/CLK22/ACK26.

## 8. Milestones (basis for the implementation plan)

- **M0 — Scaffold & build.** Fork DS4toPS2 into the repo, restructure into the modules
  above, set `PICO_BOARD=pico2_w`, compile clean for RP2350 and flash a no-op `.uf2`.
- **M1 — BluePad32 up.** Replace the BTstack DS4 driver with BluePad32; pair a controller;
  dump button/stick state over USB serial. Confirms input path.
- **M2 — PS2 slave on bench.** Bring up `psxSPI.pio` + core1 protocol at 2.5 MHz; verify
  the `FF 73 5A …` frame + ACK timing with a logic analyzer or a second Pico acting as bus
  master — **before** touching a real console.
- **M3 — Real console.** Plug into a real PS2 (and PS1 for the sample-edge quirk); get
  analog input working in a game. Tune sample edge / clkdiv.
- **M4 — Map & polish.** Full BluePad32 → `PSXInputState` mapping table; config-mode
  robustness; neutral-state-on-disconnect.
- *(Post-MVP)* M5 pressure buttons (`0x79`/`0x4F`), M6 rumble (`0x4D` + `play_dual_rumble`),
  M7 multiple controllers / multitap.

## 9. Error handling / robustness

- **Transaction abort:** SEL-ISR restart of PIO SMs + core1 (proven pattern).
- **Bus sharing:** ignore `0x81` packets (stay Hi-Z) so we never fight a memory card on the
  same port.
- **BT disconnect:** core1 keeps replying with **neutral** state (sticks `0x80`, buttons
  released) so the console sees no dropout; BluePad32 handles reconnect.
- **Timing:** place time-critical functions and the SEL ISR in RAM (`__time_critical_func`
  / custom `memmap.ld`).
- **Clock:** verify the system clock yields a clean 2.5 MHz PIO clkdiv on RP2350
  (references use 240 MHz / 225 MHz sys clock).

## 10. Testing strategy

- **Host unit tests (TDD core):** `ds2_protocol`'s response-builder and config state machine
  are pure functions. Build them natively and test with known vectors from §6, e.g.
  all-released analog = `FF 73 5A FF FF 80 80 80 80`, and config-mode transitions
  (`0x43`/`0x44`) changing the reported ID. This is where automated coverage lives.
- **PIO logic:** verify shift + ACK with the `rp2040-pio-emulator` (NathanY3G) and/or a
  second Pico as bus master.
- **Hardware-in-the-loop:** logic-analyzer capture of a real PS2 poll; then manual in-game
  testing on PS2 and PS1.
- Bluetooth pairing and real-console behavior are inherently manual/hardware tests — which
  is exactly why the protocol core is isolated behind `PSXInputState` to stay
  host-testable.

## 11. Reuse map (fork strategy)

Fork base: **DS4toPS2** (small, single-purpose BT→PS2, our exact topology).

| Piece | Action | Source |
|---|---|---|
| `psxSPI.pio` (shift + ACK + open-drain, 2.5 MHz) | **Reuse near-verbatim** (remap pins) | PicoMemcard lineage (all three repos identical) |
| DualShock protocol state machine | **Adapt** (start from DS4toPS2's `controller_simulator`) | DS4toPS2 (has working rumble for later) |
| SEL-ISR → restart + `multicore_reset_core1` | **Copy pattern** | DS4toPS2 / PicoMemcard |
| Core split (core1 = PS2, core0 = BT) | **Adopt** | DS4toPS2 |
| Bluetooth HID host | **Replace** BTstack DS4 driver with **BluePad32** | new work (our real effort) |
| RP2350 build config | **Add** `pico2_w` target | PicoGamepadConverter proves the combo builds |

## 12. Licensing

All reference code (`psxSPI.pio`, the protocol state machine, transaction-reset idiom) is
**GPL-3.0**. Forking DS4toPS2 makes this project **GPL-3.0** (open source, source
distributed). **BluePad32 is Apache-2.0** (GPL-compatible), so a BluePad32 + GPL DS2
emulator is a valid GPL-3.0 combined work. The protocol byte tables themselves are public
(gamesx/psx-spx); only this specific code carries copyleft.

## 13. References

- DS4toPS2 (fork base): https://github.com/TonyMacDonald1995/DS4toPS2
- PicoGamepadConverter (RP2350 proof, PSX mapping): https://github.com/Loc15/PicoGamepadConverter
- PicoMemcard (psxSPI.pio origin) + PR #7: https://github.com/dangiu/PicoMemcard / https://github.com/dangiu/PicoMemcard/pull/7
- BlueRetro (device-side reference): https://github.com/darthcloud/BlueRetro (`main/wired/ps_spi.c`)
- BluePad32: https://github.com/ricardoquesada/bluepad32 (Pico W docs: `docs/plat_picow.md`)
- Pico SDK PIO+DMA reference: `src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c`
- Protocol: https://store.curiousinventor.com/guides/PS2/ · https://gamesx.com/wiki/doku.php?id=controls:playstation_controller · https://psx-spx.consoledev.net/controllersandmemorycards/
- PIO test tool: https://github.com/NathanY3G/rp2040-pio-emulator
