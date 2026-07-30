# Jak flashnout desku esp32stepper
@doc_pwc: file:FLASH.md | id:HW-00174 | created:2026-07-30 | rev:2 | revised:2026-07-30_0956 | type:PROC

Deska má **ESP32-S3-WROOM-2 N32R16V** (32 MB oktální flash). `pio run -t upload` na ní nefunguje —
esptool 4.5.1 z PlatformIO padá na `BrokenPipeError`. Buildí se přes `pio`, flashuje zvlášť
samostatným esptoolem 5.3.1+.

## Doporučená cesta — `flash.sh`

```bash
firmware/flash.sh -d firmware/testing_firmware      # env se vezme z default_envs (= menu)
firmware/flash.sh -d firmware/test_simple           # (= usb_j5)
```

Skript nic nepředpokládá o stroji a je idempotentní — na **novém** si sám založí venv a doinstaluje
esptool, na **už použitém** jen zkontroluje verze. Postupně: najde `pio`, ověří esptool ≥ 5.3.1
(a doinstaluje / povýší, když ne), najde port, přeloží build, přečte `flash-id` a jen když je flash
zdravá, zapíše všechny čtyři regiony s `--flash-mode/freq/size keep`. Když něco selže, vypíše
konkrétní nápravu z tabulky níž.

Užitečné přepínače:

| | |
|---|---|
| `-e menu` | jiné prostředí než `default_envs` (např. `-e t01_blink_io38`) |
| `-p /dev/ttyACM1` | když je připojených víc desek (jinak se jediná najde sama) |
| `--check` | jen diagnostika flash, **nic nezapisuje** — první věc, když se deska chová divně |
| `--no-build` | použij už přeložený build (např. přeložený z VSCode) |
| `--help` | nápověda |
| `ESPTOOL_VENV=...` | jiné umístění venv než `~/venv-esptool` |

## Ruční postup

Kdyby skript nešel použít. Platí to samé, jen si musíš doplnit cesty sám.

**Jednorázová příprava na stroji.** Idempotentní, dá se pustit i tam, kde už venv je:

```bash
python3 -m venv ~/venv-esptool 2>/dev/null; ~/venv-esptool/bin/pip install -U 'esptool>=5.3.1'
~/venv-esptool/bin/python -m esptool version     # musi hlasit 5.3.1 nebo vic
```

Tři věci, které na cizím stroji typicky chybí:

- **`pio` není v PATH.** Po instalaci PlatformIO IDE ve VSCode žije jen v `~/.platformio/penv/bin/pio` —
  použij tuhle plnou cestu, nebo si nainstaluj Core: `python3 -m pip install --user platformio`.
- **Práva na port.** `sudo usermod -aG dialout $USER` a **odhlásit/přihlásit** (jinak se skupina
  nepropíše). Bez toho esptool hlásí `Permission denied` na `/dev/ttyACM0`.
- **`python3-venv`.** Na Debianu/Ubuntu je to samostatný balík: `sudo apt install python3-venv`.

**Nahrání.** `boot_app0.bin` se stahuje s frameworkem, takže existuje až po prvním buildu:

```bash
cd firmware/testing_firmware        # nebo firmware/test_simple
pio run -e menu                     # jen build, BEZ -t upload  (test_simple: -e usb_j5)

~/venv-esptool/bin/python -m esptool --port /dev/ttyACM0 --baud 460800 \
    --before usb-reset --connect-attempts 5 \
    write-flash --flash-mode keep --flash-freq keep --flash-size keep \
    0x0     .pio/build/menu/bootloader.bin \
    0x8000  .pio/build/menu/partitions.bin \
    0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x10000 .pio/build/menu/firmware.bin
```

`keep` u všech tří parametrů je důležité — nechá hlavičku z buildu být, aby ji esptool nepřepsal na
jinou velikost nebo režim flash. Hotovo, když každý ze čtyř regionů skončí `Hash of data verified.`

## Když to nejde

Nejdřív vždycky `firmware/flash.sh --check` (ručně:
`~/venv-esptool/bin/python -m esptool --port /dev/ttyACM0 flash-id`).
Zdravé je `Manufacturer: c2`, `Device: 8039`, `32MB`, `octal (8 data lines)`, `1.8V`, bez varování.

| Co vidíš | Co s tím |
|---|---|
| `Manufacturer: 00` | Odpoj **USB i 24V barrel jack**, počkej 10 s, zapoj jen USB. (Reset přes EN flash neresetuje.) |
| `flash-id` OK, ale zápis selže | `esptool ... write-flash-status --bytes 1 --non-volatile 0x00` (block-protect bity) |
| Nahraje se, ale boot loop | Špatná paměťová konfigurace — musí být `opi_opi` + `dout` + `32MB` + `default_16MB.csv` |
| `Permission denied` na portu | Chybí skupina `dialout`, viz ruční postup výše |
| žádné `/dev/ttyACM*` | Kabel v konektoru **USB1** na desce; nebo drž BOOT a cvakni RESET |
| konzole zůstane prázdná | Nativní USB CDC ztrácí vše vypsané před otevřením portu — cvakni RESET, nebo stiskni Enter |

Rychlostmi to neřeš, přes nativní USB je baud rate no-op. Efuse nikdy nepal.

Podrobné vysvětlení všech tří případů je v [report.md](./report.md) (HW-00173).

## Ověřeno

2026-07-30 na `firmware/testing_firmware` (env `menu`), esptool 5.3.1: všechny čtyři regiony
`Hash of data verified.`, deska nabootovala, T00 hlásí 5 OK / 0 FAIL (32 MB flash, 16 MB PSRAM),
T01 přepíná IO38.
