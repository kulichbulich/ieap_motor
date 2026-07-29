// T04 - koncove spinace na IO13..IO16.
//
// Kazdy vstup ma na desce 10k pull-up na +3V3 a 10R v serii, takze rozepnuty
// spinac = H, sepnuty = L. Interni pull-up se nezapina, testujeme i ten
// externi rezistor.
//
// Test bezi, dokud u vsech ctyr vstupu neuvidi obe urovne (nebo dokud
// nestisknes klavesu).
//
// Potrebuje: pripojene koncove spinace, rukou je postupne sepnout.

#include <Arduino.h>

#include "testkit.h"

void t04_endswitches() {
  tk::reset_results();
  tk::banner("T04 - koncove spinace");

  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_ENDSWITCH[i], INPUT);  // externi 10k pull-up na desce
  }
  delay(5);

  bool seen_high[4] = {false, false, false, false};
  bool seen_low[4] = {false, false, false, false};
  bool last[4];

  tk::section("klidovy stav");
  for (uint8_t i = 0; i < 4; ++i) {
    last[i] = digitalRead(PIN_ENDSWITCH[i]);
    tk::info("ENDSW%u (IO%u) = %s", i, PIN_ENDSWITCH[i], last[i] ? "H" : "L");
    if (last[i]) {
      seen_high[i] = true;
    } else {
      seen_low[i] = true;
      tk::warn("  v klidu L - spinac je sepnuty, nebo chybi pull-up");
    }
  }

  tk::section("postupne sepni kazdy spinac");
  tk::info("cekam na obe urovne u vsech ctyr vstupu, klavesa = konec");
  tk::flush_input();

  for (;;) {
    bool all_done = true;
    for (uint8_t i = 0; i < 4; ++i) {
      const bool v = digitalRead(PIN_ENDSWITCH[i]);
      if (v != last[i]) {
        last[i] = v;
        (v ? seen_high[i] : seen_low[i]) = true;
        tk::info("ENDSW%u -> %s", i, v ? "H (rozepnuto)" : "L (sepnuto)");
      }
      if (!(seen_high[i] && seen_low[i])) all_done = false;
    }
    if (all_done) {
      tk::info("vsechny vstupy videly obe urovne");
      break;
    }
    if (tk::key_pressed()) {
      tk::warn("preruseno operatorem");
      break;
    }
    delay(10);
  }

  tk::section("vyhodnoceni");
  for (uint8_t i = 0; i < 4; ++i) {
    if (seen_high[i] && seen_low[i]) {
      tk::pass("ENDSW%u (IO%u) prepina H<->L", i, PIN_ENDSWITCH[i]);
    } else if (seen_high[i]) {
      tk::fail("ENDSW%u (IO%u) zustal v H - spinac nesepnul, nebo neni pripojen",
               i, PIN_ENDSWITCH[i]);
    } else {
      tk::fail("ENDSW%u (IO%u) zustal v L - zkratovany vstup, nebo chybi pull-up",
               i, PIN_ENDSWITCH[i]);
    }
  }

  tk::summary();
}
