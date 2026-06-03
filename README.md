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
