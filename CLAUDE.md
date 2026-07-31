# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

Hardware project for a custom 4-channel stepper controller: **ESP32-S3-WROOM-2 + 4× TMC2209
SilentStepStick (BOB)**, 100×80 mm, 4-layer KiCad board. Two halves:

- [PCB/](PCB/) — KiCad 9 project `esp32stepper` (schematic, layout, BOM CSV, gerber exports)
- [firmware/testing_firmware/](firmware/testing_firmware/) — PlatformIO bring-up test suite

**There is no application firmware yet.** The only code in the repo is a ladder of hardware tests
whose purpose is to prove the board works, one subsystem at a time. Do not treat it as a motion
control library.

## Firmware commands

All from [firmware/testing_firmware/](firmware/testing_firmware/):

```bash
pio run -e menu -t upload && pio device monitor   # normal flow: all tests + console menu
pio run -e t00_chip_info -t upload                # single-test binary (nothing else in the image)
pio run -t clean
pio device list                                   # if upload cannot find the port
```

Compiling **every** environment is the only automated gate — there is no host-side test suite, no
CI, no linter. `default_envs = menu`, so a bare `pio run` builds only `menu`; to check that nothing
broke, list the envs explicitly:

```bash
pio run -e menu -e t00_chip_info -e t01_blink_io38 -e t02_shift_register -e t03_step_pins \
        -e t04_endswitches -e t05_i2c_scan -e t06_tmc_uart -e t07_wiring_selftest -e t08_motor_move
```

"Running a single test" means either `-e tNN_*` (its own binary) or flashing `menu` and typing the
test number at the `test>` prompt. `a` runs 00–07 in sequence (skips T08); `?` reprints the menu.

## Firmware architecture

A test is **one `void tNN_name()` function, no arguments**, in `src/tests/tNN_name.cpp`. Two ways
the same sources get built, selected purely by `platformio.ini`:

| env | `build_src_filter` | `TEST_ENTRY` | `main.cpp` compiles to |
|---|---|---|---|
| `menu` | everything | undefined | interactive menu over `TESTS[]` from `src/test_registry.cpp` |
| `tNN_*` | `main.cpp` + that one test | `-DTEST_ENTRY=tNN_name` | run the test at boot, repeat on keypress |

Because single-test envs exclude `test_registry.cpp`, nothing in a test may reference the registry.

- [lib/testkit/](firmware/testing_firmware/lib/testkit/) — everything shared: console + result
  counters (`tk::pass/fail/warn/info`, `reset_results`, `summary`), input helpers, `tk::safe_state()`,
  `tk::ShiftReg` (74HC595), `tk::TmcBus` (one-wire TMC2209 UART with CRC8 and echo discard).
  New shared hardware access belongs here, not in a test.
- `tk::fail_count()` is what sequence mode reads to decide whether to stop — a test that reports
  through `Serial.print` instead of `tk::pass/fail` is invisible to the runner.
- [include/board_pins.h](firmware/testing_firmware/include/board_pins.h) — the pin map, `constexpr`
  only, no code.

### Adding a test — four places, all required

1. `src/tests/tNN_name.cpp` with `void tNN_name()`
2. declaration in `src/test_registry.h`
3. row in `src/test_registry.cpp` (name, what it proves, what must be connected)
4. `[env:tNN_name]` section in `platformio.ini`

## Hardware facts that constrain the firmware

These are not preferences — getting them wrong bricks the boot or the board.

- **`include/board_pins.h` is the only authoritative pin map**, read off the netlist and re-verified
  against `PCB/esp32stepper.kicad_pcb`. Never take pin numbers from prose. STEP0–3 = IO4–7;
  74HC595 SRCLK/SER/RCLK = IO8/9/10; I2C SCL/SDA = IO11/12; end switches = IO13–16; TMC UART
  TX/RX = IO17/18; debug UART on header J5 = IO2/IO1; spare on J4 = IO38.
- **The 74HC595 DIR/EN bit order is interleaved, not blocked**: MSB-first write gives
  `bit0→dir0, bit1→en0, bit2→dir1, bit3→en1, …`. Always go through `SR_BIT_DIR[]`/`SR_BIT_EN[]`
  and `ShiftReg::set_dir/set_enable`, never hand-rolled shifts.
- **EN is the SilentStepStick's active-low `ENN`** — `0` enables a driver. `SR_ALL_DISABLED = 0xAA`.
  The 595's `~OE` is hard-tied to GND, so it drives outputs the instant the ESP32 boots; a known-safe
  byte must be pushed out first (`ShiftReg::begin()` does this).
- **No USB-UART bridge.** The console is native USB CDC on IO19/IO20 (`ARDUINO_USB_MODE=1`,
  `ARDUINO_USB_CDC_ON_BOOT=1`). Anything printed before the host opens the port is lost, and
  `HWCDC::operator bool()` only reports whether the cable is plugged in — a terminal attaching is
  undetectable. Hence `main.cpp`'s `wait_for_input()` re-prints its prompt every 2 s until the first
  byte ever arrives; **blocking waits must use that pattern, not a bare `while (!Serial.available())`**.
- **UART0 (IO43/IO44) is physically unconnected** — never route logging there.
- **No software-controllable LED.** LED1 hangs off the LT8610's PG pin via an NPN. IO38 goes only to
  the single-pin header J4.
- **Flash config is deliberately quad + 8 MB** (`dio`, `qio_qspi`, `default_8MB.csv`) because the BOM
  says only "ESP32-S3-WROOM-2" without a memory variant. Setting `opi` on a quad module makes the
  bootloader `abort()` in an endless reset loop. Do not "upgrade" these settings without proof of the
  module variant; T00 reports what was actually found.
- All four drivers share **one** UART wire (IO17 → R19 1k → bus, IO18 reads it), so the ESP32 hears
  its own transmission — `TmcBus` discards the echo. Driver addresses 0–3 come from pull-ups
  R13–R16 on MS1/MS2.

## Bring-up status and safety

Only **T00 `chip_info` and T01 `blink_io38`** have run on real hardware (and on an
ESP32-S3-DevKitC-1 N8R2, not the target board). **T02–T08 compile but have never executed** — their
PASS/FAIL logic is unverified. Treat them as code under review, and do not suggest running them to
bring up a fresh board.

- `main.cpp` calls `tk::safe_state()` before and after every test (STEP pins low, `SR = 0xAA`).
  Every new exit path must leave the board safe; a test that enables a driver disables it itself.
- **T08 is the only test that moves a motor** and needs 24 V; sequence mode skips it.
- Any loop must poll `tk::key_pressed()` so the operator can escape.

`T07 wiring_selftest` is the design worth extending: the TMC2209's `IOIN` register reads back the
levels of its own DIR/ENN/STEP pins, so the firmware sets a level and reads it back over UART —
proving the whole signal path (595 bit order, cold joints, swapped STEP pins, wrong address) with no
scope and the drivers disabled. Prefer extending T07 over adding another probe-and-eyeball test.

## Conventions

- **Firmware comments and all console output are Czech, ASCII-only, no diacritics** — some terminals
  mangle UTF-8 over USB CDC. Markdown docs use full Czech with diacritics.
- Docs are bilingual and must be kept in sync as a pair:
  [README.md](firmware/testing_firmware/README.md) (EN) and
  [README_CZ.md](firmware/testing_firmware/README_CZ.md) (CZ). Changing firmware behaviour means
  editing both.
- Commit subjects are tagged: `[fw]`, `[pcb]`, `[doc]`, `[upd]`.
- Never put AI co-authorship or attribution in commit messages.

## PCB

- Root sheet `esp32stepper.kicad_sch` instantiates `esp32.kicad_sch`, `PWR.kicad_sch`,
  `CONN.kicad_sch` and `tmc2209_driver.kicad_sch`. `drivers.kicad_sch` is present but not referenced
  by the root sheet.
- Stackup: F.Cu / PWR.Cu / GND.Cu / B.Cu.
- Manufacture exports are versioned directories plus a matching zip:
  `PCB/Manufacture/Export_v1.0/`, `Export_v1.1/`. v1.1 is the newest.
- `esp32stepper.csv` is the BOM, `esp32stepper_report.txt` the KiCad statistics report; both are
  generated — regenerate from KiCad rather than editing.
- Datasheets and the upstream TMC2209-BOB KiCad source live in [Document/](Document/).
