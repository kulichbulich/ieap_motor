// Vsechna nastaveni simple_move na jednom miste.
//
// Firmware nema zadne konzolove prikazy - jen vypisuje. Zmena chovani = uprava
// tohohle souboru, prelozit a nahrat. Ladit hodnoty za behu (proud, rychlost,
// mikrokrok, jiny motor) se da v ../testing_firmware, test T09.
//
// Zamerne tu nejsou zadne prepinace chovani (typu "cekat na Enter", "hlidat
// koncovak ano/ne"). Tenhle firmware dela jednu vec jednim zpusobem; kdo chce
// varianty, ma vedle ../FAS_firmware.

#pragma once

#include <stdint.h>

namespace sm {

// --- ktery motor -----------------------------------------------------------
// Slot driveru, 0..3 (BOB-0..3 na desce). Z toho vyplyva i adresa na UART
// sbernici (TMC_ADDR[MOTOR]), STEP pin (PIN_STEP[MOTOR]) a koncovy spinac
// (PIN_ENDSWITCH[MOTOR]). Vychozi 3 = konektor BOB-3, ctvrty slot - tam je
// zapojeny motor, na kterem se deska oziva (testy T08/T09).
static constexpr uint8_t MOTOR = 3;

// --- motor a mikrokrok -----------------------------------------------------
// Bezny krokovy motor 1.8 stupne = 200 plnych kroku na otacku.
static constexpr uint16_t FULL_STEPS_PER_REV = 200;
// Mikrokrok se nastavuje po UART (CHOPCONF.MRES), NE pinama MS1/MS2 - ty na
// teto desce delaji adresu driveru. Povolene hodnoty: 1, 2, 4, ... 256.
static constexpr uint16_t MICROSTEPS = 16;
// Kolik STEP impulsu je jedna otacka hridele.
static constexpr uint32_t STEPS_PER_REV =
    static_cast<uint32_t>(FULL_STEPS_PER_REV) * MICROSTEPS;

// --- proud (posila se po UART do IHOLD_IRUN) -------------------------------
// Efektivni (RMS) proud na fazi pri jizde, v mA. Firmware si z toho spocita
// CS hodnotu 0..31 a vypise, na jakou se to doopravdy zaokrouhlilo.
// 600 mA je konzervativni start pro NEMA17 - motor jde, ale nehreje.
static constexpr uint16_t RUN_CURRENT_MA = 600;
// Klidovy proud jako procento z RUN. 0 = hridel se pusti, 100 = plna drzici
// sila (a plne hrati). 50 % je bezny kompromis.
static constexpr uint8_t HOLD_CURRENT_PCT = 50;
// Jak rychle se po zastaveni sjede z IRUN na IHOLD (0..15, vetsi = pomaleji).
static constexpr uint8_t IHOLDDELAY = 6;
// Za jak dlouho po poslednim kroku se vubec zacne prepinat na IHOLD
// (jednotky ~0.021 s, 20 => ~0.4 s).
static constexpr uint8_t TPOWERDOWN = 20;

// Snimaci rezistor na modulu driveru. JEDINE cislo, na kterem stoji prepocet
// mA -> CS: 0.11 ohm ma SilentStepStick TMC2209 i Trinamic TMC2209-BOB.
// Kdyz je na modulu jina hodnota, vsechny proudy jsou o ten podil vedle.
static constexpr float R_SENSE_OHM = 0.11f;

// --- pohyb -----------------------------------------------------------------
// Kolik otacek je jeden pohyb. Firmware jede porad totez: tam, pauza, zpet,
// pauza, tam, ... Prvni pohyb je smer A (DIR v L).
static constexpr float REVS_PER_MOVE = 1.0f;
// Cestovni rychlost v otackach za minutu. 60 rpm = jedna otacka za sekundu.
static constexpr float SPEED_RPM = 60.0f;
// Zrychleni v otackach za sekundu na sekundu. Pri 60 rpm a 4 ot/s^2 se na
// plnou rychlost rozjede za 0.25 s (~ 1/8 otacky).
static constexpr float ACCEL_RPS2 = 4.0f;
// Rychlost prvniho a posledniho kroku. Rampa nesmi zacinat od nuly (perioda
// by byla nekonecna), ale prilis vysoka hodnota znamena skok na start.
static constexpr float START_RPM = 3.0f;
// Prodleva mezi jednotlivymi pohyby.
static constexpr uint32_t PAUSE_MS = 500;
// Odpocet po nabootovani, nez se motor prvne rozjede. Da se stihnout odpojit
// napajeni, kdyz je mechanika v nepovolenem stavu. 0 = rozjezd hned.
static constexpr uint32_t START_DELAY_MS = 3000;

// --- limity generatoru kroku ----------------------------------------------
// Nad 20 000 kroku/s uz rezie digitalWrite prevazi nad periodou a rychlost
// prestava odpovidat zadani. Strop plati i pro SPEED_RPM.
static constexpr float MAX_STEP_RATE = 20000.0f;
// Sirka STEP impulsu. TMC2209 potrebuje minimalne 100 ns, 3 us je s rezervou
// a pri 20 000 krocich/s (perioda 50 us) porad bez problemu.
static constexpr uint32_t STEP_PULSE_US = 3;
// Jak dlouho musi byt DIR stabilni, nez prijde hrana STEP. Datasheet chce
// 20 ns, tady se ceka na zapis do 74HC595, takze radeji hodne s rezervou.
static constexpr uint32_t DIR_SETUP_US = 200;

}  // namespace sm
