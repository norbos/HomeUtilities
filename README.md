# AVR C Project for ATmega328P

This repository is a minimal AVR C project scaffold for an ATmega328P-based Arduino board.

## Structure

- `src/` - application source files
- `include/` - project headers
- `Makefile` - build rules for `avr-gcc`

## Build

Use `make` to build the project:

```sh
make
```

## Flashing

Update the serial port and programmer settings in `Makefile` before flashing. Example:

```sh
make flash
```

## Notes

This is intentionally kept minimal so you can write direct register-level code in C.

## Wiring Diagram and Connections

Below is a concise wiring diagram and connection table for the HomeRoofDrain hardware described in the firmware.

1) Power distribution (high-level)

- Solar Panel & Battery -> Solar Charge Controller (wired per controller docs)
- Use the controller's 12V LOAD output to supply the pump's 12V positive through the relay (COM -> NO)
- Controller Load negative -> Pump negative
- Buck converter input -> 12V bus from controller LOAD output
- Buck converter output -> 5V and GND for Arduino, sensors, and relay VCC (common ground required)

2) Arduino (ATmega328P) pin connections

| Component       | Arduino Pin / MCU Signal | Notes |
|-----------------|--------------------------:|-------|
| Pump relay IN   | PB0 (digital D8)         | Drives relay module input (active-high assumed) |
| Flow sensor VCC | 5V                       | Power to sensor |
| Flow sensor GND | GND                      | Common ground |
| Flow sensor OUT | PD2 (INT0, digital D2)   | External interrupt on rising edge (pulse count) |
| Rain sensor VCC | 5V                       | Power to sensor |
| Rain sensor GND | GND                      | Common ground |
| Rain sensor DO  | PD3 (INT1, digital D3)   | External interrupt on falling edge (wet detection) |

3) Important safety notes

- Tie grounds together: buck converter GND, Arduino GND, sensor grounds, and relay module GND must be common.
- Keep the 12V pump power and Arduino 5V control electrically isolated except through the relay contacts.
- Use a properly rated relay or a logic-level MOSFET and add flyback or suppression if the pump is inductive.
- Verify relay wiring on a bench with no solar load before connecting to battery/solar.

4) Flashing the firmware

Update the serial port in the `Makefile` `flash` target if needed, then run:

```
make all
make flash
```

Replace the port in the Makefile (`/dev/ttyACM0`) with your device (e.g. `/dev/ttyUSB0`, `/dev/ttyACM0`).

