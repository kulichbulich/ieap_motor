// T07 - automaticka kontrola zapojeni DIR / EN / STEP.
//
// Nejcennejsi test cele sady: overi kompletni cestu od GPIO az na patku
// driveru bez sondy a bez pohybu. Trik je v tom, ze TMC2209 umi pres registr
// IOIN precist urovne svych vlastnich vstupnich pinu:
//
//   ESP32 -> 74HC595 -> DIR  ->|
//   ESP32 -> 74HC595 -> ENN  ->| TMC2209.IOIN -> UART -> ESP32
//   ESP32 -----------> STEP  ->|
//
// Nastavime tedy uroven, precteme ji zpatky driverem a porovname. Odhali
// prohozene bity v posuvnem registru, studene spoje, zamenene STEP piny
// i spatne osazeny driver.
//
// STEP se pouze staticky prepina, drivery zustavaji zakazane -> zadny pohyb.
//
// Potrebuje: osazene drivery BOB-0..3, napajeni desky. Funkcni T06.

#include <Arduino.h>

#include "testkit.h"

namespace {

tk::ShiftReg sr;
tk::TmcBus bus;

// Precte IOIN s par pokusy - jednodratova linka obcas ztrati ramec.
bool ioin(uint8_t node, uint32_t* out) {
  for (uint8_t i = 0; i < 3; ++i) {
    if (bus.read_reg(node, tk::TMC_IOIN, out)) return true;
    delay(2);
  }
  return false;
}

// Nastavi uroven, precte IOIN a porovna jeden bit.
bool check_bit(uint8_t node, uint8_t bit, bool expected, const char* what) {
  delayMicroseconds(200);  // ustaleni po zapisu do registru / GPIO
  uint32_t v = 0;
  if (!ioin(node, &v)) {
    tk::fail("%s: driver neodpovedel", what);
    return false;
  }
  const bool actual = (v >> bit) & 1;
  if (actual == expected) {
    tk::pass("%s = %c", what, expected ? 'H' : 'L');
    return true;
  }
  tk::fail("%s: nastaveno %c, driver cte %c", what, expected ? 'H' : 'L',
           actual ? 'H' : 'L');
  return false;
}

}  // namespace

void t07_wiring_selftest() {
  tk::reset_results();
  tk::banner("T07 - automaticka kontrola zapojeni DIR/EN/STEP");

  sr.begin();
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  bus.begin(115200);

  tk::info("drivery zustanou zakazane pri testu DIR a STEP, motory se nehnou");

  uint8_t motors_ok = 0;

  for (uint8_t m = 0; m < 4; ++m) {
    const uint8_t node = TMC_ADDR[m];
    tk::section("motor");
    tk::info("BOB-%u, adresa %u, STEP na IO%u, SR bity dir=%u en=%u", m, node,
             PIN_STEP[m], SR_BIT_DIR[m], SR_BIT_EN[m]);

    uint32_t probe = 0;
    if (!ioin(node, &probe)) {
      tk::fail("BOB-%u neodpovida - spust nejdriv T06", m);
      continue;
    }

    bool all = true;
    char label[48];

    // --- DIR ---
    for (uint8_t lvl = 0; lvl < 2; ++lvl) {
      sr.set_dir(m, lvl != 0);
      snprintf(label, sizeof(label), "  DIR%u (SR bit %u)", m, SR_BIT_DIR[m]);
      all &= check_bit(node, tk::IOIN_DIR, lvl != 0, label);
    }
    sr.set_dir(m, false);

    // --- STEP ---
    for (uint8_t lvl = 0; lvl < 2; ++lvl) {
      digitalWrite(PIN_STEP[m], lvl ? HIGH : LOW);
      snprintf(label, sizeof(label), "  STEP%u (IO%u)", m, PIN_STEP[m]);
      all &= check_bit(node, tk::IOIN_STEP, lvl != 0, label);
    }
    digitalWrite(PIN_STEP[m], LOW);

    // --- ENN: povoleni driveru znamena ENN v L ---
    sr.set_enable(m, true);
    snprintf(label, sizeof(label), "  ENN%u povoleno (SR bit %u)", m,
             SR_BIT_EN[m]);
    all &= check_bit(node, tk::IOIN_ENN, false, label);

    sr.set_enable(m, false);
    snprintf(label, sizeof(label), "  ENN%u zakazano (SR bit %u)", m,
             SR_BIT_EN[m]);
    all &= check_bit(node, tk::IOIN_ENN, true, label);

    if (all) {
      ++motors_ok;
    } else {
      tk::info("  tip: prohozene DIR/EN bity znamenaji, ze mapa v");
      tk::info("  board_pins.h nesedi s osazenim U4");
    }
  }

  // --- krizova kontrola: budi kazdy STEP jen "svuj" driver? ---
  tk::section("krizova kontrola STEP -> spravny driver");
  for (uint8_t src = 0; src < 4; ++src) {
    digitalWrite(PIN_STEP[src], HIGH);
    delayMicroseconds(200);
    bool crosstalk = false;
    for (uint8_t m = 0; m < 4; ++m) {
      if (m == src) continue;
      uint32_t v = 0;
      if (!ioin(TMC_ADDR[m], &v)) continue;
      if ((v >> tk::IOIN_STEP) & 1) {
        tk::fail("STEP%u v H zvedl take STEP na BOB-%u - zkrat nebo zamena",
                 src, m);
        crosstalk = true;
      }
    }
    digitalWrite(PIN_STEP[src], LOW);
    if (!crosstalk) tk::pass("STEP%u budi jen BOB-%u", src, src);
  }

  sr.begin();
  tk::section("vyhodnoceni");
  if (motors_ok == 4) {
    tk::pass("zapojeni DIR/EN/STEP je u vsech ctyr driveru v poradku");
  } else {
    tk::fail("v poradku %u ze 4 driveru", motors_ok);
  }

  tk::summary();
}
