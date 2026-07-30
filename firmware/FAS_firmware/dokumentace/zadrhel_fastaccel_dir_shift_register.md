# Zádrhel: FastAccelStepper a DIR přes shift registr (74HC595)

@doc_pwc: file:zadrhel_fastaccel_dir_shift_register.md | id:HW-00178 | created:2026-07-30 | rev:1 | revised:2026-07-30_1900 | type:NOTE

## Zadání

`Stepper::move()` v `stepper.cpp` je blokující busy-wait smyčka na jeden motor
(viz komentář v `stepper.cpp:4-7`) — bude to potřeba přepsat, až firmware
poběží se čtyřmi motory najednou nebo bude potřeba mezitím číst enkodéry.
FastAccelStepper je knihovna přesně na tuhle situaci: generuje kroky přes
RMT/hardware timer, je neblokující a umí víc motorů souběžně s vlastní
rampou.

## Zádrhel

DIR i EN na desce `esp32stepper` nejdou na holé GPIO, ale přes 74HC595
(`ShiftReg`). FastAccelStepper normálně čeká, že DIR pin je reálný GPIO,
který si sám přepíná.

**První hypotéza byla mylná:** zdálo se, že stačí knihovně DIR pin nedat
(`PIN_UNDEFINED`) a směr si řešit sám kolem `move()` — knihovna si prý i tak
polohu počítá správně, jen fyzicky nic nepřepíná. Ve skutečnosti ale
`FastAccelStepper::move()` bez nastaveného DIR pinu **odmítne záporný pohyb**
rovnou chybou `MOVE_ERR_NO_DIRECTION_PIN` (viz `FastAccelStepper.cpp`,
kontrola `(move < 0) && (_dirPin == PIN_UNDEFINED)`). Tahle cesta tedy nejde.

## Skutečné řešení: externí DIR pin

Knihovna má pro přesně tenhle případ (DIR/EN za externím HW jako shift
registr) vestavěný mechanismus - **externí pin callback**:

- `engine.setExternalCallForPin(bool (*func)(uint8_t pin, uint8_t value))`
  registruje jednu globální callback funkci na `FastAccelStepperEngine`.
- `stepper->setDirectionPin(id | PIN_EXTERNAL_FLAG, dirHighCountsUp)` řekne
  knihovně, že DIR pin `id` je "externí" - při změně směru nezavolá
  `digitalWrite`, ale tuhle callback funkci.
- Callback dostane požadovanou úroveň (0/1) a vrací potvrzenou hodnotu. Náš
  zápis do `ShiftReg` je synchronní (shiftOut + latch), takže potvrzujeme
  hned v jednom volání - žádné další čekání není potřeba.

Implementace: `include/stepper_fastaccel.h` + `src/stepper_fastaccel.cpp`
(`StepperFA::set_dir_pin`). EN se do knihovny neregistruje vůbec (žádný
`setEnablePin`/`setAutoEnable`) a zůstává řízen přímo z `enable()`, stejně
jako u původního `Stepper`.

Jediné pravidlo, které je potřeba dodržet: nikdy nereverzovat na běžícím
motoru (vždy čekat na `!stepper->isRunning()`, než se pošle pohyb opačným
směrem) - `StepperFA::move()` to zajišťuje tím, že je to blokující volání,
stejně jako `Stepper::move()`.

## Ověření

API (`setExternalCallForPin`, `PIN_EXTERNAL_FLAG`, `setDirectionPin`,
`MoveResultCode`) ověřeno čtením zdrojového kódu knihovny přímo na GitHubu
(`gin66/FastAccelStepper`), shodné mezi git tagem `v0.34.0` a `master`
(= PlatformIO registry verze `1.2.7`, viz níže). Mechanismus pro externí
direction pin byl v `1.1.0` interně přepracován (nový `ExtDirPendingState`,
automatická 2ms pauza při změně směru na externím pinu), ale veřejné API se
nezměnilo.

Prototyp přeložen (`pio run -e fas_accel`), ještě NEODZKOUŠENO na reálné
desce/motoru.

## Poznámka k verzi knihovny

`gin66/FastAccelStepper` má dvě nezávislá číslování verzí, která je snadné
zaměnit:

- git tagy na GitHubu: `v0.34.0`, `v0.33.14`, ...
- `library.properties` (to, co čte PlatformIO/Arduino Library registry):
  `1.2.7`

Pro `lib_deps` v `platformio.ini` je rozhodující to druhé -
`gin66/FastAccelStepper@1.2.7`, ne git tag.
