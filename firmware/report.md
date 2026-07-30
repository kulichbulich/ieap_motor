# Oživení ESP32-S3-WROOM-2 na desce esp32stepper — co bylo špatně a jak s tím zacházet
@doc_pwc: file:report.md | id:HW-00173 | created:2026-07-30 | rev:1 | revised:2026-07-30_0913 | type:PROC

Postmortem k tomu, proč se na desku `PCB/esp32stepper.kicad_pro` nedalo nic nahrát, a závazný postup
pro příště. Řešeno 2026-07-29 a 2026-07-30. **Modul vadný nebyl** — jeden den se to omylem uzavřelo
jako hardwarová závada, což byl špatný závěr.

---

## Shrnutí jednou větou

Na sobě ležely **tři nezávislé problémy**, každý maskoval ten další, a první dva dohromady vypadaly
přesně jako mrtvý hardware.

| # | Problém | Projev | Náprava |
|---|---|---|---|
| 1 | Flash zaseknutá v OPI režimu | `Failed to communicate with the flash chip`, `Manufacturer: 00` | úplné odpojení napájení |
| 2 | Nastavené block-protect bity ve flash | `flash_id` OK, ale zápis tiše selže | `esptool write-flash-status` |
| 3 | Quad build konfigurace na oktálním modulu | boot loop na `do_core_init` assertu | `opi_opi` + `dout` + 32 MB |

Navíc se ukázalo, že **esptool z PlatformIO (4.5.1) je na téhle desce nepoužitelný** a nahrávat se
musí samostatným esptoolem 5.3.1.

---

## Varianta modulu — potvrzeno

`U1` je **ESP32-S3-WROOM-2 N32R16V**: 32 MB oktální (OPI) flash + 16 MB oktální PSRAM, obojí 1,8 V.
Flash je samostatná die na substrátu modulu, ne v čipu.

Ověřeno proti efuse přímo na čipu i proti `RDID`:

```
FLASH_TYPE   = 8 data lines          VDD_SPI_XPD   = True
PSRAM_CAP    = 16M                   VDD_SPI_TIEH  = 1.8V LDO
PSRAM_VENDOR = AP_1v8                VDD_SPI_FORCE = True
FLASH_CAP    = None  (spravne: flash neni v cipu, ale na substratu modulu)
RDID         = C2 80 39  -> Macronix MX25UM25645G, 32 MB
```

BOM `PCB/esp32stepper.csv` uvádí u `U1` jen „ESP32-S3-WROOM-2" bez paměťové přípony, protože se
generuje z KiCadu. **Variantu ber odsud, ne z BOM.** Právě ta nejistota v BOM stála za problémem č. 3.

---

## Problém 1 — flash zaseknutá v OPI režimu

**Projev.** `esptool flash_id` hlásil `Failed to communicate with the flash chip` a
`Manufacturer: 00, Device: 0000`. Čtení flash vracelo nedeterministická data — tři čtení téže adresy
`0x0` daly tři různé MD5. Chip erase „uspěl" za 0,6 s (u 32 MB nemožné) a nic nesmazal.

**Proč.** Reset přes `EN` resetuje procesor, ale **neresetuje flash die**. Když v ní zůstane OPI
režim (nastaví ho ROM podle efuse při bootu, viz problém 3), ROM ani esptool s ní v SPI/DOUT režimu
nedomluví. Ta nedeterministická data byly plovoucí datové linky.

**Náprava.** Úplně odpojit napájení. **Pozor: deska umí jet i z 24 V barrel jacku** (`LT8610` buck →
`LDL1117` na 3V3), takže vytažení jen USB modul nevypne. Odpoj USB **i** barrel jack a počkej ~10 s
na vybití kondenzátorů.

Po power cyclu začal `flash_id` vracet správně `C2 / 8039 / 32MB`.

**Slepé uličky, které stály čas** — tudy ne:

- verze esptoolu (4.5.1 i 5.3.1 selhaly stejně), se stubem i `--no-stub`
- rychlosti: 4 baud raty × 2 režimy stubu, všech 8 kombinací identicky `Manufacturer: 00`.
  Přes nativní USB-Serial/JTAG je baud rate principiálně **no-op**, je to USB CDC — `upload_speed`
  na téhle cestě nemůže udělat nic.
- pájení FTDI na UART0 — nepomůže, závada je na sběrnici SPI0↔flash, tedy *za* hostitelským
  rozhraním; přes UART dostaneš stejnou chybu
- napájení: program u téhle flash tahá jednotky mA, kdežto běžící CPU s USB řádově víc a ten jede
- efuse: jsou od výroby správné, **nic v nich nepal** (viz níže)

## Problém 2 — block-protect bity ve flash

**Projev.** `flash_id` už vracel správně `C2/8039/32MB`, ale zápis tiše neprošel:
`MD5 of file does not match data in flash!`, u zápisu mimo hlavičku
`Write failed, the written flash region is empty`. Chip erase pořád „uspěl" a nic nesmazal.

**Diagnóza.** Status registr `MX25UM25645G` čten jako `SR1 = 0xB0`:

| bit7 | bit6 | bit5 | bit4 | bit3 | bit2 | bit1 | bit0 |
|---|---|---|---|---|---|---|---|
| Reserved | Reserved | **BP3=1** | **BP2=1** | BP1=0 | BP0=0 | WEL=0 | WIP=0 |

`BP[3:0] = 1100`, tedy nenulové → pole chráněné proti zápisu. Datasheet to popisuje doslova:

> *„If a program or erase instruction is applied to a protected memory area, the instruction will be
> ignored and WEL will clear to 0."*
>
> *„only if Block Protect bits (BP3:BP0) set to 0, the CE instruction can be executed"*

To vysvětluje jak tichý fail zápisu, tak chip erase, který nic nesmazal.

**Pozor na dvě věci, které se snadno spletou** (a já je spletl, než jsem si přečetl datasheet):
`MX25UM` (oktální) série **nemá bity `SRWD` ani `QE`** — bit7 a bit6 jsou Reserved. Neber layout
status registru z analogie s quad sérií `MX25U`.

**Náprava.**

```bash
esptool --port /dev/ttyACM0 read-flash-status  --bytes 1                      # kontrola: 0xb0
esptool --port /dev/ttyACM0 write-flash-status --bytes 1 --non-volatile 0x00  # naprava
esptool --port /dev/ttyACM0 read-flash-status  --bytes 1                      # overeni: 0x00
```

Proč přesně takhle:

- `--bytes 1` **záměrně**: `WRSR` (`01h`) bere 8 nebo 16 bitů a jen 8bitová forma nechá
  **konfigurační registr netknutý** (dummy cycles, ODS). 16 bitů by ho přepsalo a rozbilo časování
  čtení.
- `--non-volatile` je **nutné**: posílá `WREN` (`06h`) před `WRSR`, což datasheet vyžaduje. Bez něj
  jde `WEVSR` (`50h`), který tenhle díl nemusí znát.
- `BP` bity jsou nevolatilní, ale **volně přepisovatelné** — není to OTP, jde to vrátit.
- OTP bit `WPSEL` se tímhle nastavit **nedá**, má jiný opcode (`68h`). Kdyby byl `WPSEL=1`, byly by
  `BP` bity bez efektu a blokovaly by `DPB` bity po každém zapnutí; poznalo by se to tím, že se
  zápis do SR neprojeví.

Příčina, proč byly `BP` bity nastavené, není prokázaná. Nejpravděpodobněji je nastavily zápisy do
flash zaseknuté v OPI režimu (problém 1), kde se SPI příkazy interpretují jako něco jiného.

## Problém 3 — quad konfigurace na oktálním modulu

**Projev.** Nahrávání konečně prošlo s `Hash of data verified`, ale deska se resetovala ve smyčce:

```
SPIWP:0xee
Octal Flash Mode Enabled
For OPI Flash, Use Default Flash Boot Mode
...
assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)
```

**Proč.** ROM podle efuse zapne OPI, ale aplikace byla přeložená pro quad (`qio_qspi`, `dio`, 8 MB),
takže inicializace flash v aplikaci selže.

**Tohle přímo vyvrací, co dřív tvrdil `testing_firmware/README.md`:** že quad konfigurace
„nabootuje na každé variantě S3 včetně modulu, který octal paměti opravdu má". Nenabootuje. **Žádný
quad/oktální společný jmenovatel neexistuje** a obě README i `platformio.ini` jsou už opravené.

**Správná konfigurace** pro N32R16V:

```ini
board_upload.flash_size = 32MB
board_build.partitions = default_16MB.csv
board_build.flash_mode = dout
board_build.arduino.memory_type = opi_opi
```

- `flash_mode = dout`, **ne `opi`**: 1. ani 2. bootloader OPI neumí, zavádí se v DOUT a ROM režim
  nahradí podle efuse. Stejně to má i board def `esp32s3camlcd`, jediný v platformu s `opi_opi`.
- `default_32MB.csv` ve frameworku **neexistuje**; `default_16MB.csv` na 32 MB čipu jede (nevyužije
  zbytek), pro víc prostoru je `large_littlefs_32MB.csv`.

---

## Postup nahrávání odteď

`pio run -t upload` na téhle desce **nepoužívej**. PlatformIO má v sobě esptool 4.5.1
(`tool-esptoolpy 1.40501.0`), který na oktální flash přes nativní USB-Serial/JTAG padá na
`BrokenPipeError: [Errno 32] Broken pipe`, jakmile se čip zároveň resetuje (typicky když je v boot
loopu). Samostatný esptool 5.3.1 se připojí bez problémů.

```bash
# jednorazova priprava
python3 -m venv ~/venv-esptool && ~/venv-esptool/bin/pip install esptool

# 1) jen build, BEZ -t upload
pio run -e usb_j5            # nebo -e menu v testing_firmware

# 2) nahrani samostatnym esptoolem
~/venv-esptool/bin/python -m esptool --port /dev/ttyACM0 --baud 460800 \
    --before usb-reset --connect-attempts 5 \
    write-flash --flash-mode keep --flash-freq keep --flash-size keep \
    0x0     .pio/build/usb_j5/bootloader.bin \
    0x8000  .pio/build/usb_j5/partitions.bin \
    0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x10000 .pio/build/usb_j5/firmware.bin
```

`keep` u všech tří parametrů je důležité — nechá hlavičku z buildu být, aby ji esptool nepřepsal na
jinou velikost nebo režim flash. `--before usb-reset` pomáhá, když se čip resetuje ve smyčce.

## Diagnostický postup, když to zase nejde

Vždycky v tomhle pořadí, od nejlevnějšího:

1. **`flash_id`** — první věc, ještě před jakýmkoli buildem.
   ```bash
   ~/venv-esptool/bin/python -m esptool --port /dev/ttyACM0 flash-id
   ```
   Zdravý stav: `Manufacturer: c2`, `Device: 8039`, `Detected flash size: 32MB`, **bez** varování.
2. **`Manufacturer: 00`** → problém 1. Odpoj USB **i** barrel jack, počkej 10 s, zopakuj.
3. **`flash_id` OK, ale zápis selže** → problém 2. Přečti status registr, případně vynuluj.
4. **Nahraje se, ale boot loop** → problém 3. Zkontroluj `memory_type`/`flash_mode`/`flash_size`
   proti tabulce výše a proti tomu, na jaké desce jsi (WROOM-2 vs. DevKit N8R2).

## Co nikdy nedělat

- **Nepal efuse.** Jsou od výroby správné. `espefuse burn_*` je nevratné a špatné `VDD_SPI` by 1,8 V
  flash zničilo. Nic v nich opravovat netřeba a `summary` je bezpečné (jen čte).
- **Nezapisuj do status registru 16bitovou formou** (`--bytes 2`), přepsalo by to konfigurační
  registr.
- **Neřeš to rychlostmi.** Přes nativní USB je `upload_speed` no-op.
- **Nenos oktální konfiguraci na DevKit N8R2.** Ten má quad flash a skončí na
  `E cpu_start: Octal Flash option selected, but EFUSE not configured!`.

## Otevřené otázky

- **Reset/BOOT hardware desky není zdokumentovaný.** BOM uvádí jen `SW1`, který schéma kreslí jako
  napájecí `SW_SPDT`, plus dvoupinovou lištu `J1`, která v BOM není. `testing_firmware/README`
  tvrdí „tlačítko `BOOT1`, ale žádný reset", `test_simple/platformio.ini` mluví o „cvakni reset na
  J1". Než se na konkrétní tlačítko spolehneš, ověř to ve schématu.
- **Banner z `test_simple` se přes nativní USB CDC nezachytí** — cokoli vypsaného před otevřením
  portu hostem se ztratí. `ALIVE #n` v sekundové smyčce ověří, že to běží; na banner je potřeba
  externí FTDI na `J5`.
- Nikdy se neověřilo, jestli je závada č. 2 na jednom kusu, nebo na celé sérii. **Zkus druhou
  desku**, ať se to ví.

## Stav k 2026-07-30

Deska běží. Firmware `test_simple` nahraný a ověřený (`Hash of data verified`), `ALIVE` počítadlo
běží, po resetu startuje čistě od `#2`, žádný boot loop. Z testovací sady `testing_firmware` zatím
na téhle desce nic neběželo — `T00` a `T01` jsou odladěné jen na DevKitu N8R2.
