# esp32stepper — hardware bring-up test suite

Test suite for the custom 4-channel stepper board (ESP32-S3-WROOM-2 + 4× TMC2209
SilentStepStick). **This is not application firmware** — it is a ladder of tests that prove the
board works, one subsystem at a time.

Czech version of this document: [README_CZ_.md](README_CZ.md)

---

## ⚠️ Status: only the first two tests are validated

| Tests | State |
|---|---|
| **T00 `chip_info`, T01 `blink_io38`** | run on real hardware, debugged, behave as documented |
| **T02 … T08** | compile, never executed on hardware — PASS/FAIL logic is unverified |

**Do not run T02 and above on a fresh board.** They drive the shift register, the STEP pins, the
TMC UART and (T08) an actual motor; on an untested board an unvalidated test can just as easily be
wrong about the board as the board is about the test, and T08 moves mechanics. Bring a new board up
with T00 and T01 only, and treat everything above as code that still needs review before its first
run.

The two validated tests were run on an **ESP32-S3-DevKitC-1 N8R2**, not on the target board.

---

## Requirements

- [PlatformIO](https://platformio.org/) CLI (`pio`) — the `espressif32` platform is fetched on
  first build
- USB-C cable to the board's **USB1** connector (native USB CDC on IO19/IO20)
- 24 V on the barrel jack only for T06 (optional) and T08 (required)

All commands below run from this directory (`firmware/platformio/`).

## Quick start

```bash
pio run -e menu -t upload && pio device monitor
```

That flashes a binary containing every test plus an interactive menu, then opens the console. This
is the usual flow — you flash once and pick tests over USB, without reprogramming between them.

### The console starts blank — that is normal

The board has **no USB-UART bridge**; the console is native USB CDC. Anything printed before your
terminal opens the port is lost forever, so a freshly attached monitor can show an empty screen on
a perfectly healthy board.

**Press Enter.** The menu re-prints its prompt every 2 s until the first byte ever arrives, so
within two seconds you will see either the prompt or the whole menu.

## Using the menu

```
test> 0        run a single test by number
test> a        run 00–07 in sequence (skips T08, no motor movement)
test> ?        print the menu again
```

In sequence mode a failing test stops and asks whether to continue. Individual tests are
interactive: they ask for parameters (`perioda [ms]`, `frekvence [Hz]`, …, Enter accepts the
default in brackets) and ask you to confirm what you observed. Results are printed as
`[ OK ]` / `[FAIL]` / `[WARN]` and summed up at the end of each test.

Tests that loop can be stopped by pressing any key.

## Single-test binaries

Every test also has its own environment, containing nothing but that test and `main.cpp`:

```bash
pio run -e t00_chip_info -t upload && pio device monitor
```

Use these when the board hangs, browns out or resets during a test — the binary then contains
nothing else that could be to blame. The single-test build runs the test immediately after boot and
repeats it on any keypress.

## Build without flashing

`menu` is `default_envs`, so a bare `pio run` builds only that. To check that everything still
compiles, list the envs explicitly:

```bash
pio run -e menu -e t00_chip_info -e t01_blink_io38 -e t02_shift_register -e t03_step_pins \
        -e t04_endswitches -e t05_i2c_scan -e t06_tmc_uart -e t07_wiring_selftest \
        -e t08_motor_move
```

Compiling every env is the only automated gate — there is no host-side test suite and no CI.

```bash
pio run -t clean     # drop build artifacts
pio device list      # find the serial port if auto-detection fails
```

## The test ladder

Each test assumes the ones before it passed. `src/test_registry.cpp` is the authoritative list.

| # | Test | Proves | Needs | Validated |
|---|---|---|---|---|
| 00 | `chip_info` | chip boots, USB CDC console works, flash size / PSRAM / reset reason | just USB | ✅ |
| 01 | `blink_io38` | a GPIO output really toggles a pin | external LED + 330 R or a meter on J4 | ✅ |
| 02 | `shift_register` | 74HC595 drives DIR and EN | meter/probe on the BOB pads | ❌ |
| 03 | `step_pins` | STEP pulses on IO4…IO7 | scope or logic analyzer | ❌ |
| 04 | `endswitches` | IO13…IO16 read both H and L | end switches, a hand | ❌ |
| 05 | `i2c_scan` | TCA9548A at 0x70, per-channel encoder scan | nothing / encoders | ❌ |
| 06 | `tmc_uart` | all four drivers answer, `VERSION = 0x21`, MS1/MS2 addressing | populated BOB-0…3 | ❌ |
| 07 | `wiring_selftest` | DIR/EN/STEP wiring, **verified automatically** | populated BOB-0…3 | ❌ |
| 08 | `motor_move` | a motor actually turns | 24 V + motor, free mechanics | ❌ |

**T07 is the one worth understanding.** The TMC2209's `IOIN` register reads back the levels of its
own DIR / ENN / STEP input pins, so the firmware can set a level through the shift register or a
GPIO and read it back over the UART. That closes the loop on the whole signal path — shift-register
bit order, cold joints, swapped STEP pins, wrong driver address — with no scope and with the motors
stationary. Prefer extending T07 over adding another probe-and-eyeball test.

## Safety

- `main.cpp` calls `tk::safe_state()` before and after every test: STEP pins low, all drivers
  disabled (`SR = 0xAA`).
- **EN is active-low** (the SilentStepStick's `ENN`); `0` enables a driver. The 74HC595's `~OE` is
  hard-tied to GND, so it drives its outputs the instant the ESP32 boots — a known-safe byte is
  pushed out first.
- T02 briefly enables each driver in turn (holding torque, current draw) and asks before doing so.
  T03 and T07 keep all drivers disabled, so nothing moves even with 24 V connected.
- **Only T08 moves a motor.** It asks for the motor, step count, rate and `IRUN` current, and
  refuses to start until you confirm the mechanics are clear. Sequence mode (`a`) skips it.
- There is **no software-controllable LED** on the board (LED1 hangs off the LT8610's PG pin via an
  NPN). IO38 is brought out to the single-pin header J4 only.

## Pin map

`include/board_pins.h` is the **only correct pin map** — read off the netlist and re-verified
pad-by-pad against `PCB/esp32stepper.kicad_pcb`.

| Signal | GPIO |
|---|---|
| STEP0…3 | 4, 5, 6, 7 (direct GPIO → BOB-0…3) |
| 74HC595 SRCLK / SER / RCLK | 8 / 9 / 10 |
| I2C SCL / SDA | 11 / 12 |
| End switch 0…3 | 13, 14, 15, 16 |
| TMC2209 UART TX / RX | 17 / 18 |
| Debug UART TX / RX (header J5) | 2 / 1 |
| Spare (header J4) | 38 |

Two other sources in this repo disagree and are **wrong for this board** — never copy pin numbers
from them: the table in `firmware/README.md`, and all of `firmware/*.py` (legacy MicroPython
scripts written from a guess at the schematic, before the board existed).

The DIR/EN bit order in the shift register is **interleaved, not blocked**: writing MSB-first,
`bit0→QA=dir0, bit1→QB=en0, bit2→QC=dir1, bit3→QD=en1, …`. Use `SR_BIT_DIR[]` / `SR_BIT_EN[]`,
never hand-rolled shifts.

## Troubleshooting

**Console stays blank.** Expected — press Enter (see above). `HWCDC::operator bool()` only reports
whether the USB *cable* is plugged in, so the firmware cannot detect your terminal attaching.

**Boot loops with `E cpu_start: Octal Flash option selected, but EFUSE not configured!`**
Someone set `flash_mode = opi` / `memory_type = opi_opi` on a module with quad flash. Revert to the
committed quad settings. `platformio.ini` deliberately configures the quad common denominator
(`dio`, `qio_qspi`, 8 MB, `default_8MB.csv`) because the BOM does not state the module's memory
variant; this boots on every S3 variant, including a module that does have octal memories. T00
reports the flash size actually found.

**T00 warns that PSRAM was not found.** A `warn`, not a `fail` — no test in the ladder needs PSRAM.
It means the module is not an R* variant, or it has octal PSRAM (which the quad config leaves
unused).

**T00 reports a brownout reset.** Power supply is inadequate or sagging — fix that before running
anything that enables a driver.

**Upload cannot find the port.** `pio device list` to see what is there. The board has a `BOOT1`
tactile switch but no reset button; hold `BOOT1` while plugging USB in to force download mode.

**UART0 (IO43/IO44) is physically unconnected.** Never route logging there — it goes nowhere on
this board.

## Adding a test

Three places, all required:

1. `src/tests/tNN_name.cpp` defining `void tNN_name()` — no arguments, returns when done.
2. A row in `src/test_registry.cpp` (name, what it proves, what must be connected).
3. A declaration in `src/test_registry.h` and an `[env:tNN_name]` section in `platformio.ini`.

Conventions that keep the suite usable as a bring-up tool:

- Start with `tk::reset_results()` and `tk::banner()`, end with `tk::summary()`.
- Report through `tk::pass/fail/warn/info` — `pass`/`fail` feed the counters sequence mode uses to
  decide whether to stop.
- Leave the board in a safe state on every exit path; a test that enables a driver must disable it
  itself.
- A loop must poll `tk::key_pressed()` so the operator can get out.
- A blocking wait must use `main.cpp`'s `wait_for_input()` pattern, not a bare
  `while (!Serial.available())`.
- Shared hardware helpers (`ShiftReg`, `TmcBus`, console) belong in `lib/testkit/`, not in tests.
- Comments and console output are Czech and ASCII-only (no diacritics) — some terminals mangle
  UTF-8 over USB CDC.

## Layout

```
platformio.ini          envs: menu (default) + one per test
include/board_pins.h    authoritative pin map, shift-register bit map
lib/testkit/            console, result counters, ShiftReg, TmcBus, safe_state
src/main.cpp            interactive menu, or single-test entry via -DTEST_ENTRY
src/test_registry.*     the list the menu prints
src/tests/tNN_*.cpp     one test per file
```

See `../../CLAUDE.md` for the hardware facts that constrain this firmware.
