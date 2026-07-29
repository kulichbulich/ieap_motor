// T02 - posuvny registr 74HC595 (U4), signaly DIR a EN.
//
// U4 ridi DIR a EN vsech ctyr driveru. Bitova mapa je PROLOZENA:
//   bit0->QA=dir0  bit1->QB=en0  bit2->QC=dir1  bit3->QD=en1
//   bit4->QE=dir2  bit5->QF=en2  bit6->QG=dir3  bit7->QH=en3
// EN je ENN, tedy aktivni v L. ~OE je natvrdo na GND, takze U4 budi vystupy
// hned po zapnuti - bezpecny vychozi stav je 0xAA (vsechny drivery zakazane).
//
// Tenhle test je "slepy" - jen budi vystupy, operator je mericky overuje.
// Automatickou verifikaci stejnych cest dela T07 pres cteni IOIN z driveru.
//
// Potrebuje: multimetr / osciloskop na patkach BOB-0..3 (DIR, EN).

#include <Arduino.h>

#include "testkit.h"

namespace {

tk::ShiftReg sr;

void show(const char* what) {
  tk::info("%-28s SR=0x%02X", what, sr.value());
}

}  // namespace

void t02_shift_register() {
  tk::reset_results();
  tk::banner("T02 - posuvny registr 74HC595");

  sr.begin();
  show("bezpecny stav (vse zakazano)");
  if (sr.value() == SR_ALL_DISABLED) {
    tk::pass("registr inicializovan na 0x%02X", SR_ALL_DISABLED);
  } else {
    tk::fail("registr ma 0x%02X misto 0x%02X", sr.value(), SR_ALL_DISABLED);
  }

  tk::section("A) DIR - vsechny drivery zustavaji zakazane");
  tk::info("Mer napeti na patce DIR jednotlivych BOB-N.");
  for (uint8_t m = 0; m < 4; ++m) {
    sr.set_dir(m, true);
    show("");
    tk::info("  DIR%u ocekavane H (bit %u)", m, SR_BIT_DIR[m]);
    delay(1200);
    sr.set_dir(m, false);
    tk::info("  DIR%u ocekavane L", m);
    delay(1200);
  }
  if (tk::prompt_yes_no("Prepinaly se DIR0..DIR3 podle vypisu?")) {
    tk::pass("cesta SER/SRCLK/RCLK -> QA/QC/QE/QG (DIR) funguje");
  } else {
    tk::fail("DIR nesedi - zkontroluj U4, poradi bitu nebo pajeni BOB");
  }

  tk::section("B) EN - drivery se budou po jednom povolovat");
  tk::warn("Povoleny driver zacne drzet motor (odber proudu). Motory se");
  tk::warn("nerozjedou, STEP zustava v L.");
  if (tk::prompt_yes_no("Pokracovat testem EN?")) {
    for (uint8_t m = 0; m < 4; ++m) {
      sr.set_enable(m, true);
      show("");
      tk::info("  EN%u povolen -> ENN%u = L (bit %u)", m, m, SR_BIT_EN[m]);
      delay(1500);
      sr.set_enable(m, false);
      tk::info("  EN%u zakazan -> ENN%u = H", m, m);
      delay(500);
    }
    if (tk::prompt_yes_no("Prepinaly se ENN0..ENN3 podle vypisu?")) {
      tk::pass("cesta -> QB/QD/QF/QH (EN) funguje");
    } else {
      tk::fail("EN nesedi - zkontroluj U4 nebo pajeni BOB");
    }
  } else {
    tk::warn("test EN preskocen");
  }

  tk::section("C) beh pro osciloskop");
  tk::info("10 s: pres registr bezi 0x01..0x80 vzdy s vynucenymi EN v H.");
  const uint32_t t0 = millis();
  uint8_t bit = 0;
  while ((millis() - t0) < 10000 && !tk::key_pressed()) {
    sr.write(static_cast<uint8_t>((1u << bit) | SR_ALL_DISABLED));
    bit = static_cast<uint8_t>((bit + 1) & 7);
    delay(100);
  }

  sr.begin();
  show("konec, zpet do bezpecneho stavu");
  tk::summary();
}
