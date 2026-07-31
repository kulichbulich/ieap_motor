# TMC2209: jak se na této desce nastavuje proud (IRUN/IHOLD, CS, mA)

@doc_pwc: file:TMC2209_CURRENT.md | id:HW-00179 | created:2026-07-31 | rev:1 | revised:2026-07-31_0923 | type:REF

## Shrnutí

Registr `IHOLD_IRUN` (0x10) nemá proud v miliampérech, ale v abstraktním kroku **CS**
(Current Scale, 0–31). Kolik miliampérů který CS opravdu znamená, závisí na dvou věcech,
které nejsou v registru vidět: snímacím rezistoru na desce driveru (`Rsense`) a jednom
bitu v jiném registru (`vsense` v `CHOPCONF`). Bez těchhle dvou čísel je "IRUN=8" jen
číslo bez jednotky — přesně to je ten "divný stupeň", který si všiml operátor testu T08.

`testing_firmware` (T08, T09) proto nezadává CS přímo, ale přepočítává ho z mA pomocí
`tk::cs_from_ma()` / `tk::ma_from_cs()` v `lib/testkit`.

## Vzorec

Datasheet TMC2209, kapitola o `IHOLD_IRUN`:

```
I_rms = (CS+1)/32 × V_fs / (R_sense + 0,02) × 1/√2
```

- **`CS`** — hodnota 0–31 zapsaná do registru. `+1`, protože CS=0 neznamená nulový
  proud, ale nejnižší nenulový krok.
- **`V_fs`** — plné napěťové rozpětí snímacího zesilovače. Řídí ho bit `vsense`
  v `CHOPCONF` (bit 17): `V_fs = 0,325 V` (vsense=0) nebo `0,180 V` (vsense=1, jemnější
  krok, nižší strop).
- **`R_sense + 0,02`** — snímací rezistor na desce driveru plus 0,02 Ω, což je datasheetem
  udávaný vnitřní odpor spodních spínačů H-můstku. Bez toho přičtení by přepočet byl u
  malých proudů systematicky vedle.
- **`1/√2`** — driver řídí špičkovou hodnotu proudu (sinusovka mikrokrokování), ale
  udávaná i měřená veličina je RMS.

## Rsense téhle desky: 0,11 Ω

Na desce jsou u driveru dva SMD rezistory (jeden na fázi A, jeden na fázi B) s potiskem
`R11`. To je běžný zápis SMD rezistorů pod 1 Ω: `R` nahrazuje desetinnou tečku, takže
`R11` = **0,11 Ω** (analogicky `1R1` by bylo 1,1 Ω, `11R0` by bylo 11 Ω). Ověřeno přímým
pohledem na desku 2026-07-31 — nejde o hodnotu opsanou z README, viz "Jak je to ověřené"
níže.

0,11 Ω je zároveň hodnota, kterou pro tyhle moduly (SilentStepStick / Trinamic
TMC2209-BOB) používá i `FAS_firmware` (`R_SENSE_OHM` v `fas_config.h`) — obě desky tedy
sedí na stejné číslo.

## Dva rozsahy podle `vsense`

Se snímacím rezistorem 0,11 Ω vychází CS 0–31 na tyhle proudy:

| CS | vsense=0 (V_fs=0,325 V) | vsense=1 (V_fs=0,180 V) |
|---|---|---|
| 0  | 55 mA   | 31 mA  |
| 8  | 497 mA  | 275 mA |
| 16 | 939 mA  | 520 mA |
| 24 | 1381 mA | 765 mA |
| 31 | 1768 mA | 979 mA |

- **vsense=0** — krok ~55 mA na jeden CS, strop 1768 mA RMS.
- **vsense=1** — krok ~31 mA na jeden CS (jemnější), ale strop jen 979 mA RMS.

Driver po resetu startuje na `vsense=0` a `testing_firmware` do `CHOPCONF` (kde ten bit
je) vůbec nesahá — proto T08/T09 počítají jen s rozsahem **vsense=0, 55–1768 mA**. Nižší
proud s jemnějším krokem by šel (jako ve `FAS_firmware`, který si vybírá rozsah podle
zadaného proudu automaticky), ale vyžaduje číst-upravit-zapsat `CHOPCONF` a hlídat pořadí
zápisů vůči `MRES` (viz `TMC2209_MS1_MS2.md`, HW-00201) — pro bring-up testovací žebřík to
zbytečně zvyšuje riziko na sdíleném kódu.

## IRUN vs. IHOLD

`IHOLD_IRUN` nese dvě hodnoty proudu, ne jednu:

- **IRUN** (bity 12:8) — proud, když se motor točí.
- **IHOLD** (bity 4:0) — proud, když motor stojí, ale driver je povolený (drží hřídel).
  Typicky zlomek IRUN (T08/T09 používají `IHOLD = IRUN/2`), aby motor v klidu zbytečně
  nehřál.
- **IHOLDDELAY** (bity 19:16) — kolik ~2^IHOLDDELAY taktů čeká po zastavení kroků, než
  proud sníží z IRUN na IHOLD.

Obě hodnoty se přepočítávají ze stejného CS rozsahu, takže platí stejný vzorec i stejná
tabulka výše — jen s jiným CS.

## Co dělá `testing_firmware`

`lib/testkit/testkit.h` + `testkit.cpp`:

```cpp
// Jen vsense=0 (V_fs=0,325 V) - CHOPCONF se nikde nemeni.
uint8_t cs_from_ma(uint16_t ma);   // mA -> CS, 0-31 (klepne na kraje rozsahu)
uint16_t ma_from_cs(uint8_t cs);   // CS -> skutecny mA RMS (po zaokrouhleni CS)
```

T08/T09 se ptají operátora na proud v mA (rozsah 55–1768), přepočítají na CS pomocí
`cs_from_ma()` a do logu vypíšou obojí — zadané mA i skutečně nastavené (`ma_from_cs()` po
zaokrouhlení CS), stejně jako to dělá `FAS_firmware`. Díky zaokrouhlení CS totiž zadaná a
skutečná hodnota většinou nejsou stejné — u kroku ~55 mA to může být rozdíl v jednotkách
procent.

## Jak je to ověřené

- **Rezistory**: přímý pohled na osazenou desku, 2026-07-31 — dva rezistory s potiskem
  `R11` u driveru, po jednom na fázi. Fyzický nález, ne převzatý údaj z dokumentace (viz
  poznámka o READMEs v paměti asistenta — repo dokumentace tady občas neodpovídá realitě).
- **Vzorec a konstanty** (`V_fs`, `+0,02`, `1/√2`): datasheet TMC2209, kapitola IHOLD_IRUN
  / Current settings. Stejné konstanty používá a cituje `FAS_firmware/src/tmc2209.cpp`.
- **Skutečný proud do fáze** (ampérmetrem, ne jen z rezistoru): zatím neověřeno — pokud by
  se vzorcem spočítaná hodnota lišila od naměřené, je to znamení, že `Rsense` na desce
  přece jen není 0,11 Ω (klon s jinou hodnotou), viz `HANDOFF_FAS_firmware.md`.

## Související

- `firmware/FAS_firmware/include/tmc2209.h`, `src/tmc2209.cpp` — stejný přepočet, oba
  rozsahy (`vsense=0` i `1`), plus zápis `vsense` bitu do `CHOPCONF`.
- `firmware/FAS_firmware/README.md` — stejná tabulka z pohledu aplikačního firmwaru.
- `firmware/TMC2209_MS1_MS2.md` (HW-00201) — proč se `CHOPCONF` musí zapisovat opatrně a
  ve správném pořadí vůči `GCONF`.
- `firmware/testing_firmware/lib/testkit/testkit.{h,cpp}` — `cs_from_ma()` /
  `ma_from_cs()`.
- `firmware/testing_firmware/src/tests/t08_motor_move.cpp`,
  `src/tests/t09_motor_jog.cpp` — kde se to používá.
