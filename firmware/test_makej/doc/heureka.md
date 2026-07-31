# heureka.md - vznik test_makej a hledani, proc se motor nehybe

Zaznam konverzace, ve ktere vznikl projekt `test_makej` a resil se bug "motor
stoji na miste". Pro budouci referenci, kdyby se podobny problem opakoval
nebo kdyby bylo potreba pochopit, proc je kod takovy, jaky je.

## Jak se builduje testing_firmware / test_makej

`pio` neni v PATH, kdyz je PlatformIO nainstalovane jen jako VSCode extension -
zije v `~/.platformio/penv/bin/pio`. Bud pouzit plnou cestu, nebo si pridat do
`~/.bashrc`:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

Build (bez flashovani):

```bash
cd firmware/test_makej    # nebo firmware/testing_firmware
pio run
```

Flashovani NE pres `pio run -t upload` (esptool 4.5.1 v PlatformIO padá na
`BrokenPipeError`), ale pres `firmware/flash.sh -d firmware/test_makej` - viz
`../FLASH.md`.

## Vznik test_makej

Zalozeno na `testing_firmware` (konkretne na `t08_motor_move.cpp`), ale bez
interaktivnich promptu - parametry, na ktere se t08 normalne ptalo pres
`tk::read_int()`, jsou tady pevne dane konstanty:

- `MOTOR = 3`, `STEPS`, `STEP_RATE_HZ = 800`, `CURRENT_MA = 500`
- `SHIFT_REG_VALUE = SR_ALL_DISABLED & ~(1u << SR_BIT_EN[MOTOR])` - povoli
  driver zvoleneho motoru, ostatni zustanou zakazane, vsechny DIR v L.

Struktura: `include/board_pins.h` a `lib/testkit/` jsou kopie z
`testing_firmware` (samostatna kopie, ne sdileny kod - zmeny v jednom projektu
neovlivni druhy).

## Bug #1 - zadny pohyb vubec (chybely STEP pulzy)

Prvni verze `main.cpp` jen zapsala posuvny registr a vypsala konstanty na
konzoli - **nikdy nevygenerovala zadny pulz na STEP pin**. Motor logicky stal,
protoze ho nemel duvod se hnout. Doplneno: `step_burst()` (stejna logika jako
v t08) + inicializace TMC2209 pres UART (`GCONF`, `IHOLD_IRUN`,
`TPOWERDOWN`) - bez UART zapisu jede driver na resetovy proud, deska nema
VREF potenciometr, CS se nastavuje jen pres UART.

## Bug #2 - konzole nevidet (slepa ulicka s Debug UART0)

Zkousel jsem presmerovat konzoli z nativniho USB CDC na "Debug UART0"
(`Serial0`, piny podle `board_pins.h`). Nejdriv se predpokladalo IO1/IO2
podle puvodniho komentare v `board_pins.h`, pak podle uzivatelova popisu
schematu opraveno na IO43/IO44 (skutecny vychozi UART0 - `U0TXD`/`U0RXD`).
Ani po opravě pinu se ale aplikacni vypis neobjevil - videt byl porad jen ROM
boot log (`rst:0x15 (USB_UART_CHIP_RESET)`, `boot:0x8 ...`), ktery se
vypisuje vzdy pres nativni USB nezavisle na tom, co dela appka.

**Zavěr:** presmerovani na Debug UART0 zruseno, konzole vracena zpatky na
nativni USB CDC (`Serial`, presne jako v `testing_firmware`) - to je port,
ktery uzivatel uz prokazatelne pouziva pro flashovani a kde vidi ROM log, tedy
zadne dalsi zapojeni netreba. Pokud by se v budoucnu Debug UART0 chtelo znovu
zprovoznit, je potreba nejdriv overit multimetrem/logickym analyzatorem, ze
na IO43/44 skutecne nekdo (adapter) poslouchá - jinak neni jak overit, jestli
je chyba v pinech, nebo proste nikdo na druhe strane neni.

## Bug #3 - motor se hybe jen nepatrne (CHOPCONF.MRES)

Uzivatel prinesl referencni Python projekt
(`Temp/TMC2209_ESP32-main/test_script_03_basic_movement.py` a
`tmc/TMC_2209_StepperDriver.py`) jako znamy funkcni priklad inicializace
TMC2209. Srovnanim s nasim kodem:

- **Skutecna prezinat priciny "motor stoji":** `GCONF.mstep_reg_select=1` (uz
  jsme meli) rika driveru, at bere mikrokrokove rozliseni z `CHOPCONF`, ne z
  MS1/MS2 pinu. V resetu je ale `CHOPCONF.MRES = 0` = **256 mikrokroku na
  krok**, zatimco `step_burst()` generuje pulzy s predstavou "1 pulz = 1 cely
  krok". Vysledek: 400 pulzu na STEP = jen 400/256 ≈ 1,6 skutecneho kroku
  motoru - prakticky neznatelne. Oprava: cist-modifikovat-zapsat `CHOPCONF`,
  nastavit `MRES=8` (1 mikrokrok = 1 plny krok, odpovida tomu, jak generujeme
  pulzy) a `intpol=1` (interni vyhlazeni).

- **Co jsme z referencniho skriptu zamerne NEkopirovali:**
  - `setIScaleAnalog(True)` - prepne proudovou referenci na externi VREF pin,
    ktery nase deska nema zapojeny (proud se u nas ridi cistě pres UART
    `IHOLD_IRUN`, internal reference). Prevzeti by proud spis rozbilo.
  - `setVSense(True)` - meni referencni napeti pro vypocet CS; nas
    `tk::cs_from_ma()` pocita s `vsense=0` (overeno primo na desce merenim
    Rsense), takze by tenhle prepinac propocet proudu rozladil.

- **Bitova mapa 74HC595 (U4) potvrzena schematem** od uzivatele: QA→dir0,
  QB→en0, QC→dir1, QD→en1, QE→dir2, QF→en2, QG→dir3, QH→en3, `~OE` na GND,
  `~SRCLR` na 3V3 - presne odpovida `SR_BIT_DIR`/`SR_BIT_EN` v
  `board_pins.h`. Zadny bug v mapovani bitu.

Stejna MRES chyba (bod "Bug #3") je pravdepodobne i v originalnim
`testing_firmware/src/tests/t08_motor_move.cpp` - nebylo tam zatim opraveno,
cekalo se na potvrzeni od uzivatele.

## Otevrene body k dalsimu overeni

- Po oprave MRES: overit na desce, jestli se motor uz toci normalne a jestli
  odber odpovida nastavenemu proudu (drive hlaseno jako "maly odber, jako by
  byl odpojeny").
- Rozhodnout, jestli opravit stejnou MRES chybu i v puvodnim
  `t08_motor_move.cpp` (testing_firmware).
