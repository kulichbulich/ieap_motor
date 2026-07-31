# simple_move — základní pohyb driveru BOB-3

Nejjednodušší firmware, který na desce `esp32stepper` (ESP32-S3-WROOM-2 N32R16V + 4× TMC2209
SilentStepStick) rozhýbe **jeden** motor. Po zapnutí nastaví driver po UART, povolí ho a pak už jen
pořád dokola jede **jednu otáčku tam, pauza, jednu otáčku zpět, pauza**. Z konzole se nic neovládá —
konzole jen vypisuje, co se děje.

Vznikl jako osekaná kopie [../FAS_firmware](../FAS_firmware). Rozdíly jsou schválně:

| | simple_move | ../FAS_firmware |
|---|---|---|
| generátor kroků | jen softwarová smyčka (`stepper.cpp`) | smyčka **i** FastAccelStepper (`env:fas_accel`) |
| přepínače chování | žádné (čekání na Enter, vypnutí koncováku…) | `WAIT_FOR_ENTER`, `ENDSTOP_GUARD`, `STOP_ON_KEYPRESS` |
| reset driveru za běhu (výpadek 24 V) | **HALT** a čeká na člověka | sám se znovu nastaví (`driver_survived()`) |

Kdo potřebuje varianty, má sáhnout po `FAS_firmware`. Kdo chce ladit proud, rychlost a mikrokrok
**za běhu**, patří do [../testing_firmware](../testing_firmware), test **T09**.

## Co to potřebuje

| | |
|---|---|
| napájení | 24 V na barrel jacku (bez něj driver komunikuje, ale motor stojí a hlásí `uv_cp`) |
| motor | v konektoru **BOB-3** (čtvrtý slot; jiný slot = `MOTOR` v `include/move_config.h`) |
| ověřeno předem | testy **T06** (UART), **T07** (zapojení) a **T08** (pohyb) v `../testing_firmware` |
| mechanika | volná dráha na obě strany, motor se rozjede **3 s po nabootování** |

## Build a nahrání

`pio run -t upload` na této desce nespolehlivě padá (esptool 4.5.1 v PlatformIO, `BrokenPipeError`),
proto skript s vlastním esptoolem — podrobně v [../FLASH.md](../FLASH.md):

```bash
cd firmware/simple_move
../flash.sh -d .            # build + kontrola flash + zápis
../flash.sh -d . --check    # jen diagnostika, nic nezapisuje
pio device monitor -d .     # konzole (115200, USB CDC)
```

Prostředí je jediné (`simple`) a je v `default_envs`, takže `-e` není potřeba.

## Nastavení

Všechno je v [include/move_config.h](include/move_config.h) jako `constexpr` — změna znamená přeložit
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

### Proud se zadává v mA, driver ho zná jako CS

Registr `IHOLD_IRUN` má na proud jen 5 bitů, takže zadanou hodnotu nelze potrefit přesně: firmware si
z mA spočítá `CS` (a případně přepne `vsense` na jemnější rozsah) a na konzoli vypíše **obojí** —
co bylo zadáno a co se doopravdy nastavilo. Výchozích 600 mA vyjde na `IRUN = 19, vsense = 1`,
tedy ~612 mA RMS.

Celý přepočet stojí na `R_SENSE_OHM = 0.11` (SilentStepStick / Trinamic TMC2209-BOB). Jiná hodnota
na modulu = všechny proudy o ten podíl vedle. **Ampérmetrem to ověřené není**, jen podle datasheetu —
stejně jako v `FAS_firmware`. Podrobně v [../TMC2209_CURRENT.md](../TMC2209_CURRENT.md).

### MS1/MS2 dělají adresu i mikrokrok

Na této desce drží pull-upy `R13`–`R16` na `MS1`/`MS2` **adresu driveru** na UART sběrnici. Tytéž
piny ale po resetu určují i mikrokrok, takže mikrokrok se musí přepnout na registrový:

1. nejdřív `CHOPCONF.MRES` (žádaný mikrokrok),
2. teprve pak `GCONF` bit 7 `mstep_reg_select`.

V obráceném pořadí by driver na okamžik jel s `MRES` z resetu (1/256). Pořadí v
[src/tmc2209.cpp](src/tmc2209.cpp) je proto záměrné — podrobně v
[../TMC2209_MS1_MS2.md](../TMC2209_MS1_MS2.md).

`GCONF` bit 0 (`I_scale_analog`) zůstává **0** schválně: s jedničkou by proud škáloval pin `VREF`,
který modul driveru na této desce nevyvádí, a přepočet mA → CS by nic neznamenal.

## Co firmware kontroluje

Rozjede se jen tehdy, když všechno projde; jinak jde do `HALT` (driver zakázán, čeká na reset desky):

- **driver odpovídá** po UART a hlásí `VERSION = 0x21` i tu adresu, na kterou se ptáme (pět pokusů,
  driver po zapnutí chvíli nabíhá),
- **konfigurace prošla** — kontroluje se přes `IFCNT` (počet přijatých zápisů) i čtením registrů zpět,
- **`DRV_STATUS` nehlásí chybu** (přehřátí, zkrat na GND/VS) — před rozjezdem i po každém pohybu;
  `otpw` (předzvěst přehřátí) a `cs_actual = 0` (neteče proud → chybí 24 V) jsou jen varování,
- **`GSTAT.reset` po každém pohybu** — když driver mezitím ztratil napájení, přišel o celou
  konfiguraci (mikrokrok zpátky pinový, proud výchozí) a firmware jde do `HALT`. Pokračovat by
  znamenalo jet s proudem, který nikdo nezadal.

Kromě toho po každém cyklu ověří, že se počítadlo kroků vrátilo na začátek. **Pozor:** hlídá tím jen
samo sebe — když motor ztrácí kroky (malý proud, vysoká rychlost, tuhá mechanika), počítadlo o tom
nic neví a bez enkodéru to firmware nepozná.

## Bezpečnost

- **74HC595 má `~OE` na GND**, takže jeho výstupy platí od přivedení napájení a do prvního zápisu
  jsou náhodné. Proto je `mot.begin()` (STEP piny do L, `SR = 0xAA`, tedy všechny drivery zakázané)
  úplně první věc v `setup()` — ještě před konzolí, na kterou se čeká až 3 s.
- **Koncový spínač** `IO16` (pro `MOTOR = 3`) jízdu zastaví. Když je při startu už sepnutý — nebo
  není zapojený — hlídání se samo vypne (jinak by se firmware nikdy nerozjel) a vypíše se `[WARN]`.
- **Jakákoli klávesa** z konzole jízdu přeruší a driver zakáže (hřídel je pak volná); další klávesa
  pokračuje. Kontroluje se každých 16 kroků, tedy při 3200 krocích/s asi každých 5 ms.
- Motor se rozjede **sám** 3 s po nabootování, i bez připojené konzole. `START_DELAY_MS` je jediná
  pojistka, kdy se dá odpojit napájení.

## Stav

**Přeloženo, na desku nenahráno.** `pio run -e simple` staví čistě (`-Wall`, 0 warningů,
RAM 6,0 %, Flash 293 977 B). Kód je kopie logiky z `FAS_firmware`, která na hardwaru také nikdy
neběžela — chování je odvozené z datasheetu TMC2209 a z pin mapy, ne z pozorování na desce.

Neověřeno: skutečný proud fáze ampérmetrem, chování při výpadku 24 V za jízdy, funkce koncového
spínače.

## Struktura

```
simple_move/
  platformio.ini          jedno prostředí "simple" (default_envs), oktální flash config
  include/board_pins.h    kopie z ../testing_firmware — držet v souladu, NENÍ symlink
  include/move_config.h   VŠECHNA nastavení jako constexpr
  include/tmc2209.h + src/tmc2209.cpp   driver po jednodrátové UART, přepočet mA -> CS
  include/stepper.h + src/stepper.cpp   74HC595 (DIR/EN) + generátor kroků s trapézovou rampou
  src/main.cpp            bring-up, kontroly, smyčka pohybů
```

Žádná externí knihovna — je to pět registrů a stejně by se muselo řešit zahazování vlastního echa na
jednodrátové sběrnici, takže `TMCStepper` by nic neušetřil.

`move()` **blokuje**: kroky se generují softwarovou smyčkou na `micros()`, ne timerem ani RMT. Dokud
pohyb neskončí, firmware nedělá nic jiného. Až bude potřeba víc motorů najednou nebo souběžné čtení
enkodérů, tohle se musí přepsat (nebo použít FastAccelStepper — prototyp je v
[../FAS_firmware](../FAS_firmware), prostředí `fas_accel`).
