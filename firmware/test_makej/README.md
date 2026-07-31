# test_makej

Non-interactive PlatformIO project derived from `../testing_firmware`
(specifically from `t08_motor_move.cpp`). Instead of prompting the operator
for parameters at runtime, it drives all four motor channels (0-3) one after
another using fixed compile-time constants (the same defaults `t08` used as
prompt fallbacks): `STEPS`, `STEP_RATE_HZ`, `CURRENT_MA`. For each channel it
computes the 74HC595 shift-register value that enables just that motor's
driver, configures the TMC2209 over UART (current, microstep resolution),
and runs a step burst.

`include/board_pins.h` and `lib/testkit/` are standalone copies of the ones
in `testing_firmware` - editing one project does not affect the other.

See `doc/heureka.md` for the debugging history behind this project (why it
didn't move at first, and the microstep-resolution bug that was found by
comparing against a known-working reference implementation).

## Building

`pio` is not on `PATH` when PlatformIO is only installed as the VSCode
extension - it lives at `~/.platformio/penv/bin/pio`. The `Makefile` in this
directory already accounts for that.

```bash
make            # same as `make build`
```

Flashing does **not** go through `pio run -t upload` - esptool 4.5.1 bundled
with PlatformIO crashes on this board with `BrokenPipeError`. Flashing is
done separately via `../flash.sh`; see `../FLASH.md` for the full story.

## Makefile targets

| Target | What it does |
|---|---|
| `make build` (default) | `pio run` - compiles the firmware, does not touch the board. |
| `make flash` | Builds, then runs `../flash.sh -d .` - checks flash health and writes bootloader, partitions, and app to the board. |
| `make check` | Runs `../flash.sh -d . --check` - only reads back flash diagnostics (manufacturer/device ID, size, mode), writes nothing. Use this first if the board is behaving oddly. |
| `make monitor` | `pio device monitor` - opens the serial console (native USB, same port used for flashing). |
| `make clean` | `pio run -t clean` - removes `.pio/build/test_makej`. |

## Hardware notes

- Board: ESP32-S3-WROOM-2 N32R16V (32 MB octal flash, octal PSRAM) - the
  memory config in `platformio.ini` (`dout` flash mode, `opi_opi` PSRAM,
  `default_16MB.csv` partitions) is not optional, see the comments there.
- Console is native USB CDC (`Serial`), not the board's Debug UART0 header -
  that was tried and reverted, see `doc/heureka.md`.
- Motor current is set purely over the TMC2209 UART (`IHOLD_IRUN`) - there is
  no VREF potentiometer on this board.
