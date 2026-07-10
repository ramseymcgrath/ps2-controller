# Wiring — PS2 controller port ↔ Pico 2 W

The Pico 2 W is the **device/slave** on the console's controller bus. Cut a PS2
controller extension cable and connect the plug end (the part that goes into the
console) to the Pico as below.

## GPIO map

| Signal | PS2 pin* | Pico GPIO | Direction (Pico) | Notes |
|--------|----------|-----------|------------------|-------|
| DAT    | 1        | **GP6**   | out (open-drain) | data to console; driven low or Hi-Z, **never high** |
| CMD    | 2        | **GP7**   | in               | command from console |
| 7.6V   | 3        | —         | —                | **do not connect** (controller rumble rail) |
| GND    | 4        | GND       | —                | common ground — required |
| VCC 3.3V | 5      | —         | —                | console supplies 3.3V; power the Pico via USB instead for dev |
| ATT/SEL| 6        | **GP8**   | in               | slave select (active low); rising edge = end of transaction |
| CLK    | 7        | **GP9**   | in               | 250 kHz–500 kHz clock from console |
| ACK    | 9        | **GP10**  | out (open-drain) | acknowledge pulse; low ~2 µs after each byte, **omitted after last** |

> GP4 (SDA) and GP5 (SCL) are reserved for the status-LED matrix on the board's
> STEMMA QT connector (`i2c0`); PS2 port 0 therefore starts at GP6.

\* PS2 controller connector pin numbers, per
[psx-spx](https://psx-spx.consoledev.net/controllersandmemorycards/). Pin 8 is
unused. Confirm against your specific cable with a multimeter — cable colors are
not standardized.

## Open-drain on RP2350

The RP2350 has no open-drain pad mode. DATA and ACK are emulated open-drain in
PIO by toggling **pin direction**: output-low drives logic 0, input (Hi-Z) lets
the console's pull-up float the line to logic 1. The line is never actively
driven high. This is why `psxSPI.pio` writes `pindirs` rather than pin values.

## Power

For development, power the Pico from USB (also gives you the serial console) and
share **GND** with the console. Do **not** wire the console's 3.3V (pin 5) or
7.6V (pin 3) to the Pico while it is USB-powered.

## Clock divider

`psxSPI.pio` runs the state machines at 2.5 MHz (`SLOW_CLKDIV 50`) so the SM
ignores clock activity not meant for the controller port. Do not change this
without re-verifying on real hardware.

## Port 1 (second controller)

Same signals as port 0, shifted by 5 GPIOs (the relative-pin PIO requires each
port's DAT/CMD/SEL/CLK/ACK to be consecutive):

| Signal | PS2 pin | Pico GPIO |
|--------|---------|-----------|
| DAT | 1 | GP11 |
| CMD | 2 | GP12 |
| ATT/SEL | 6 | GP13 |
| CLK | 7 | GP14 |
| ACK | 9 | GP15 |

Wire a second console controller port the same way as port 0. Both ports share
GND with the console. core1 owns both `pio0` (port 0) and `pio1` (port 1).

## Status LED — 8×8 bicolor matrix (I2C)

An Adafruit 8×8 bicolor LED matrix (HT16K33 backpack, addr `0x70`) plugs into the
board's Qwiic/STEMMA QT connector — `i2c0`, **SDA = GP4, SCL = GP5**, 3V3, GND.
Solder-free with a JST-SH cable. The STEMMA breakout carries its own SDA/SCL
pull-ups; if wiring bare, add ~2.2–4.7 kΩ pull-ups for 400 kHz.
