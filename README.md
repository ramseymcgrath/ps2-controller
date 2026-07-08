# ps2-controller

A wireless controller adapter for the **PlayStation 2**: a Raspberry Pi **Pico 2 W (RP2350)**
takes a modern **Bluetooth** controller as input (via [BluePad32](https://github.com/ricardoquesada/bluepad32))
and **emulates a Sony DualShock 2** to a real PS2 console — the MCU is the device/slave on the
console's controller bus.

[![host-tests](https://github.com/ramseymcgrath/ps2-controller/actions/workflows/host-tests.yml/badge.svg)](https://github.com/ramseymcgrath/ps2-controller/actions/workflows/host-tests.yml)
[![firmware](https://github.com/ramseymcgrath/ps2-controller/actions/workflows/firmware.yml/badge.svg)](https://github.com/ramseymcgrath/ps2-controller/actions/workflows/firmware.yml)

## Status

The **complete firmware cross-compiles** to a flashable `pico2_w` `.uf2`, and the
hardware-independent core is host unit-tested. What has **not** happened yet is
**hardware verification** — no Pico, PS2, or logic analyzer has run this. Every
timing-critical behavior (PIO/ACK, the SEL ISR, the console handshake) is written
against the reference but unverified on real hardware. See
[`docs/bringup.md`](docs/bringup.md) for the checklist to close that gap.

| Area | State |
|------|-------|
| DualShock 2 protocol state machine (`src/ps2_device/ds2_protocol.*`) | ✅ host unit-tested |
| Bluetooth gamepad → PS2 input mapping (`src/input/gamepad_map.*`) | ✅ host unit-tested |
| Firmware builds for `pico2_w` (Pico SDK 2.3) | ✅ cross-compiles in CI |
| BluePad32 integration (`src/input/bluepad32_platform.*`) | ✅ compiles; ⬜ pairing unverified |
| PIO SPI-slave transport (`src/ps2_device/ps2_transport.*`, `psxSPI.pio`) | ✅ compiles; ⬜ bench unverified |
| core1 protocol loop + connect/disconnect lifecycle | ✅ compiles; ⬜ on-console unverified |
| MVP scope | analog DualShock (mode `0x73`): sticks + all digital buttons + config handshake |
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

## Building the firmware (`pico2_w`)

Requires the ARM toolchain (`arm-none-eabi-gcc`), CMake, Ninja, and Pico SDK ≥ 2.1
with `PICO_SDK_PATH` set. BluePad32 and its bundled BTstack come in as submodules:

```sh
git submodule update --init --recursive
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
# -> build/ps2_controller.uf2
```

Notes:
- We use **BluePad32's bundled BTstack** (via `PICO_BTSTACK_PATH`), not the SDK's:
  BluePad32 4.2.0 calls the pre-v1.8 `hids_client_*` API that the SDK's newer
  BTstack renamed. Pico SDK ≥ 2.1 honors the override, so no SDK patching.
- The system clock is pinned to **125 MHz** so `psxSPI.pio`'s `SLOW_CLKDIV 50`
  yields the required 2.5 MHz PIO clock (and ~2 µs ACK).
- Flashing and on-hardware verification: see [`docs/bringup.md`](docs/bringup.md).

## Documentation

- Design spec: [`docs/superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md`](docs/superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md)
- Implementation plan (M0–M4): [`docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md`](docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md)
- `0x79` pressure-mode decision + protocol references: [`docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md`](docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md)
- Wiring / pinout: [`docs/wiring.md`](docs/wiring.md)
- Hardware bring-up checklist: [`docs/bringup.md`](docs/bringup.md)

## Credits & references

- [DS4toPS2](https://github.com/TonyMacDonald1995/DS4toPS2) — the fork base (Bluetooth → PS2 on a Pico W)
- [PicoMemcard](https://github.com/dangiu/PicoMemcard) — origin of the `psxSPI.pio` SPI-slave program
- [BluePad32](https://github.com/ricardoquesada/bluepad32) — Bluetooth controller host
- [BlueRetro](https://github.com/darthcloud/BlueRetro) — device-side protocol reference
- Protocol: [psx-spx](https://psx-spx.consoledev.net/controllersandmemorycards/), [curiousinventor](https://store.curiousinventor.com/guides/PS2/)

## License

**GPL-3.0** (inherited from the DS4toPS2 / `psxSPI.pio` lineage). See [`LICENSE`](LICENSE).
BluePad32 is Apache-2.0 (GPL-compatible).
