// FAS firmware - nejjednodussi aplikacni firmware desky esp32stepper.
//
// Co dela: nastavi jeden driver TMC2209 po UART (proud, mikrokrok), povoli ho
// a pak uz jen porad dokola jede jednu otacku tam, pauza, jednu otacku zpet,
// pauza. Nic se neovlada z konzole - konzole jen vypisuje, co se deje. Vsechna
// nastaveni jsou v include/fas_config.h.
//
// Proc je to postavene takhle:
//   - Je to prvni krok od testu (../testing_firmware) k aplikaci. Testy se
//     ptaji na parametry a cekaji na cloveka; tenhle firmware se rozjede sam
//     po zapnuti, jako to bude delat hotovy stroj.
//   - Pohyb je v otackach a rpm, ne v krocich - kroky si firmware spocita
//     z mikrokroku, ktery sam nastavil.
//   - Proud se nastavuje v mA (RMS na fazi) a firmware vypise, na jakou
//     hodnotu se to zaokrouhlilo. CS registr je jen 5 bitu, takze zadanou
//     hodnotu nepotrefi presne.
//
// Co to potrebuje: 24 V na barrel jacku, motor v konektoru BOB-3 (viz
// fas_config.h MOTOR), prosle testy T06/T07/T08 na te same desce.
//
// POZOR: motor se rozjede sam, 3 s po nabootovani. Mechanika musi mit volnou
// drahu. Jizdu prerusi jakakoli klavesa z konzole nebo koncovy spinac.

#include <Arduino.h>

#include <stdarg.h>

#include "board_pins.h"
#include "fas_config.h"
#include "stepper.h"
#include "tmc2209.h"

namespace {

fas::Tmc2209 drv;
fas::Stepper mot;

// Kolik kroku je jeden pohyb. Kroky, ne otacky, jsou to, cim se hybe driver.
constexpr int32_t MOVE_STEPS =
    static_cast<int32_t>(fas::REVS_PER_MOVE * fas::STEPS_PER_REV + 0.5f);

uint32_t cycles = 0;
bool halted = false;
const char* halt_reason = "";

void logln(const char* fmt, ...) {
  char buf[220];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  Serial.print("\r\n");
}

// USB CDC ma nemilou vlastnost: co se vypise driv, nez si host otevre port,
// se nenavratne ztrati. Proto to kratke cekani - a proto se dulezite udaje
// (nastaveni driveru) vypisuji jeste jednou u prvniho cyklu.
void console_begin() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {
    delay(10);
  }
  delay(200);
  Serial.println();
  Serial.println();
}

void banner() {
  logln("===================================================");
  logln("  FAS firmware - pohyb tam a zpet");
  logln("===================================================");
}

void halt(const char* why) {
  mot.enable(false);
  halted = true;
  halt_reason = why;
  logln("");
  logln("HALT: %s", why);
  logln("      driver zakazan, motor se dal nehybe. Oprav a resetuj desku.");
}

void print_move_plan() {
  const float move_s =
      static_cast<float>(MOVE_STEPS) / mot.speed_steps_s() +
      mot.speed_steps_s() / mot.accel_steps_s2();  // hruby odhad s rampou
  logln("pohyb    : %.2f otacky = %ld kroku, %.1f rpm (%.0f kroku/s),"
        " zrychleni %.1f ot/s^2",
        fas::REVS_PER_MOVE, static_cast<long>(MOVE_STEPS), fas::SPEED_RPM,
        mot.speed_steps_s(), fas::ACCEL_RPS2);
  logln("           rampa %lu kroku, jeden pohyb ~%.1f s, pauza %lu ms",
        static_cast<unsigned long>(mot.ramp_steps()), move_s,
        static_cast<unsigned long>(fas::PAUSE_MS));
  if (mot.ramp_steps() * 2 > static_cast<uint32_t>(MOVE_STEPS)) {
    logln("           pozn.: rampy se protnou, na cestovni rychlost se"
          " nedojede");
  }
}

void print_driver_config() {
  logln("driver   : BOB-%u, adresa %u na UART sbernici (IO%u/IO%u),"
        " STEP=IO%u, koncovak=IO%u",
        fas::MOTOR, drv.node(), PIN_TMC_TX, PIN_TMC_RX, PIN_STEP[fas::MOTOR],
        PIN_ENDSWITCH[fas::MOTOR]);
  logln("mikrokrok: 1/%u z registru (MS1/MS2 tady drzi adresu),"
        " %lu kroku na otacku",
        drv.microsteps(), static_cast<unsigned long>(fas::STEPS_PER_REV));
  logln("proud    : zadano %u mA -> nastaveno %u mA RMS (IRUN=%u, vsense=%u,"
        " Rsense %.2f ohm)",
        fas::RUN_CURRENT_MA, drv.run_ma_actual(), drv.irun(),
        drv.vsense() ? 1 : 0, fas::R_SENSE_OHM);
  logln("           klidovy %u %% = %u mA (IHOLD=%u, IHOLDDELAY=%u,"
        " TPOWERDOWN=%u)",
        fas::HOLD_CURRENT_PCT, drv.hold_ma_actual(), drv.ihold(),
        fas::IHOLDDELAY, fas::TPOWERDOWN);
}

// Vypise, co se da vycist z DRV_STATUS. Vraci false, kdyz je to chyba, na
// kterou se ma zastavit.
bool report_status(const fas::DrvStatus& st, bool verbose) {
  if (verbose) {
    logln("DRV_STATUS=0x%08lX  cs_actual=%u  stst=%u",
          static_cast<unsigned long>(st.raw), st.cs_actual, st.stst ? 1 : 0);
  }
  if (st.otpw) logln("[WARN] otpw - predzvest prehrati, snizit proud");
  if (st.ot) logln("[FAIL] ot - driver se vypnul prehratim");
  if (st.s2ga) logln("[FAIL] s2ga - zkrat faze A na GND");
  if (st.s2gb) logln("[FAIL] s2gb - zkrat faze B na GND");
  if (st.s2vsa) logln("[FAIL] s2vsa - zkrat faze A na VS");
  if (st.s2vsb) logln("[FAIL] s2vsb - zkrat faze B na VS");
  return !st.fault();
}

// Cela cesta od "deska nabootovala" k "driver je povoleny a smi se hybat".
// false = firmware se nesmi rozjet.
bool bring_up() {
  // mot.begin() uz probehl v setup() jeste pred konzoli - viz komentar tam.
  if (!mot.set_profile(fas::STEPS_PER_REV, fas::SPEED_RPM, fas::ACCEL_RPS2,
                       fas::START_RPM)) {
    logln("[WARN] %.1f rpm je nad stropem generatoru (%.0f kroku/s) -"
          " rychlost snizena",
          fas::SPEED_RPM, fas::MAX_STEP_RATE);
  }

  drv.begin(TMC_ADDR[fas::MOTOR]);

  // Prvni cteni po zapnuti obcas propadne (driver jeste nabiha), proto par
  // pokusu, nez se to prohlasi za mrtve.
  uint32_t ioin = 0;
  bool alive = false;
  for (uint8_t attempt = 0; attempt < 5 && !alive; ++attempt) {
    alive = drv.ping(&ioin);
    if (!alive) delay(50);
  }
  if (!alive) {
    logln("driver BOB-%u (adresa %u) neodpovida, nebo hlasi jinou adresu"
          " (IOIN=0x%08lX)",
          fas::MOTOR, drv.node(), static_cast<unsigned long>(ioin));
    logln("zkontroluj osazeni driveru, R19 na UART sbernici a pull-upy"
          " R13..R16; podrobnou diagnostiku dela test T06");
    halt("driver nekomunikuje po UART");
    return false;
  }
  logln("driver odpovida: VERSION=0x%02lX, ENN=%lu, PDN_UART=%lu",
        static_cast<unsigned long>((ioin >> 24) & 0xFF),
        static_cast<unsigned long>((ioin >> fas::IOIN_ENN) & 1),
        static_cast<unsigned long>((ioin >> fas::IOIN_PDN_UART) & 1));

  uint32_t gstat = 0;
  if (drv.read_and_clear_gstat(&gstat)) {
    if (gstat & 0x01) logln("GSTAT: reset - driver nabehl (ceka se po zapnuti)");
    if (gstat & 0x02) logln("[WARN] GSTAT: drv_err - driver se driv vypnul chybou");
    if (gstat & 0x04) {
      logln("[WARN] GSTAT: uv_cp - podpeti nabojove pumpy, nejspis chybi 24 V");
    }
  }

  if (!drv.configure(fas::RUN_CURRENT_MA, fas::HOLD_CURRENT_PCT,
                     fas::MICROSTEPS)) {
    logln("zapisy do driveru neprosly (IFCNT nebo CHOPCONF nesouhlasi) -"
          " sbernice UART je nespolehliva, %lu chyb cteni",
          static_cast<unsigned long>(drv.bus_errors()));
    halt("driver neprijal konfiguraci");
    return false;
  }
  print_driver_config();
  print_move_plan();

  fas::DrvStatus st;
  if (drv.read_status(&st) && !report_status(st, true)) {
    halt("driver hlasi chybu jeste pred rozjezdem");
    return false;
  }

  // Koncovy spinac ma v klidu pull-up, tedy H. Kdyz je uz pri startu v L, bud je
  // mechanika na koncaku, nebo spinac neni zapojeny - hlidat ho pak nejde,
  // protoze by se firmware nikdy nerozjel.
  if (fas::ENDSTOP_GUARD) {
    if (mot.endstop_closed()) {
      logln("[WARN] koncovy spinac IO%u je uz v klidu sepnuty (nebo neni"
            " zapojeny) - hlidani vypnuto, jizdu zastavi jen klavesa",
            PIN_ENDSWITCH[fas::MOTOR]);
      mot.set_endstop_guard(false);
    } else {
      mot.set_endstop_guard(true);
      logln("koncovy spinac IO%u je rozepnuty - pri sepnuti pohyb zastavi",
            PIN_ENDSWITCH[fas::MOTOR]);
    }
  } else {
    logln("[WARN] hlidani koncoveho spinace je vypnute v konfiguraci");
  }

  logln("");
  if (fas::START_DELAY_MS > 0) {
    logln("motor se rozjede za %lu s - mechanika musi mit volnou drahu",
          static_cast<unsigned long>((fas::START_DELAY_MS + 999) / 1000));
    for (uint32_t left = fas::START_DELAY_MS; left > 0;) {
      const uint32_t chunk = (left > 1000) ? 1000 : left;
      delay(chunk);
      left -= chunk;
    }
  }
  if (fas::WAIT_FOR_ENTER) {
    logln(">>> stisknij klavesu, tim se motor rozjede");
    while (!Serial.available()) {
      delay(10);
    }
  }
  while (Serial.available()) Serial.read();  // at hned nespadne stop klavesa

  mot.enable(true);
  mot.zero();
  logln("driver povolen, hridel drzi klidovy proud. Tady je nula polohy -"
        " udelej si znacku na hrideli.");
  logln("Jakakoli klavesa jizdu prerusi, dalsi ji pusti dal.");
  return true;
}

// Registry driveru jsou volatilni - zijou jen dokud ma driver VM. Po vypadku
// 24 V nabehne s vychozimi hodnotami: mikrokrok zase z pinu MS1/MS2 (viz
// firmware/TMC2209_MS1_MS2.md, HW-00201), IRUN na vychozich 23 a I_scale_analog
// zpatky na jednicce, tedy proud podle VREF. Nic z toho neni videt na
// DRV_STATUS - jediny zpusob, jak to firmware pozna, je GSTAT bit 0 (reset).
//
// Vraci false, kdyz se ma cyklus prerusit: po rekonfiguraci nebo pri haltu.
bool driver_survived() {
  uint32_t gstat = 0;
  if (!drv.read_and_clear_gstat(&gstat)) return true;  // vypadek sbernice resi volajici

  if (gstat & 0x04) {
    logln("[WARN] GSTAT: uv_cp - podpeti nabojove pumpy, nejspis vypadlo 24 V");
  }
  if ((gstat & 0x02) && !(gstat & 0x01)) {
    logln("[WARN] GSTAT: drv_err - driver se vypnul chybou");
  }
  if (!(gstat & 0x01)) return true;  // zadny reset, konfigurace plati dal

  logln("[WARN] driver se restartoval (GSTAT.reset) - konfigurace je pryc:");
  logln("       mikrokrok by se vratil na pinovy z MS1/MS2 a proud na vychozi.");

  // Rekonfigurovat se musi se zakazanym driverem: dokud registry drzi vychozi
  // hodnoty, tece do motoru proud, ktery jsme nezadali.
  mot.enable(false);
  if (!drv.configure(fas::RUN_CURRENT_MA, fas::HOLD_CURRENT_PCT,
                     fas::MICROSTEPS)) {
    logln("znovunastaveni driveru neproslo (%lu chyb cteni na sbernici)",
          static_cast<unsigned long>(drv.bus_errors()));
    halt("driver se restartoval a nelze ho znovu nastavit");
    return false;
  }
  print_driver_config();

  // Behem vypadku napajeni se hridel mohla protocit a citac kroku uz nerika
  // nic o tom, kde doopravdy je. Nulou se prizna, ze je to novy start.
  mot.zero();
  mot.enable(true);
  logln("driver znovu nastaven, citac polohy vynulovan - znacka na hrideli"
        " uz nesedi, jede se dal od tady");
  return false;
}

// Jeden pohyb. false = cyklus se ma prerusit (halt nebo pauza od operatora).
bool do_move(int32_t steps) {
  const char* dir = (steps >= 0) ? "A" : "B";
  const fas::MoveEnd end = mot.move(steps);

  logln("  smer %s: %lu kroku za %lu ms (~%lu kroku/s), poloha %+ld",
        dir, static_cast<unsigned long>(mot.last_steps()),
        static_cast<unsigned long>(mot.last_ms()),
        static_cast<unsigned long>(mot.last_rate()),
        static_cast<long>(mot.position()));

  // Jestli driver po celou dobu pohybu drzel nasi konfiguraci. Kontroluje se
  // po kazdem pohybu, ne raz za cyklus - jinak by druhy pohyb v cyklu jel
  // s vychozim proudem a mikrokrokem, nez by si toho firmware vsiml.
  if (!driver_survived()) return false;

  fas::DrvStatus st;
  if (!drv.read_status(&st)) {
    logln("[WARN] DRV_STATUS nelze precist (%lu chyb cteni celkem)",
          static_cast<unsigned long>(drv.bus_errors()));
  } else {
    if (!report_status(st, false)) {
      halt("driver hlasi chybu po jizde");
      return false;
    }
    if (st.cs_actual == 0) {
      logln("[WARN] cs_actual=0 - do motoru netece proud, zkontroluj 24 V");
    }
  }

  switch (end) {
    case fas::MoveEnd::done:
      return true;
    case fas::MoveEnd::endstop:
      logln("koncovy spinac sepnul - pohyb zastaven");
      halt("koncovy spinac");
      return false;
    case fas::MoveEnd::keypress:
      mot.enable(false);
      logln("preruseno klavesou. Driver zakazan, hridel je volna.");
      logln(">>> dalsi klavesa pokracuje (poloha zustava %+ld)",
            static_cast<long>(mot.position()));
      while (!Serial.available()) {
        delay(10);
      }
      while (Serial.available()) Serial.read();
      mot.enable(true);
      logln("pokracuje se");
      return false;
  }
  return false;
}

}  // namespace

void setup() {
  // Bezpecny stav driveru je prvni vec, jeste pred konzoli: 74HC595 ma ~OE na
  // GND, takze jeho vystupy plati od privedeni napajeni a do prvniho zapisu
  // jsou nahodne. Cekani na USB CDC trva az 3 s - tak dlouho nesmi byt
  // nahodne povoleny driver s nahodnym DIR.
  mot.begin(fas::MOTOR);

  console_begin();
  banner();
  if (!bring_up()) return;  // halt uz je nastaveny, loop() jen hlasi
}

void loop() {
  if (halted) {
    logln("HALT: %s - resetuj desku", halt_reason);
    delay(5000);
    return;
  }

  ++cycles;
  logln("");
  logln("cyklus %lu: %.2f otacky tam a zpet", static_cast<unsigned long>(cycles),
        fas::REVS_PER_MOVE);

  // Porovnava se proti zacatku cyklu, ne proti absolutni nule: kdyz nekdo
  // jeden pohyb prerusil klavesou, dalsi cykly zacinaji jinde a hlasit to
  // porad dokola by nemelo smysl.
  const int32_t start = mot.position();

  if (!do_move(MOVE_STEPS)) return;
  delay(fas::PAUSE_MS);
  if (!do_move(-MOVE_STEPS)) return;
  delay(fas::PAUSE_MS);

  // Kontrola invariantu: symetricky dojety cyklus musi skoncit tam, kde zacal.
  // Citac kroku se tak sam kontroluje - ale POZOR, hlida jen sebe. Kdyz je
  // citac na svem miste a znacka na hrideli ne, motor ztraci kroky (maly
  // proud, vysoka rychlost, tuha mechanika) a bez enkoderu to firmware nepozna.
  if (mot.position() != start) {
    logln("  [WARN] poloha po cyklu je %+ld misto %+ld",
          static_cast<long>(mot.position()), static_cast<long>(start));
  }
}
