# TMC2209: MS1/MS2 určují na této desce zároveň adresu i mikrokrokování
@doc_pwc: file:TMC2209_MS1_MS2.md | id:HW-00201 | created:2026-07-30 | rev:1 | revised:2026-07-30_1730 | type:REF

## Shrnutí

Piny `MS1` a `MS2` mají na TMC2209 **dvě funkce najednou**. Na desce esp32stepper jsou
natvrdo zadrátované pull-upy `R13..R16`, aby každá patice měla jinou UART adresu — a tím
je zároveň, jako vedlejší účinek, každé patici nastavené **jiné rozlišení mikrokroku**.

Aplikační firmware to musí přepsat, jinak budou čtyři osy jezdit ve čtyřech různých
měřítkách. Není to chyba návrhu: adresování se na jednodrátové UART sběrnici bez těchto
pinů udělat nedá. Je to past, která se neprojeví při komunikaci, ale až u pohybu.

## Tabulka

| patice | MS2 | MS1 | UART adresa | mikrokrok po zapnutí |
|---|---|---|---|---|
| BOB-0 | 0 | 0 | 0 | 1/8 |
| BOB-1 | 0 | 1 | 1 | 1/2 |
| BOB-2 | 1 | 0 | 2 | 1/4 |
| BOB-3 | 1 | 1 | 3 | 1/16 |

## Jak je to ověřené

Změřeno 2026-07-30 testem `t06_tmc_uart` na osazené patici BOB-3, ostatní tři patice
tehdy osazené nebyly. Řádek BOB-3 je tedy naměřený, zbytek tabulky je z datasheetu
TMC2209 (tabulka MS2/MS1 → microstep resolution), potvrzený tím, že naměřený řádek na ni
sedí.

```
IOIN=0x2100004D  ver=0x21  MS1/MS2 -> addr 3
GCONF        = 0x00000101
CHOPCONF     = 0x14010053
```

- `IOIN` bit 2 (`MS1`) = 1 a bit 3 (`MS2`) = 1 → adresa 3. Potvrzuje, že `R13..R16`
  fungují tak, jak popisuje `testing_firmware/include/board_pins.h`.
- `GCONF` bit 7 (`mstep_reg_select`) = **0** → rozlišení se bere z pinů, ne z registru.
- `CHOPCONF` bity 24–27 (`MRES`) = **4** → 1/16 mikrokroku. Výchozí hodnota po resetu je
  přitom `MRES = 0`, tedy 1/256. Rozdíl je právě vliv pinů.

## Co s tím musí udělat firmware

U **každého** driveru zvlášť, po navázání UART komunikace:

1. `GCONF` bit 7 `mstep_reg_select = 1` — převezme řízení mikrokroku z pinů do registru.
2. `CHOPCONF` `MRES` nastavit explicitně na požadovanou hodnotu, stejnou pro všechny osy.
3. `GCONF` bit 6 `pdn_disable = 1` — doporučeno pro UART provoz, aby pin `PDN_UART`
   nedělal automatické vypínání při stání. Po resetu je 0.

Adresu naopak přepisovat nelze ani netřeba — čte se z pinů průběžně a je správná.

## Související

- `firmware/testing_firmware/src/tests/t06_tmc_uart.cpp` — test, který tyto hodnoty vypisuje
- `firmware/testing_firmware/include/board_pins.h` — mapa pinů a `TMC_ADDR[]`
- `PCB/esp32stepper.kicad_sch` — `R13..R16`
- `firmware/report.md` (HW-00173), `firmware/FLASH.md` (HW-00174)
