# esp32stepper — sada testů hardwaru (bring-up)

Testovací sada pro vlastní čtyřkanálovou desku se stepper drivery (ESP32-S3-WROOM-2 + 4× TMC2209
SilentStepStick). **Nejde o aplikační firmware** — je to žebřík testů, který postupně, jeden
podsystém po druhém, dokazuje, že deska funguje.

English version: [README.md](README.md)

---

## ⚠️ Stav: odladěné jsou zatím jen první dva testy

| Testy | Stav |
|---|---|
| **T00 `chip_info`, T01 `blink_io38`** | spuštěné na reálném hardwaru, odladěné, chovají se podle dokumentace |
| **T02 … T09** | přeloží se, na hardwaru nikdy neběžely — logika PASS/FAIL je neověřená |

**Na nové desce nedoporučuji zkoušet T02 a výš.** Budí posuvný registr, STEP piny, TMC UART a (T08,
T09) skutečný motor; na neoživené desce se stejně snadno může mýlit neověřený test jako deska, a T08
s T09 navíc rozhýbou mechaniku. Novou desku oživuj jen testy T00 a T01, na všechno ostatní se koukej jako
na kód, který si před prvním spuštěním zaslouží revizi.

Ty dva odladěné testy běžely na **ESP32-S3-DevKitC-1 N8R2**, ne na cílové desce.

> **DevKit a cílová deska teď potřebují jiné nastavení pamětí.** Varianta cílového modulu je
> potvrzená: **ESP32-S3-WROOM-2 N32R16V** (32 MB oktální flash + 16 MB oktální PSRAM, 1,8 V) a
> `platformio.ini` je pro ni nastavené oktálně. Tahle konfigurace **na DevKitu N8R2 nenabootuje**,
> protože ten má quad flash. Při návratu na DevKit přepni `flash_mode`/`memory_type`/`flash_size`
> zpátky na `dout` + `qio_qspi` + `8MB`. Viz [../report.md](../report.md).

---

## Co je potřeba

- [PlatformIO](https://platformio.org/) CLI (`pio`) — platformu `espressif32` si stáhne sám při
  prvním buildu
- kabel USB-C do konektoru **USB1** na desce (nativní USB CDC na IO19/IO20)
- 24 V na barrel jack až pro T06 (nepovinně) a T08 / T09 (povinně)

Všechny příkazy níže se spouští z této složky (`firmware/testing_firmware/`).

## Rychlý start

```bash
pio run -e menu -t upload && pio device monitor
```

Nahraje binárku, která obsahuje všechny testy plus interaktivní menu, a otevře konzoli. To je běžný
režim — nahraje se jednou a testy se pak vybírají po USB, bez přeprogramování mezi nimi.

> **`-t upload` na cílové desce nefunguje spolehlivě.** PlatformIO má v sobě esptool 4.5.1, který na
> oktální flash přes nativní USB-Serial/JTAG padá na `BrokenPipeError: [Errno 32] Broken pipe`,
> zvlášť když se čip zároveň resetuje. Přelož přes `pio run` a nahraj **samostatným esptoolem
> 5.3.1**; přesný příkaz je v [../report.md](../report.md).

## Nahrávání a `../flash.sh --help`

Kvůli tomu výše se nahrává skriptem [../flash.sh](../flash.sh) — je idempotentní a nic
nepředpokládá o stroji: najde (nebo doinstaluje) `pio` a esptool ≥ 5.3.1, zkontroluje flash a teprve
pak zapíše všechny čtyři regiony. Postup i ruční varianta jsou ve [../FLASH.md](../FLASH.md).

`--help` vypíše nápovědu **a k tomu co který test dělá**, `--list` jen ten seznam. Oboje se čte
z `platformio.ini` a `src/test_registry.cpp`, tedy ze stejného zdroje, ze kterého ho vypisuje menu na
konzoli desky — seznam se proto nemůže rozejít s firmwarem.

```text
$ ../flash.sh --help
flash.sh - nahrani firmware do desky esp32stepper (ESP32-S3-WROOM-2 N32R16V).

Proc to nejde pres "pio run -t upload" a co delat, kdyz to nejde vubec, je
ve FLASH.md (HW-00174) a report.md (HW-00173).

Skript je zamerne IDEMPOTENTNI a nic nepredpoklada o stroji: sam si najde
pio i esptool, na novem stroji je dotahne, na uz pouzitem jen zkontroluje
verzi. Nic nemaze a nesaha na efuse.

Pouziti:
  firmware/flash.sh                                  # projekt = aktualni adresar, env = default_envs
  firmware/flash.sh -d firmware/testing_firmware     # projekt explicitne
  firmware/flash.sh -d firmware/test_simple -e usb_j5
  firmware/flash.sh -p /dev/ttyACM1                  # kdyz je pripojenych vic desek
  firmware/flash.sh --check                          # jen diagnostika flash, NIC nezapisuje
  firmware/flash.sh --no-build                       # pouzij uz prelozeny build
  firmware/flash.sh --help                           # tenhle text + co ktery test dela
  firmware/flash.sh --list                           # jen seznam prostredi a testu

--help i --list vypisou prostredi, ktera jde dat za -e, a u testu i to, co
overuji a co k tomu musi byt pripojene - tedy to same, co vypise menu na
konzoli desky. Bez -d se to vezme z aktualniho adresare, jinak ze vsech
projektu vedle skriptu.

Prepsat cestu k venv jde promennou ESPTOOL_VENV.

prostredi pro -e v <repo>/firmware/testing_firmware:
 * menu                 Vychozi prostredi: vsechny testy + menu na konzoli.
   t00_chip_info        hlasi se cip, sedi flash/PSRAM varianta
                          potreba: jen USB
   t01_blink_io38       GPIO vystup na pinu J4
                          potreba: LED+330R nebo multimetr
   t02_shift_register   74HC595 budi DIR a EN
                          potreba: sonda na patky BOB
   t03_step_pins        STEP pulsy na IO4..IO7
                          potreba: osciloskop
   t04_endswitches      koncove spinace IO13..IO16
                          potreba: spinace, ruka
   t05_i2c_scan         TCA9548A a kanaly enkoderu
                          potreba: nic / enkodery
   t06_tmc_uart         drivery odpovidaji na UART
                          potreba: osazene BOB-0..3
   t07_wiring_selftest  DIR/EN/STEP overene ctenim IOIN
                          potreba: osazene BOB-0..3
   t08_motor_move       skutecny pohyb jednoho motoru
                          potreba: 24 V + motor   POZOR: hybe motorem
   t09_motor_jog        pusteni motoru a jednoduche pohyby po krocich
                          potreba: 24 V + motor   POZOR: hybe motorem
   * = default_envs, pouzije se bez -e
```

Bez `-d` se seznam bere z aktuálního adresáře; spuštěný z kořene repa vypíše všechny projekty vedle
skriptu (`testing_firmware` i `test_simple`).

### Konzole je po připojení prázdná — tak to má být

Deska **nemá USB-UART převodník**, konzole je nativní USB CDC. Cokoli vypsaného dřív, než hostitel
otevře port, se nenávratně ztratí — čerstvě připojený terminál tak může zůstat prázdný i na
naprosto zdravé desce.

**Stiskni Enter.** Menu opakuje výzvu každé 2 s, dokud nedorazí první znak, takže do dvou sekund
uvidíš buď výzvu, nebo celé menu.

## Ovládání menu

```
test> 0        spustí jeden test podle čísla
test> a        spustí za sebou všechny testy, které nehýbou motorem (T08 a T09 vynechá)
test> ?        znovu vypíše menu
```

V sekvenčním režimu se běh na selhaném testu zastaví a zeptá se, jestli pokračovat. Jednotlivé testy
jsou interaktivní: ptají se na parametry (`perioda [ms]`, `frekvence [Hz]`, … Enter vezme výchozí
hodnotu v hranatých závorkách) a na potvrzení toho, co jsi viděl. Výsledky se hlásí jako
`[ OK ]` / `[FAIL]` / `[WARN]` a na konci testu se sečtou.

Testy, které běží ve smyčce, se ukončí stiskem libovolné klávesy.

## Jednoúčelové binárky

Každý test má i vlastní prostředí, ve kterém není nic než ten jeden test a `main.cpp`:

```bash
pio run -e t00_chip_info -t upload && pio device monitor
```

Hodí se, když deska při některém testu tuhne, propadá napájení nebo se resetuje — v binárce pak není
nic jiného, co by to mohlo způsobit. Jednoúčelový build spustí test hned po bootu a po každém stisku
klávesy ho zopakuje.

## Build bez nahrávání

`menu` je `default_envs`, takže samotné `pio run` přeloží jen jeho. Kontrola, že se všechno pořád
překládá, vyžaduje vyjmenovat prostředí:

```bash
pio run -e menu -e t00_chip_info -e t01_blink_io38 -e t02_shift_register -e t03_step_pins \
        -e t04_endswitches -e t05_i2c_scan -e t06_tmc_uart -e t07_wiring_selftest \
        -e t08_motor_move -e t09_motor_jog
```

Překlad všech prostředí je jediná automatická kontrola v repu — žádná sada testů na hostiteli ani CI
neexistuje.

```bash
pio run -t clean     # smaže výstupy buildu
pio device list      # najde sériový port, když autodetekce selže
```

## Žebřík testů

Každý test předpokládá, že ty před ním prošly. Autoritativní seznam je `src/test_registry.cpp`.

| # | Test | Co ověřuje | Co potřebuje | Odladěno |
|---|---|---|---|---|
| 00 | `chip_info` | čip nabootoval, konzole USB CDC funguje, velikost flash / PSRAM / důvod resetu | jen USB | ✅ |
| 01 | `blink_io38` | GPIO výstup opravdu přepíná úroveň na pinu | externí LED + 330 R nebo multimetr na J4 | ✅ |
| 02 | `shift_register` | 74HC595 budí DIR a EN | multimetr/sonda na patky BOB | ❌ |
| 03 | `step_pins` | STEP pulsy na IO4…IO7 | osciloskop nebo logický analyzátor | ❌ |
| 04 | `endswitches` | IO13…IO16 vidí obě úrovně H i L | koncové spínače, ruka | ❌ |
| 05 | `i2c_scan` | TCA9548A na 0x70, scan enkodérů po kanálech | nic / enkodéry | ❌ |
| 06 | `tmc_uart` | všechny čtyři drivery odpovídají, `VERSION = 0x21`, adresace MS1/MS2 | osazené BOB-0…3 | ❌ |
| 07 | `wiring_selftest` | zapojení DIR/EN/STEP, **ověřené automaticky** | osazené BOB-0…3 | ❌ |
| 08 | `motor_move` | motor se skutečně otáčí | 24 V + motor, volná mechanika | ❌ |
| 09 | `motor_jog` | pustí motor a jede jednoduché pohyby zadané v krocích | 24 V + motor, volná mechanika | ❌ |

**T09 je ten, ve kterém se sedí nejvíc.** T08 odpoví na „otočí se to vůbec“; T09 nechá driver
nastavený a povolený a pohyby se zadávají z konzole, všechno v **krocích** (krok = jeden mikrokrok
při nastaveném rozlišení — při 1/16 je otáčka 200krokového motoru 3200 kroků). Výchozí je motor ve
**slotu 03**, ten zapojený na oživování. Na začátku se ptá na motor, `IRUN`, mikrokrok, počet kroků
na jeden pohyb a kroky za sekundu.

| klávesa | co udělá |
|---|---|
| `f` / `b` | ujede nastavený počet kroků, směr A / B |
| `F` / `B` | jeden krok, směr A / B (dokrokování) |
| `n` | jednorázově zadaný počet kroků a směr |
| `d` / `v` | změní počet kroků na pohyb / rychlost v krocích za sekundu |
| `+` / `-` | rychlost ±25 % |
| `t` | tam a zpět 3× — vůle a ztracené kroky |
| `c` / `C` | plynulá jízda směr A / B, dokud nestiskneš klávesu |
| `s` | rampa: rozjezd na 2× rychlost a dojezd |
| `a` / `u` | změní proud `IRUN` / mikrokrok |
| `m` | přepne motor (starý driver se nejdřív pustí) |
| `0` | vynuluje čítač polohy — „tady je start“ |
| `i` | stav driveru (`DRV_STATUS`, `IOIN`, `IFCNT`) a úroveň koncového spínače |
| `r` | konec a spuštění testu znovu od začátku (nové parametry) |
| `x` | konec — driver se zakáže a test se vyhodnotí |

Čítač polohy jde v krocích od startu (`+` = směr A) a vypisuje se po každém pohybu, takže symetrický
pohyb, který skončí mimo značku, je přímo počet ztracených kroků. Koncový spínač vybraného motoru
jízdu zastaví — ale jen když je v klidu v H; když na vstupu nic není, hlídání se vypne a jedinou
pojistkou je klávesa.

**T07 je ten, který se vyplatí pochopit.** Registr `IOIN` v TMC2209 čte zpátky úrovně vlastních
vstupních pinů DIR / ENN / STEP, takže firmware může nastavit úroveň přes posuvný registr nebo GPIO
a přečíst si ji zpátky po UART. Tím se uzavře smyčka přes celou signálovou cestu — pořadí bitů
v posuvném registru, studené spoje, prohozené STEP piny, špatná adresa driveru — bez sondy a se
stojícími motory. Místo dalšího testu typu „přilož sondu a koukej“ radši rozšiř T07.

## Bezpečnost

- `main.cpp` volá `tk::safe_state()` před každým testem i po něm: STEP piny v L, všechny drivery
  zakázané (`SR = 0xAA`).
- **EN je aktivní v L** (je to `ENN` SilentStepSticku), `0` driver povolí. `~OE` na 74HC595 je
  natvrdo na GND, takže registr budí výstupy hned po zapnutí ESP32 — proto se do něj jako první
  vždycky pošle známý bezpečný bajt.
- T02 postupně krátce povolí každý driver (držný moment, odběr proudu) a předem se zeptá. T03 a T07
  drží všechny drivery zakázané, takže se nic nepohne ani při připojených 24 V.
- **Motorem hýbe jen T08 a T09.** Oba se ptají na motor, počet kroků, rychlost a proud `IRUN`
  a nerozjedou se, dokud nepotvrdíš, že je mechanika volná; T09 navíc zastaví na koncovém spínači
  vybraného motoru a na stisk klávesy. Oba mají v `src/test_registry.cpp` příznak `moves_motor`
  a sekvenční režim (`a`) vynechává všechno, co ho nese.
- Na desce **není žádná softwarově řiditelná LED** (LED1 visí na PG výstupu LT8610 přes NPN). IO38
  je vyvedený jen na jednopinovou lištu J4.

## Pin mapa

`include/board_pins.h` je **jediná správná pin mapa** — odečtená z netlistu a znovu ověřená pad po
padu proti `PCB/esp32stepper.kicad_pcb`.

| Signál | GPIO |
|---|---|
| STEP0…3 | 4, 5, 6, 7 (přímo GPIO → BOB-0…3) |
| 74HC595 SRCLK / SER / RCLK | 8 / 9 / 10 |
| I2C SCL / SDA | 11 / 12 |
| Koncový spínač 0…3 | 13, 14, 15, 16 |
| TMC2209 UART TX / RX | 17 / 18 |
| Debug UART TX / RX (lišta J5) | 2 / 1 |
| Volný pin (lišta J4) | 38 |

Historicky s tímhle nesouhlasily dva další zdroje v repu a pro tuhle desku byly špatné: tabulka ve
`firmware/README.md` a staré MicroPython skripty `firmware/*.py`, psané podle odhadu schématu ještě
než deska existovala. **Ani jedno v repu k 2026-07-30 není** — `firmware/` obsahuje jen
`testing_firmware/` a `test_simple/`. Kdyby se někdy vrátily, čísla pinů z nich neopisuj.

Pořadí bitů DIR/EN v posuvném registru je **proložené, ne blokové**: při zápisu MSB-first
`bit0→QA=dir0, bit1→QB=en0, bit2→QC=dir1, bit3→QD=en1, …`. Používej `SR_BIT_DIR[]` / `SR_BIT_EN[]`,
nikdy ruční posuvy.

## Když to nejde

**Konzole zůstává prázdná.** Očekávané — stiskni Enter (viz výš). `HWCDC::operator bool()` hlásí jen
to, jestli je zapojený USB *kabel*, takže firmware připojení terminálu nedokáže detekovat.

**Deska se restartuje ve smyčce s `E cpu_start: Octal Flash option selected, but EFUSE not
configured!`** Oktální konfigurace (`memory_type = opi_opi`) běží na modulu s **quad** flash,
typicky na ESP32-S3-DevKitC-1 N8R2. Pro ten DevKit přepni na `dout` + `qio_qspi` + `8MB` +
`default_8MB.csv`.

**Deska se restartuje ve smyčce s `assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)`**
Zrcadlový případ toho výše — a tenhle README dřív tvrdil, že nastat nemůže: **quad** konfigurace na
**oktálním** cílovém modulu. ROM podle efuse zapne OPI (v boot logu se objeví `Octal Flash Mode
Enabled`) a aplikace přeložená pro quad pak neinicializuje flash a spadne. Použij commitnuté oktální
nastavení (`dout`, `opi_opi`, `32MB`, `default_16MB.csv`).

Žádný **quad/oktální společný jmenovatel neexistuje** — ty dvě desky opravdu potřebují jiné
nastavení. Skutečně nalezenou velikost flash vypíše T00.

**T00 hlásí, že nenašel PSRAM.** Je to `warn`, ne `fail` — žádný test v žebříku PSRAM nepotřebuje.
Na cílovém modulu N32R16V je 16 MB oktální PSRAM a `-DBOARD_HAS_PSRAM` je nastavené, takže tam tohle
varování znamená, že něco skutečně nehraje. Na DevKitu N8R2 s quad konfigurací je očekávané.

**T00 hlásí reset kvůli brownoutu.** Napájení nestačí nebo kolísá — sprav to, než pustíš cokoli, co
povoluje driver.

**Upload nenajde port.** `pio device list` ukáže, co v systému je. Download režim vynutíš tím, že
držíš `BOOT1` a zapojíš USB. (Reset/BOOT hardware desky není spolehlivě zdokumentovaný — BOM uvádí
jen `SW1`, který schéma kreslí jako napájecí SPDT přepínač, plus dvoupinovou lištu `J1`. Než se
spolehneš na konkrétní tlačítko, ověř si to ve schématu.)

**`Failed to communicate with the flash chip` / `Manufacturer: 00`, nebo zápis, který se ověří jako
prázdný.** Dvě různé závady na úrovni flash, obě opravitelné a obě popsané krok za krokem včetně
příkazů v [../report.md](../report.md): flash zaseknutá v OPI režimu (chce úplné odpojení napájení —
USB *i* barrel jack) a nastavené block-protect bity ve status registru (chce
`esptool write-flash-status`). Ani jedno neznamená, že je modul mrtvý.

**UART0 (IO43/IO44) není fyzicky zapojený.** Nikdy tam nesměruj logování — na téhle desce nevede
nikam.

## Přidání testu

Tři místa, všechna povinná:

1. `src/tests/tNN_name.cpp` s definicí `void tNN_name()` — bez parametrů, po dokončení se vrátí.
2. Řádek v `src/test_registry.cpp` (jméno, co ověřuje, co musí být připojené).
3. Deklarace v `src/test_registry.h` a sekce `[env:tNN_name]` v `platformio.ini`.

Konvence, které drží sadu použitelnou jako nástroj na oživování:

- Začni `tk::reset_results()` a `tk::banner()`, skonči `tk::summary()`.
- Hlas přes `tk::pass/fail/warn/info` — `pass`/`fail` plní počítadla, podle kterých se sekvenční
  režim rozhoduje, jestli zastavit.
- Na každé cestě z testu nech desku v bezpečném stavu; test, který povolí driver, ho musí sám
  zakázat.
- Smyčka musí volat `tk::key_pressed()`, aby se z ní operátor dostal.
- Blokující čekání musí použít vzor `wait_for_input()` z `main.cpp`, ne holé
  `while (!Serial.available())`.
- Společné hardwarové pomůcky (`ShiftReg`, `TmcBus`, konzole) patří do `lib/testkit/`, ne do testů.
- Komentáře i výpisy na konzoli jsou česky a jen ASCII (bez diakritiky) — některé terminály UTF-8
  přes USB CDC komolí.

## Struktura

```
platformio.ini          prostředí: menu (výchozí) + jedno na každý test
include/board_pins.h    autoritativní pin mapa, bitová mapa posuvného registru
lib/testkit/            konzole, počítadla výsledků, ShiftReg, TmcBus, safe_state
src/main.cpp            interaktivní menu, nebo jeden test přes -DTEST_ENTRY
src/test_registry.*     seznam, který vypisuje menu
src/tests/tNN_*.cpp     jeden test na soubor
```

Hardwarová fakta, která tenhle firmware svazují — varianta modulu, nastavení pamětí a postup
nahrávání — jsou v [../report.md](../report.md).
