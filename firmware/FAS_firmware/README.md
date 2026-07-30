# FAS firmware — pohyb tam a zpět
@doc_pwc: file:README.md | id:HW-00176 | created:2026-07-30 | rev:1 | revised:2026-07-30_1410 | type:MAN

Nejjednodušší **aplikační** firmware desky `esp32stepper` (ESP32-S3-WROOM-2 N32R16V + 4× TMC2209
SilentStepStick). Po zapnutí nastaví jeden driver po UART, povolí ho a pak už jen pořád dokola jede
**jednu otáčku tam, pauza, jednu otáčku zpět, pauza**. Z konzole se nic neovládá — konzole jen
vypisuje, co se děje.

Tohle je první krok od testů k aplikaci: [../testing_firmware](../testing_firmware) se ptá na
parametry a čeká na člověka, tenhle firmware se rozjede sám po zapnutí, jako to bude dělat hotový
stroj.

## Co to potřebuje

| | |
|---|---|
| napájení | 24 V na barrel jacku (bez něj driver komunikuje, ale motor stojí a hlásí `uv_cp`) |
| motor | v konektoru **BOB-3** (čtvrtý slot; jiný slot = `MOTOR` v `include/fas_config.h`) |
| ověřeno předem | testy **T06** (UART), **T07** (zapojení) a **T08** (pohyb) v `../testing_firmware` |
| mechanika | volná dráha na obě strany, motor se rozjede **3 s po nabootování** |

## Build a nahrání

`pio run -t upload` na této desce nespolehlivě padá (esptool 4.5.1 v PlatformIO, `BrokenPipeError`),
proto skript s vlastním esptoolem — podrobně v [../FLASH.md](../FLASH.md):

```bash
cd firmware/FAS_firmware
../flash.sh -d .            # build + kontrola flash + zápis
../flash.sh -d . --check    # jen diagnostika, nic nezapisuje
pio device monitor -d .     # konzole (115200, USB CDC)
```

Prostředí je jediné (`fas`) a je v `default_envs`, takže `-e` není potřeba.

## Nastavení

Všechno je v [include/fas_config.h](include/fas_config.h) jako `constexpr` — změna znamená přeložit
a nahrát. Nejdůležitější hodnoty:

| konstanta | výchozí | co dělá |
|---|---|---|
| `MOTOR` | `3` | slot driveru 0–3; určuje i adresu na UART, STEP pin a koncový spínač |
| `MICROSTEPS` | `16` | mikrokrok; nastavuje se **po UART** (MS1/MS2 na desce drží adresu) |
| `RUN_CURRENT_MA` | `600` | efektivní (RMS) proud fáze při jízdě |
| `HOLD_CURRENT_PCT` | `50` | klidový proud v procentech z jízdního |
| `REVS_PER_MOVE` | `1.0` | jeden pohyb = jedna otáčka |
| `SPEED_RPM` | `60` | cestovní rychlost (60 rpm = otáčka za sekundu) |
| `ACCEL_RPS2` | `4.0` | zrychlení v otáčkách/s² (rozjezd na 60 rpm za 0,25 s) |
| `PAUSE_MS` | `500` | prodleva mezi pohyby |
| `START_DELAY_MS` | `3000` | odpočet po nabootování, než se motor rozjede |
| `WAIT_FOR_ENTER` | `false` | `true` = deska se sama nerozjede, čeká na klávesu |

Ladit proud, rychlost a mikrokrok **za běhu** se dá v `../testing_firmware`, test **T09** — tady se
mění jen v kódu.

### Proud se nastavuje v mA, driver ho zná jako CS

Registr `IHOLD_IRUN` má na proud jen 5 bitů, takže zadanou hodnotu nelze potrefit přesně:

```
I_rms = (CS+1)/32 × V_fs/(R_sense+0,02) × 1/√2      V_fs = 0,325 V (vsense=0) nebo 0,180 V (vsense=1)
```

Firmware si CS spočítá sám, a když se proud vejde do jemnějšího rozsahu (`vsense=1`), použije ho —
polovina chyby zaokrouhlení. Na konzoli vypíše obojí, zadané i skutečné:

```
proud    : zadano 600 mA -> nastaveno 612 mA RMS (IRUN=19, vsense=1, Rsense 0.11 ohm)
           klidovy 50 % = 306 mA (IHOLD=9, IHOLDDELAY=6, TPOWERDOWN=20)
```

Dvě věci, které z toho plynou:

* **`R_SENSE_OHM = 0.11` je jediné číslo, na kterém celý přepočet stojí.** 0,11 Ω má
  SilentStepStick TMC2209 i Trinamic TMC2209-BOB. Když je na modulu jiná hodnota, jsou všechny
  proudy o ten podíl vedle — ověřit ampérmetrem, ne z datasheetu klonu.
* Strop je **1768 mA RMS** (CS=31, `vsense=0`). Vyšší zadání se do něj utne, `SilentStepStick`
  bez chladiče na to ale stejně není.

## Výpis na konzoli

```
===================================================
  FAS firmware - pohyb tam a zpet
===================================================
driver odpovida: VERSION=0x21, ENN=1, PDN_UART=1
driver   : BOB-3, adresa 3 na UART sbernici (IO17/IO18), STEP=IO7, koncovak=IO16
mikrokrok: 1/16 z registru (MS1/MS2 tady drzi adresu), 3200 kroku na otacku
proud    : zadano 600 mA -> nastaveno 612 mA RMS (IRUN=19, vsense=1, Rsense 0.11 ohm)
pohyb    : 1.00 otacky = 3200 kroku, 60.0 rpm (3200 kroku/s), zrychleni 4.0 ot/s^2
           rampa 398 kroku, jeden pohyb ~1.2 s, pauza 500 ms
koncovy spinac IO16 je rozepnuty - pri sepnuti pohyb zastavi
motor se rozjede za 3 s - mechanika musi mit volnou drahu
driver povolen, hridel drzi klidovy proud. Tady je nula polohy - ...

cyklus 1: 1.00 otacky tam a zpet
  smer A: 3200 kroku za 1247 ms (~2565 kroku/s), poloha +3200
  smer B: 3200 kroku za 1246 ms (~2568 kroku/s), poloha +0
```

Naměřená rychlost je nižší než cestovní — je to průměr přes celý pohyb včetně obou ramp.

## Bezpečnost a zastavení

* **Bezpečný stav je první věc v `setup()`**, ještě před konzolí: `74HC595` má `~OE` na GND, takže
  jeho výstupy platí od přivedení napájení a do prvního zápisu jsou náhodné. Čekání na USB CDC trvá
  až 3 s — tak dlouho nesmí být povolený driver s náhodným DIR.
* **Jakákoli klávesa** z konzole jízdu přeruší (driver se zakáže, hřídel se pustí); další klávesa
  pokračuje. Počítadlo polohy zůstává, ale značka na hřídeli už na startu nebude.
* **Koncový spínač** daného motoru jízdu zastaví — ale jen když je v klidu rozepnutý (H). Když už
  při startu drží L (sepnutý nebo nezapojený bez pull-upu), hlídání se vypne a jedinou pojistkou je
  klávesa. Firmware to na konzoli hlásí.
* **`DRV_STATUS` se čte po každém pohybu.** Přehřátí a zkraty (`ot`, `s2g*`, `s2vs*`) firmware
  zastaví natrvalo (`HALT`, driver zakázán, chce reset desky). `otpw` (předzvěst přehřátí) a
  `ola`/`olb` (rozpojená fáze, hlásí je i zdravý stojící motor) jsou jen varování.
* Symetrický pohyb musí skončit na poloze **0**. Když počítadlo je na nule a značka na hřídeli ne,
  motor **ztrácí kroky** — malý proud, vysoká rychlost, tuhá mechanika.

## Struktura

```
platformio.ini          jedno prostředí "fas"; oktální flash konfigurace (viz níže)
include/board_pins.h    pin mapa desky — kopie z testing_firmware, držet v souladu
include/fas_config.h    všechna nastavení
include/tmc2209.h       driver na jednodrátové UART sběrnici + přepočet mA -> CS
include/stepper.h       generátor kroků, 74HC595 (DIR/EN), trapézová rampa
src/main.cpp            bring-up (ping, GSTAT, konfigurace, kontroly) a smyčka pohybů
```

Žádná externí knihovna (`lib_deps` je prázdné). TMC2209 se obsluhuje přímo — je to pět registrů
a v jednodrátovém režimu se stejně musí zahazovat vlastní echo, takže `TMCStepper` by tu nic
neušetřil.

### Konfigurace flash v `platformio.ini`

Modul je **N32R16V**: 32 MB oktální (OPI) flash + 16 MB oktální PSRAM, obojí na 1V8 (potvrzeno
z efuse). Konfigurace `memory_type = opi_opi` **není kosmetika** a platí na obě strany:

* quad konfigurace na tomto modulu **nenabootuje** — ROM podle efuse zapne OPI a aplikace spadne na
  `assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)` ve smyčce resetu,
* a binárku s `opi_opi` naopak nenos na DevKitC-1 N8R2 — tam skončí na
  `E cpu_start: Octal Flash option selected, but EFUSE not configured!`.

`flash_mode` je záměrně `dout`, ne `opi`: ani první, ani druhý bootloader OPI neumí, zavádí se
v DOUT a ROM režim nahradí podle efuse. Tři pasti, které stály hodiny, jsou v
[../report.md](../report.md).

## Známé hranice

* **`move()` blokuje.** Kroky se generují softwarovou smyčkou (`micros()` termíny), ne timerem ani
  RMT — na pár tisíc kroků/s to stačí a kód je čitelný, ale dokud pohyb neskončí, firmware nedělá
  nic jiného. Až bude potřeba jet víc motory najednou nebo mezitím číst enkodéry, musí se to
  přepsat na timer.
* Strop generátoru je **20 000 kroků/s**; `SPEED_RPM` nad tuto hranici firmware sníží a nahlásí.
* Jede **jeden motor**, ostatní tři zůstávají zakázané.
* Driver zůstává na výchozím **stealthChop** (tichý, ale nižší moment ve vyšších otáčkách) —
  `spreadCycle` ani `TPWMTHRS` se tu nenastavují.
* **Bez enkodéru.** Poloha je jen počítadlo vydaných kroků, ne měřená skutečnost. Enkodéry na
  desce jsou (I2C mux `TCA9548A`), tenhle firmware je nepoužívá.
