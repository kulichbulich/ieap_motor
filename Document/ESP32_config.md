# ESP32-S3-WROOM-2 (U1) — measured configuration of this board
@doc_pwc: file:ESP32_config.md | id:HW-00200 | created:2026-07-30 | rev:1 | revised:2026-07-30_0911 | type:REF

Everything in this document was **read off this board**, not copied from a datasheet:

- silicon data with `esptool.py` / `espefuse.py` **v4.5.1** (the copy bundled with PlatformIO,
  `~/.platformio/packages/tool-esptoolpy`) over native USB on **2026-07-30**,
- wiring from the KiCad netlist `PCB/esp32stepper.kicad_pcb`, U1 pad-by-pad.

The BOM (`PCB/esp32stepper.csv`) lists U1 only as `ESP32-S3-WROOM-2` without a memory suffix.
The suffix is **N32R16V** — 32 MB octal flash + 16 MB octal PSRAM, both 1.8 V.

---

## 1. Chip identity

| Item | Value | Source |
|---|---|---|
| Chip | ESP32-S3, revision **v0.2** (`WAFER_VERSION_MAJOR=0`, `WAFER_VERSION_MINOR=2`) | `esptool chip_id` |
| Cores / features | 2 × Xtensa LX7, WiFi + BLE, crystal **40 MHz** | `esptool chip_id` |
| Package | `PKG_VERSION = 0` | eFuse BLOCK1 |
| Factory MAC | `<MAC>` — real value in `./credentials.md` in the repo root (gitignored); `CUSTOM_MAC` is unprogrammed | eFuse BLOCK1 |
| Unique ID | `<UNIQUE_ID>` — real value in `./credentials.md` | eFuse BLOCK2 |
| Calibration | `BLK_VERSION_MAJOR = With calibration`, `BLK_VERSION_MINOR = 4` — ADC1/ADC2 and temperature-sensor calibration present (`TEMP_SENSOR_CAL = -17.9`) | eFuse BLOCK1/2 |
| USB identity on the host | `303a:1001`, `Espressif USB JTAG/serial debug unit <MAC>` (the USB serial string **is** the MAC), appears as `/dev/ttyACM0` | `udevadm`, `lsusb` |

## 2. Memory — measured, not assumed

| Item | Value | How it was established |
|---|---|---|
| Flash size | **32 MB** (256 Mbit) | `esptool flash_id` → `Detected flash size: 32MB` |
| Flash JEDEC ID | `Manufacturer: c2` (Macronix), `Device: 8039` — octal 1.8 V family | `esptool flash_id` |
| Flash bus | **octal, 8 data lines** (`FLASH_TYPE = 1`) | eFuse BLOCK0 |
| PSRAM | **16 MB octal** (`PSRAM_CAP = 0b11`), vendor **AP 1.8 V** (`PSRAM_VENDOR = 0b10`), `PSRAM_TEMP = 1` | decoded from raw BLOCK1, see below |
| `FLASH_CAP` / `FLASH_VENDOR` | `None` / `None` — **expected, not a fault**: on WROOM-2 the flash is a separate die on the module substrate, not inside the chip package, so the in-package capability fuses stay blank | eFuse BLOCK1 |
| VDD_SPI rail | **1.8 V**: `VDD_SPI_XPD = True`, `VDD_SPI_TIEH = Connect to 1.8V LDO`, `VDD_SPI_FORCE = True` → *"Flash voltage (VDD_SPI) set to 1.8V by efuse"* | eFuse BLOCK0 |
| GPIO33…37 supply | `PIN_POWER_SELECTION = VDD_SPI` — those pins are used internally by the octal memories and are **not brought out** (module pads 28/29/30 are `NC`) | eFuse BLOCK0 |
| Flash ECC | `FLASH_ECC_EN = False`, `FLASH_ECC_MODE = 16-byte to 18-byte`, `FLASH_PAGE_SIZE = 0`, `FLASH_TPUW = 0` | eFuse BLOCK0 |

`espefuse.py` v4.5.1 does not name the PSRAM fields, so they were decoded from the raw block.
The dump is recorded here so the decode can be re-checked:

```
MAC_SPI_8M_0 (BLOCK1) read_regs: <MAC_W0> <MAC_W1> 00000000 04080000 64196938 6400d404
    bits 123..125 FLASH_CAP    = 0b000 -> None      bits 128..130 FLASH_VENDOR = 0b000 -> None
    bits 131..132 PSRAM_CAP    = 0b11  -> 16 MB     bits 135..136 PSRAM_VENDOR = 0b10  -> AP_1v8
```

Words 0 and 1 of BLOCK1 are the factory MAC (bits 0…47) and are redacted here — the full dump is in
`./credentials.md`. They are not needed for the decode above: every field shown lives in words 3…5.

This agrees with an independent read using esptool **5.3.1** on 2026-07-29
(`PSRAM_CAP = 16M`, `PSRAM_VENDOR = AP_1v8`), so two tool versions confirm it.

## 3. Build settings that follow from the above

This is what both `firmware/testing_firmware/platformio.ini` and
`firmware/test_simple/platformio.ini` now carry (commit `ffcccbe`, 2026-07-30):

```ini
board                              = esp32-s3-devkitc-1   ; generic S3 base, then override:
board_upload.flash_size            = 32MB
board_build.partitions             = default_16MB.csv     ; or large_littlefs_32MB.csv
board_build.flash_mode             = dout                 ; NOT opi — see below
board_build.arduino.memory_type    = opi_opi              ; octal flash + octal PSRAM
```

- **`flash_mode` is deliberately `dout`, not `opi`.** Neither the 1st- nor the 2nd-stage bootloader
  implements OPI, so the image is loaded in DOUT and the ROM switches the mode according to the
  eFuse (`Octal Flash Mode Enabled`). The only board definition in the platform that uses
  `opi_opi`, `esp32s3camlcd`, does the same thing.
- **A quad configuration does not boot on this board**, contrary to what
  `firmware/testing_firmware/README*.md` used to claim: the ROM turns OPI on from the eFuse, and an
  application built for quad then dies in a reset loop with
  `assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)`. The "conservative quad common
  denominator" is therefore *not* a safe fallback here — it is simply wrong for an N32R16V.
- The Arduino framework ships **no `default_32MB.csv`**. `default_16MB.csv` runs fine on a 32 MB
  chip (it just leaves the rest unused); `large_littlefs_32MB.csv` is there if more space is needed.
- **Never carry `opi_opi` to the ESP32-S3-DevKitC-1 N8R2** used for earlier bring-up tests: it has
  quad flash and boot-loops with `E cpu_start: Octal Flash option selected, but EFUSE not
  configured!`. The two boards need different configs — do not share one env between them.
- **Never burn eFuses to "fix" a mismatch** — eFuses are one-way, and this module's are already
  correct and factory-consistent.

## 4. eFuse / security state — virgin chip

Nothing is locked, nothing is burned, and the block integrity check passes
(`EFUSE_RD_RS_ERR0/1 = 0x00000000`):

| Fuse | Value |
|---|---|
| `WR_DIS` / `RD_DIS` | `0x00000000` / `0b0000000` — no fuse is write- or read-protected |
| `SECURE_BOOT_EN` | `False` (all three `SECURE_BOOT_KEY_REVOKE*` = `False`) |
| `SPI_BOOT_CRYPT_CNT` | `Disable (0b000)` — flash encryption off |
| `BLOCK_KEY0…5`, `BLOCK_USR_DATA`, `BLOCK_SYS_DATA2` | all zero, purpose `USER` |
| `DIS_DOWNLOAD_MODE`, `DIS_FORCE_DOWNLOAD`, `ENABLE_SECURITY_DOWNLOAD` | `False` — download mode fully available |
| `DIS_USB`, `DIS_USB_JTAG`, `DIS_USB_SERIAL_JTAG`, `USB_PHY_SEL`, `USB_EXCHG_PINS` | `False` — internal PHY, USB-Serial/JTAG enabled, D+/D− not swapped |
| `STRAP_JTAG_SEL` | `False` → JTAG comes from the USB-Serial/JTAG unit; GPIO3 does not select it |
| `UART_PRINT_CONTROL` | `Enabled` — the ROM boot log is printed |
| `SECURE_VERSION` | `0` (no anti-rollback) |
| `SPI_PAD_CONFIG_*` | all `0` — the module uses the default SPI pads |

## 5. How the module is wired on this board

All 41 pads of U1, straight from the netlist. `NC` means the pad is connected to nothing on the
PCB — free for future use, except where noted.

| Pad | Module pin | Net | Function on this board |
|---|---|---|---|
| 1, 40, 41 | GND | `GND` | ground (41 = large thermal pad) |
| 2 | 3V3 | `+3V3` | the only supply rail, see §6 |
| 3 | EN | `reset` | R3 10k pull-up to +3V3, C1 100n to GND, brought out to 2-pin header **J1** (J1.1 = GND, J1.2 = EN) — **no reset button on the board**, short J1 to reset |
| 4 | IO4 | `step0` | STEP → BOB-0 |
| 5 | IO5 | `step1` | STEP → BOB-1 |
| 6 | IO6 | `step2` | STEP → BOB-2 |
| 7 | IO7 | `step3` | STEP → BOB-3 |
| 8 | IO15 | `endswitch2` | end switch 2 (10k pull-up, 10 R in series) |
| 9 | IO16 | `endswitch3` | end switch 3 |
| 10 | IO17 | `/esp32/TX` | TMC2209 single-wire UART **TX**, through R19 1k into the shared bus |
| 11 | IO18 | `UART` | TMC2209 single-wire UART **RX** — bus shared by BOB-0…3 pin 7 |
| 12 | IO8 | `SRCLK` | 74HC595 (U4) shift clock, U4.11 |
| 13 | USB_D−/IO19 | `USB D-` | → U5 USBLC6-2SC6 ESD array → USB-C **USB1** |
| 14 | USB_D+/IO20 | `USB D+` | → U5 USBLC6-2SC6 ESD array → USB-C **USB1** |
| 15 | IO3 | NC | free (strapping pin, see §7) |
| 16 | IO46 | `Net-(U1-IO46)` | **R42 10k to GND** — strapping pin held low |
| 17 | IO9 | `SER` | 74HC595 serial data, U4.14 |
| 18 | IO10 | `RCLK` | 74HC595 latch clock, U4.12 |
| 19 | IO11 | `SCL` | I²C clock (3k3 pull-up R27) → TCA9548A (U6) @ 0x70 |
| 20 | IO12 | `SDA` | I²C data (3k3 pull-up R28) |
| 21 | IO13 | `endswitch0` | end switch 0 |
| 22 | IO14 | `endswitch1` | end switch 1 |
| 23 | IO21 | NC | free |
| 24 | IO47 | NC | free |
| 25 | IO48 | NC | free |
| 26 | IO45 | NC | free (strapping pin, see §7) |
| 27 | IO0 | `Net-(U1-IO0)` | tactile switch **BOOT1** (B3SL-1002P) to GND; internal weak pull-up in the chip |
| 28, 29, 30 | NC | — | not brought out by the module (a WROOM-1 uses these pads for GPIO35/36/37; on WROOM-2 the octal memories use them internally) |
| 31 | IO38 | `IO38` | single-pin header **J4** — the only GPIO deliberately left free |
| 32 | MTCK/IO39 | NC | free (JTAG pad) |
| 33 | MTDO/IO40 | NC | free (JTAG pad) |
| 34 | MTDI/IO41 | NC | free (JTAG pad) |
| 35 | MTMS/IO42 | NC | free (JTAG pad) |
| 36 | RXD0/IO44 | NC | **UART0 RX unconnected** |
| 37 | TXD0/IO43 | NC | **UART0 TX unconnected** |
| 38 | IO2 | `tx_ext` | debug UART **TX** → header **J5.2** |
| 39 | IO1 | `rx_ext` | debug UART **RX** → header **J5.3** (J5.1 = GND) |

Consequences worth spelling out:

- **UART0 (IO43/IO44) goes nowhere on this board.** Never route logging there. The ROM bootloader
  talks on UART0, so an external FTDI cannot flash the board unless it is soldered directly to the
  module pads. The board's debug UART is **IO2/IO1 on J5**, and only application firmware speaks
  on it.
- **There is no USB-UART bridge.** The console is native USB CDC through USB1, so anything printed
  before the host opens the port is lost forever — a freshly attached terminal can show an empty
  screen on a perfectly healthy board.
- **USB1 is device-only**: USB-C receptacle USB4085-GF-A with 5.1k CC1/CC2 pulldowns (R1, R2);
  D+/D− protected by U5; VBUS also feeds the USB LDO.
- **No software-controllable LED.** LED1 hangs off the LT8610's PG pin via NPN1 (BC817-40).
- **IO38 on J4 is the only free brought-out GPIO.** The other free pins (IO3, IO21, IO45, IO47,
  IO48, IO39…IO42) need soldering to the module.
- The authoritative firmware-side copy of this map is
  `firmware/testing_firmware/include/board_pins.h`; the pin table in `firmware/README.md` and the
  legacy `firmware/*.py` scripts disagree and are **wrong for this board**.

## 6. Power — how the module is fed

Two sources OR into one **+3V3** rail; there is no +5V net on this board at all:

```
J2 barrel jack (24Vin) → PMOS1 IRF4905 high-side switch (gate on header J6 "PWR_SWITCH",
                         D3 BZX84B9V1 zener) → 24V_Motor → F2 polyfuse MINISMDC150F-24-2
                       → 24V (TVS0 TSM24A, fan1 header) → U3 LT8610 buck → +3V3 → U1.2
USB1 VBUS              → U2 LDL1117 LDO → D2 BAT54WG → +3V3 → U1.2
```

- The LT8610's BIAS pin is tied to its own +3V3 output; its PG pin drives LED1 through NPN1.
- Motor power is separate downstream of the polyfuse: `24V_Motor` reaches BOB-0…3 `+VM` through
  individual fuses F1, F3, F4, F5.
- **A real power cycle of the module needs USB *and* the barrel jack removed.** Pulling EN low
  (J1) resets the chip but does **not** reset the flash die.

## 7. Strapping pins

| Pin | On this board | Effect |
|---|---|---|
| IO0 | BOOT1 to GND, internal pull-up | boot select: hold BOOT1 and pulse J1 to enter download mode |
| IO46 | R42 10k to GND | held low — ROM message printing stays in its default mode |
| IO45 | floating (NC) | would select the VDD_SPI voltage, but `VDD_SPI_FORCE = True` means **the eFuse wins** and VDD_SPI is 1.8 V regardless of this pin |
| IO3 | floating (NC) | JTAG source select; irrelevant here because `STRAP_JTAG_SEL = False` |

Console and flashing, in one place:

- Upload and monitor over native USB (`/dev/ttyACM0`), `monitor_speed = 115200`.
- `upload_speed` is a no-op over USB-Serial/JTAG — baud only matters for a soldered-on FTDI.
- To force download mode: hold **BOOT1**, short **J1**, release BOOT1, then start the upload.
- An external FTDI on J5 must be **3.3 V**; a 5 V one damages the chip.

## 8. State of this unit

| Date | Finding |
|---|---|
| 2026-07-29 | Nothing could be flashed. `flash_id` returned `Manufacturer: 00 / Device: 0000` with *"Failed to communicate with the flash chip"*, identically across 4 baud rates × stub/no-stub and on both esptool 4.5.1 and 5.3.1; a full chip erase "succeeded" in 0.6 s (32 MB takes tens of seconds); three reads of the same address gave three different MD5s. Diagnosed as a fault on the SPI0↔flash path inside the module. eFuses, board wiring and power design were ruled out. |
| 2026-07-30 | **Recovered after a full power cycle** (module unplugged and reconnected). `flash_id` → `c2 / 8039`, 32 MB detected; three consecutive reads of `0x0…0x10000` returned the identical MD5 `0f546f10a7757fb7dd1f95f2dc4b78f6`. The flash path is healthy again, so the fault was not permanent — if it returns, a cold power cycle (USB **and** barrel jack) is the first thing to try. |
| 2026-07-30 | **First firmware to actually run on this board**: `test_simple` (env `usb_j5`) was flashed and printed `ALIVE #1…#449` over native USB CDC, one line per second, with `Saved PC` in `esp_pm_impl_waiti` — a healthy idle loop. Flashing and the USB CDC console are both proven on this hardware. |

Still open: `firmware/testing_firmware/` test T00 `chip_info` has not been run here, so the flash
size and PSRAM have not yet been confirmed *at runtime* by the firmware itself — everything in §2
comes from the eFuses and the flash ID. T00's two validated runs so far were on an
ESP32-S3-DevKitC-1 N8R2, not on this board.

Note for anyone reading the console log: `esptool` / `espefuse` always reset the chip into download
mode, so an attached `pio device monitor` will show
`rst:0x15 (USB_UART_CHIP_RESET), boot:0x23 (DOWNLOAD(USB/UART0))`, `waiting for download` and then
garbage bytes (ROM bootloader baud ≠ monitor baud). That is the tool, not a crash. Short **J1** or
re-plug USB to get the application running again, and expect port-access collisions if the monitor
is open while esptool runs.
