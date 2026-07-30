# TMC2209: MS1/MS2 určují na této desce zároveň adresu i mikrokrokování
@doc_pwc: file:TMC2209_MS1_MS2.md | id:HW-00201 | created:2026-07-30 | rev:2 | revised:2026-07-30_1744 | type:REF

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

## Kód pro všechny čtyři drivery

Konfigurace jde po té samé jednodrátové UART sběrnici jako čtení (IO17 → `R19` →
sběrnice → IO18). Registry driveru jsou **volatilní** — žijí jen dokud má driver `VM`.
Po výpadku 24 V se vrátí na výchozí hodnoty a piny zase převezmou mikrokrokování, proto
je součástí kódu i hlídání `GSTAT` bitu 0.

```cpp
// MRES: 0=1/256, 1=1/128, 2=1/64, 3=1/32, 4=1/16, 5=1/8, 6=1/4, 7=1/2, 8=plny krok
static constexpr uint8_t MRES_WANTED = 4;  // 1/16 pro vsechny osy

// Nastavi jeden driver. Cti-uprav-zapis, ne zapis celeho slova - jinak se smazou
// ostatni bity (napr. I_scale_analog v GCONF, ktery driver ma po resetu nastaveny).
bool init_driver(tk::TmcBus& bus, uint8_t node, uint8_t mres) {
  uint32_t gconf = 0;
  if (!bus.read_reg(node, tk::TMC_GCONF, &gconf)) return false;
  gconf |= (1UL << 7);   // mstep_reg_select: MRES z registru, ne z pinu MS1/MS2
  gconf |= (1UL << 6);   // pdn_disable: pin PDN nedela power-down pri stani
  bus.write_reg(node, tk::TMC_GCONF, gconf);

  // POZOR na poradi: dokud je bit 7 nula, cte se MRES z pinu. Jakmile se nastavi,
  // zacne platit registrova hodnota - a ta je po resetu 0, tedy 1/256. Kdyby se
  // MRES nezapsal hned tady, driver by potichu preskocil na 1/256 a motor by se
  // na stejny pocet pulzu otocil 16x min.
  uint32_t chop = 0;
  if (!bus.read_reg(node, tk::TMC_CHOPCONF, &chop)) return false;
  chop = (chop & ~(0xFUL << 24)) | (static_cast<uint32_t>(mres) << 24);
  bus.write_reg(node, tk::TMC_CHOPCONF, chop);

  // Zapis se sam nijak nepotvrzuje - TmcBus::write_reg() vraci true vzdycky.
  // Bud se cte zpatky, jako tady, nebo se hlida IFCNT (0x02), ktery driver
  // inkrementuje po kazdem uspesnem zapisu.
  uint32_t back = 0;
  if (!bus.read_reg(node, tk::TMC_CHOPCONF, &back)) return false;
  return ((back >> 24) & 0xF) == mres;
}

// Vsechny ctyri osy najednou. Vraci pocet uspesne nastavenych driveru.
uint8_t init_all_drivers(tk::TmcBus& bus, uint8_t mres) {
  uint8_t ok = 0;
  for (uint8_t m = 0; m < 4; ++m) {
    if (init_driver(bus, TMC_ADDR[m], mres)) ++ok;
  }
  return ok;
}

// Vola se pravidelne za behu. Driver po vypadku VM zapomene konfiguraci a
// GSTAT bit 0 je jediny zpusob, jak to firmware pozna.
void recover_after_reset(tk::TmcBus& bus, uint8_t mres) {
  for (uint8_t m = 0; m < 4; ++m) {
    const uint8_t node = TMC_ADDR[m];
    uint32_t gstat = 0;
    if (!bus.read_reg(node, tk::TMC_GSTAT, &gstat)) continue;
    if (gstat & 0x01) {                          // reset - konfigurace je pryc
      init_driver(bus, node, mres);
      bus.write_reg(node, tk::TMC_GSTAT, 0x01);  // zachytny bit se maze zapisem 1
    }
  }
}
```

## Známá chyba, kterou to opravuje

`testing_firmware/src/tests/t08_motor_move.cpp` zapisuje

```cpp
bus.write_reg(node, tk::TMC_GCONF, (1UL << 6) | (1UL << 7));
```

Nastaví tedy `mstep_reg_select`, ale `MRES` v `CHOPCONF` už nezapíše — driver tím přeskočí
z 1/16 na 1/256. Navíc je to zápis celého slova, takže přemaže i `I_scale_analog` (bit 0).
Projeví se to jako „motor se skoro nehýbe" nebo „ztrácí kroky" a hledá se to v mechanice.

## Související

- `firmware/testing_firmware/src/tests/t06_tmc_uart.cpp` — test, který tyto hodnoty vypisuje
- `firmware/testing_firmware/include/board_pins.h` — mapa pinů a `TMC_ADDR[]`
- `PCB/esp32stepper.kicad_sch` — `R13..R16`
- `firmware/report.md` (HW-00173), `firmware/FLASH.md` (HW-00174)
