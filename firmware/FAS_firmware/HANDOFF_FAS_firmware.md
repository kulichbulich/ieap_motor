# Handoff: vznik FAS_firmware (aplikační firmware, pohyb tam a zpět)
@doc_pwc: file:HANDOFF_FAS_firmware.md | id:HW-00177 | created:2026-07-30 | rev:3 | revised:2026-07-30_1824 | type:NOTE

## Stav

`firmware/FAS_firmware/` je nový adresář, staví čistě (`pio run -e fas`, `-Wall`, 0 warningů,
RAM 6,0 %, Flash 4,5 %). **Na desku nebyl nahrán** — motor se po startu rozjíždí sám, nahrávání si
uživatel chtěl odzkoušet sám až později. Commit `f0f3294` obsahuje první verzi; poslední tři úpravy
(níže) jsou v pracovním stromu, **necommitnuté**.

## Co to je

Nejjednodušší aplikační firmware desky `ieap_motor_firmware` — na rozdíl od `../testing_firmware` (which
se ptá na parametry z konzole) se FAS po zapnutí rozjede sám: nastaví jeden driver TMC2209 po UART,
povolí ho a jede porád dokola *1 otáčka tam → pauza → 1 otáčka zpět → pauza*. Konzole jen vypisuje.

Kompletní popis, výchozí hodnoty a bezpečnostní chování je v
[README.md](README.md) — ten je aktuální, nekopíruj obsah sem znovu.

## Struktura

```
FAS_firmware/
  platformio.ini          jedno prostředí "fas" (default_envs), oktální flash config
  include/board_pins.h    kopie z testing_firmware — držet v souladu, NENÍ symlink
  include/fas_config.h    VŠECHNA nastavení jako constexpr (motor, proud, rychlost, rampa, ...)
  include/tmc2209.h + src/tmc2209.cpp   driver po jednodrátové UART, přepočet mA -> CS
  include/stepper.h + src/stepper.cpp   74HC595 (DIR/EN) + generátor kroků s trapézovou rampou
  src/main.cpp            bring-up, kontroly, smyčka pohybů
  README.md               HW-00176
```

Žádná externí knihovna (`TMCStepper` apod.) — je to pět registrů a stejně by se muselo řešit
zahazování vlastního echa na jednodrátové sběrnici, takže knihovna by nic neušetřila.

## Rozhodnutí, která by mohla překvapit při dalším čtení kódu

- **`move()` blokuje.** Kroky se generují softwarovou smyčkou (`micros()` termíny), ne timerem ani
  RMT. Dokud pohyb neskončí, firmware nedělá nic jiného. Až bude potřeba víc motorů najednou nebo
  souběžné čtení enkodérů, tohle se musí přepsat.
- **Proud se zadává v mA**, ne v CS. `fas_config.h::RUN_CURRENT_MA = 600` → firmware si sám spočítá
  `IRUN=19, vsense=1` → 612 mA RMS skutečně; obojí (zadané i skutečné) vypisuje na konzoli. Klíčová
  proměnná je `R_SENSE_OHM = 0.11` (SilentStepStick/Trinamic BOB) — jiná hodnota na modulu = všechny
  proudy o ten podíl vedle. Neověřeno ampérmetrem, jen podle datasheetu driveru.
- **`GCONF` bit 0 (`I_scale_analog`) je záměrně 0**, ne podle doporučení "čti-uprav-zapiš" z
  HW-00201 níže — s jedničkou by proud škáloval pin `VREF`, který na tomto modulu ani není vyvedený
  (ověřeno v `PCB/tmc2209_driver.kicad_sch`, 17 pinů, `VREF` mezi nimi chybí).
- **`CHOPCONF.MRES` se zapisuje před `GCONF` bitem 7** (`mstep_reg_select`) — opačné pořadí by na
  okamžik nechalo driver jet s `MRES` z resetu (1/256).
- Výchozí motor je slot **BOB-3** (`fas_config.h::MOTOR = 3`) — stejně jako v testech T08/T09, tam
  je fyzicky zapojený motor, na kterém se ověřovalo.

## Co bylo doplněno v tomto sezení (necommitnuto)

Uživatel našel nový dokument `firmware/TMC2209_MS1_MS2.md` (HW-00201, MS1/MS2 dělají na této desce
zároveň UART adresu i mikrokrok po resetu) a chtěl vědět, jestli je pokrytý. Odpověď: většina ano
(mstep_reg_select, explicitní MRES zápis, pdn_disable, ověření přes IFCNT i čtením zpět), ale dvě
věci chyběly a byly doplněny:

1. **Pořadí zápisů v `Tmc2209::configure()`** (`src/tmc2209.cpp`) — `CHOPCONF` teď jde před
   `GCONF`, z důvodu popsaného výše. Předtím to bylo obráceně; funkčně to nejspíš nevadilo (driver
   byl při konfiguraci vždy zakázaný), ale HW-00201 na tom pořadí trvá explicitně.
2. **Nová funkce `driver_survived()`** (`src/main.cpp`) — čte a čistí `GSTAT` po každém pohybu.
   Když `GSTAT.reset` (bit 0) naskočí, driver se restartoval (typicky výpadek 24 V) a ztratil celou
   konfiguraci — mikrokrok zpátky z pinů, proud na výchozí. Předtím se `GSTAT` četlo jen jednou při
   bring-upu a nikdy víc, takže firmware by po výpadku napájení jel dál s tichou domněnkou, že
   konfigurace platí. Teď: zakáže driver, zavolá `configure()` znovu, vynuluje počítadlo polohy
   (hřídel se mohla během výpadku protočit, počítadlo už nic neznamená) a pokud se to nepovede, jde
   do `HALT`.

README.md (`FAS_firmware/README.md`, teď rev 2) má novou sekci "MS1/MS2 dělají adresu i mikrokrok"
shrnující tohle rozhodnutí a odkaz na HW-00201.

**Ověřeno:** čistý build po obou úpravách (`pio run -e fas`, 0 warningů, Flash 294 185 B).
**Neověřeno na hardwaru:** chování `driver_survived()` při skutečném výpadku 24 V — logika je
odvozená z datasheetu (GSTAT bit 0 = reset), ne z pozorování na desce.

## Návrh na opravu v HW-00201 samotném (nehotovo, jen poznámka)

Při čtení dokumentu jsem si všiml jednoho možného nesouladu: HW-00201 doporučuje pro `GCONF`
obecně čti-uprav-zapiš, aby se nesmazal bit 0 (`I_scale_analog`). Pro tuhle desku je to ale opačně
žádoucí — `VREF` není vyvedený, takže bit 0 musí zůstat na 0, ne se zachovávat. Nechal jsem to bez
zásahu do HW-00201 (rev 2), protože to je čistě uživatelovo/dokumentační rozhodnutí, ne kód — jen
na to upozorňuji, kdyby při příštím čtení dokumentu vznikl dojem rozporu s `tmc2209.cpp`.

## Vedlejší poznatky z prozkoumávání repa (mimo scope FAS_firmware)

- Doc-ID rezervace/finalizace (`reserve_doc_id.sh`/`finalize_doc_id.sh`) je v
  `~/ws_ur_surobot/src/ur_surobot_ros2/ur_surobot_documentation/meta/` — používá se přes `flock`,
  registr je `doc_registry.md` tamtéž. Použito pro HW-00176 (rev 1→2) a HW-00177 (tento dokument).
- V `firmware/` vedle `FAS_firmware/` a `testing_firmware/` přibyl needited `blink_nonOctal/` —
  nesouvisí s touto prací, nekontrolováno.
- Git status na začátku sezení ukazoval necommitnuté změny v `testing_firmware/src/tests/
  t08_motor_move.cpp` a nový `t09_motor_jog.cpp` — ty vznikly před tímto sezením, FAS_firmware se
  jich nedotýká.

## Co zbývá (další kroky, žádný nezačat)

- Nahrát na desku a ověřit `driver_survived()` reálně (odpojit/připojit 24 V za běhu).
- Ověřit `R_SENSE_OHM = 0.11` ampérmetrem na skutečném proudu do fáze.
- Zvážit opravu HW-00201 (viz sekce výše) nebo aspoň komentář v dokumentu, že doporučení
  čti-uprav-zapiš pro bit 0 neplatí univerzálně, ale závisí na tom, jestli modul vyvádí `VREF`.
