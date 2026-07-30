# Jak flashnout desku esp32stepper
@doc_pwc: file:FLASH.md | id:HW-00174 | created:2026-07-30 | rev:1 | revised:2026-07-30_0928 | type:PROC

Deska má **ESP32-S3-WROOM-2 N32R16V** (32 MB oktální flash). `pio run -t upload` na ní nefunguje —
esptool 4.5.1 z PlatformIO padá na `BrokenPipeError`. Buildí se přes `pio`, flashuje zvlášť.

## Příprava (jednorázově)

```bash
python3 -m venv ~/venv-esptool && ~/venv-esptool/bin/pip install esptool   # potreba 5.3.1+
```

## Flashnutí

```bash
cd firmware/test_simple            # nebo firmware/testing_firmware
pio run -e usb_j5                  # jen build, BEZ -t upload

~/venv-esptool/bin/python -m esptool --port /dev/ttyACM0 --baud 460800 \
    --before usb-reset --connect-attempts 5 \
    write-flash --flash-mode keep --flash-freq keep --flash-size keep \
    0x0     .pio/build/usb_j5/bootloader.bin \
    0x8000  .pio/build/usb_j5/partitions.bin \
    0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x10000 .pio/build/usb_j5/firmware.bin
```

Hotovo, když to skončí `Hash of data verified.` U `testing_firmware` zaměň `usb_j5` za `menu`.

## Když to nejde

Nejdřív vždycky `~/venv-esptool/bin/python -m esptool --port /dev/ttyACM0 flash-id`.
Zdravé je `Manufacturer: c2`, `Device: 8039`, `32MB`, bez varování.

| Co vidíš | Co s tím |
|---|---|
| `Manufacturer: 00` | Odpoj **USB i 24V barrel jack**, počkej 10 s, zapoj jen USB. (Reset přes EN flash neresetuje.) |
| `flash_id` OK, ale zápis selže | `esptool ... write-flash-status --bytes 1 --non-volatile 0x00` (block-protect bity) |
| Nahraje se, ale boot loop | Špatná paměťová konfigurace — musí být `opi_opi` + `dout` + `32MB` + `default_16MB.csv` |

Rychlostmi to neřeš, přes nativní USB je baud rate no-op. Efuse nikdy nepal.

Podrobné vysvětlení všech tří případů je v [report.md](./report.md) (HW-00173).
