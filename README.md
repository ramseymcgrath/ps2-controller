# ps2-controller

A wireless controller adapter for the **PlayStation 2**: a Raspberry Pi **Pico 2 W (RP2350)**
takes a modern **Bluetooth** controller as input (via [BluePad32](https://github.com/ricardoquesada/bluepad32))
and **emulates a Sony DualShock 2** to a real PS2 console — the MCU is the device/slave on the
console's controller bus.

[![host-tests](https://github.com/ramseymcgrath/ps2-controller/actions/workflows/host-tests.yml/badge.svg)](https://github.com/ramseymcgrath/ps2-controller/actions/workflows/host-tests.yml)

## Status

Early development. The **hardware-independent protocol core is implemented and unit-tested**;
the firmware and hardware bring-up are planned but not yet built.

| Area | State |
|------|-------|
| DualShock 2 protocol state machine (`src/ps2_device/ds2_protocol.*`) | ✅ implemented, host unit-tested |
| Bluetooth gamepad → PS2 input mapping (`src/input/gamepad_map.*`) | ✅ implemented, host unit-tested |
| MVP scope | analog DualShock (mode `0x73`): sticks + all digital buttons + config handshake |
| Firmware skeleton / Pico SDK build | ⛔ not yet (needs Pico SDK) |
| PIO SPI-slave transport, BluePad32 integration, real-console bring-up | ⛔ planned |
| Pressure buttons (`0x79`), rumble, multitap | ⛔ deferred — see the decision doc |

The emulator is deliberately **clamped to analog mode** and never advertises pressure mode
(`0x79`); see [`docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md`](docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md)
for the reasoning and primary-source protocol references.

## Building & testing the protocol core (no hardware needed)

The protocol logic is pure C and compiles/tests natively — this is what CI runs:

```sh
cmake -S test -B build-test
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

Requires a host C compiler and CMake ≥ 3.13. [Unity](https://github.com/ThrowTheSwitch/Unity)
is vendored under `test/unity/`.

## Documentation

- Design spec: [`docs/superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md`](docs/superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md)
- Implementation plan (M0–M4): [`docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md`](docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md)
- `0x79` pressure-mode decision + protocol references: [`docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md`](docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md)

## Credits & references

- [DS4toPS2](https://github.com/TonyMacDonald1995/DS4toPS2) — the fork base (Bluetooth → PS2 on a Pico W)
- [PicoMemcard](https://github.com/dangiu/PicoMemcard) — origin of the `psxSPI.pio` SPI-slave program
- [BluePad32](https://github.com/ricardoquesada/bluepad32) — Bluetooth controller host
- [BlueRetro](https://github.com/darthcloud/BlueRetro) — device-side protocol reference
- Protocol: [psx-spx](https://psx-spx.consoledev.net/controllersandmemorycards/), [curiousinventor](https://store.curiousinventor.com/guides/PS2/)

## License

**GPL-3.0** (inherited from the DS4toPS2 / `psxSPI.pio` lineage). See [`LICENSE`](LICENSE).
BluePad32 is Apache-2.0 (GPL-compatible).
