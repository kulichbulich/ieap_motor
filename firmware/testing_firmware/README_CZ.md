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
| **T02 … T08** | přeloží se, na hardwaru nikdy neběžely — logika PASS/FAIL je neověřená |

**Na nové desce nedoporučuji zkoušet T02 a výš.** Budí posuvný registr, STEP piny, TMC UART a (T08)
skutečný motor; na neoživené desce se stejně snadno může mýlit neověřený test jako deska, a T08
navíc rozhýbe mechaniku. Novou desku oživuj jen testy T00 a T01, na všechno ostatní se koukej jako
na kód, který si před prvním spuštěním zaslouží revizi.

Ty dva odladěné testy běžely na **ESP32-S3-DevKitC-1 N8R2**, ne na cílové desce.

---

## Co je potřeba

- [PlatformIO](https://platformio.org/) CLI (`pio`) — platformu `espressif32` si stáhne sám při
  prvním buildu
- kabel USB-C do konektoru **USB1** na desce (nativní USB CDC na IO19/IO20)
- 24 V na barrel jack až pro T06 (nepovinně) a T08 (povinně)

Všechny příkazy níže se spouští z této složky (`firmware/platformio/`).

## Rychlý start

```bash
pio run -e menu -t upload && pio device monitor
```

Nahraje binárku, která obsahuje všechny testy plus interaktivní menu, a otevře konzoli. To je běžný
režim — nahraje se jednou a testy se pak vybírají po USB, bez přeprogramování mezi nimi.

### Konzole je po připojení prázdná — tak to má být

Deska **nemá USB-UART převodník**, konzole je nativní USB CDC. Cokoli vypsaného dřív, než hostitel
otevře port, se nenávratně ztratí — čerstvě připojený terminál tak může zůstat prázdný i na
naprosto zdravé desce.

**Stiskni Enter.** Menu opakuje výzvu každé 2 s, dokud nedorazí první znak, takže do dvou sekund
uvidíš buď výzvu, nebo celé menu.

## Ovládání menu

```
test> 0        spustí jeden test podle čísla
test> a        spustí 00–07 za sebou (T08 vynechá, motory se nehýbou)
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
        -e t08_motor_move
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
- **Motorem hýbe jen T08.** Ptá se na motor, počet kroků, rychlost a proud `IRUN` a nerozjede se,
  dokud nepotvrdíš, že je mechanika volná. Sekvenční režim (`a`) ho vynechává.
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

Dva další zdroje v repu s tímhle nesouhlasí a **pro tuhle desku jsou špatné** — nikdy z nich
neopisuj čísla pinů: tabulka ve `firmware/README.md` a všechny `firmware/*.py` (staré MicroPython
skripty psané podle odhadu schématu, ještě než deska existovala).

Pořadí bitů DIR/EN v posuvném registru je **proložené, ne blokové**: při zápisu MSB-first
`bit0→QA=dir0, bit1→QB=en0, bit2→QC=dir1, bit3→QD=en1, …`. Používej `SR_BIT_DIR[]` / `SR_BIT_EN[]`,
nikdy ruční posuvy.

## Když to nejde

**Konzole zůstává prázdná.** Očekávané — stiskni Enter (viz výš). `HWCDC::operator bool()` hlásí jen
to, jestli je zapojený USB *kabel*, takže firmware připojení terminálu nedokáže detekovat.

**Deska se restartuje ve smyčce s `E cpu_start: Octal Flash option selected, but EFUSE not
configured!`** Někdo nastavil `flash_mode = opi` / `memory_type = opi_opi` na modulu s quad flash.
Vrať commitnuté quad nastavení. `platformio.ini` záměrně konfiguruje quad společný jmenovatel
(`dio`, `qio_qspi`, 8 MB, `default_8MB.csv`), protože BOM neuvádí paměťovou variantu modulu; takhle
to nabootuje na každé variantě S3 včetně modulu, který octal paměti opravdu má. Skutečně nalezenou
velikost flash vypíše T00.

**T00 hlásí, že nenašel PSRAM.** Je to `warn`, ne `fail` — žádný test v žebříku PSRAM nepotřebuje.
Znamená to, že modul není varianta R*, nebo má octal PSRAM (kterou quad konfigurace nevyužije).

**T00 hlásí reset kvůli brownoutu.** Napájení nestačí nebo kolísá — sprav to, než pustíš cokoli, co
povoluje driver.

**Upload nenajde port.** `pio device list` ukáže, co v systému je. Deska má tlačítko `BOOT1`, ale
žádné reset tlačítko; download režim vynutíš tím, že držíš `BOOT1` a zapojíš USB.

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

Hardwarová fakta, která tenhle firmware svazují, jsou v `../../CLAUDE.md`.
