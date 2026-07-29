// T06 - komunikace s TMC2209 po jednodratove UART.
//
// Vsechny ctyri drivery visi na jednom vodici: IO17 vysila pres R19 1k na
// sbernici, IO18 ji cte. ESP32 tedy slysi i vlastni vysilani - echo se
// zahazuje v testkitu. Adresa driveru je dana pull-upy R13..R16 na MS1/MS2,
// ocekavame 0..3.
//
// Registrove cteni funguje uz z 3V3 logiky; 24 V neni potreba, ale bez nej
// bude DRV_STATUS hlasit podpeti / rozpojene faze.
//
// Potrebuje: osazene drivery BOB-0..3, napajeni desky.

#include <Arduino.h>

#include "testkit.h"

namespace {

tk::TmcBus bus;

void dump_reg(uint8_t node, uint8_t reg, const char* name) {
  uint32_t v = 0;
  if (bus.read_reg(node, reg, &v)) {
    tk::info("%-12s = 0x%08lX", name, static_cast<unsigned long>(v));
  } else {
    tk::info("%-12s = <bez odpovedi>", name);
  }
}

}  // namespace

void t06_tmc_uart() {
  tk::reset_results();
  tk::banner("T06 - TMC2209 UART");

  tk::ShiftReg sr;
  sr.begin();
  tk::info("drivery zakazane (SR=0x%02X), jen ctem registry", sr.value());

  const int baud = tk::read_int("baud (1=115200, 2=19200, 3=9600)", 1, 3, 1);
  const uint32_t baudrate = (baud == 1) ? 115200 : (baud == 2) ? 19200 : 9600;
  bus.begin(baudrate);
  tk::info("TX=IO%u RX=IO%u, %lu Bd", PIN_TMC_TX, PIN_TMC_RX,
           static_cast<unsigned long>(baudrate));

  uint8_t alive = 0;

  for (uint8_t m = 0; m < 4; ++m) {
    const uint8_t node = TMC_ADDR[m];
    tk::section("driver");
    tk::info("BOB-%u, adresa %u", m, node);

    uint32_t ioin = 0;
    if (!bus.read_reg(node, tk::TMC_IOIN, &ioin)) {
      tk::fail("BOB-%u (addr %u) neodpovida", m, node);
      tk::info("  zkontroluj osazeni driveru, R19, a jestli MS1/MS2 davaji");
      tk::info("  opravdu adresu %u", node);
      continue;
    }

    const uint8_t version = static_cast<uint8_t>((ioin >> 24) & 0xFF);
    if (version == tk::TMC2209_VERSION) {
      tk::pass("BOB-%u (addr %u) odpovida, VERSION=0x%02X", m, node, version);
      ++alive;
    } else {
      tk::fail("BOB-%u (addr %u) odpovida, ale VERSION=0x%02X (ceka se 0x%02X)",
               m, node, version, tk::TMC2209_VERSION);
    }

    tk::print_ioin(ioin);

    const uint8_t addr_pins = static_cast<uint8_t>(
        (((ioin >> tk::IOIN_MS2) & 1) << 1) | ((ioin >> tk::IOIN_MS1) & 1));
    if (addr_pins == node) {
      tk::pass("  MS1/MS2 kodovani adresy sedi (%u)", addr_pins);
    } else {
      tk::fail("  MS1/MS2 hlasi adresu %u, odpovedel vsak na %u", addr_pins,
               node);
    }

    dump_reg(node, tk::TMC_GCONF, "GCONF");
    dump_reg(node, tk::TMC_CHOPCONF, "CHOPCONF");
    dump_reg(node, tk::TMC_IFCNT, "IFCNT");

    uint32_t gstat = 0;
    if (bus.read_reg(node, tk::TMC_GSTAT, &gstat)) {
      tk::info("GSTAT        = 0x%08lX", static_cast<unsigned long>(gstat));
      if (gstat & 0x01) tk::warn("  reset - driver byl restartovan");
      if (gstat & 0x02) tk::fail("  drv_err - driver se vypnul chybou");
      if (gstat & 0x04) tk::warn("  uv_cp - podpeti nabojove pumpy (chybi VM?)");
    }

    uint32_t st = 0;
    if (bus.read_reg(node, tk::TMC_DRV_STATUS, &st)) {
      tk::print_drv_status(st);
    }
  }

  tk::section("spolehlivost linky");
  uint32_t ok = 0;
  const uint32_t tries = 200;
  for (uint32_t i = 0; i < tries; ++i) {
    uint32_t v;
    if (bus.read_reg(TMC_ADDR[0], tk::TMC_IOIN, &v)) ++ok;
  }
  tk::info("200 cteni z adresy %u: %lu uspesnych", TMC_ADDR[0],
           static_cast<unsigned long>(ok));
  if (ok == tries) {
    tk::pass("linka bez chyb");
  } else if (ok * 100 / tries >= 95) {
    tk::warn("%lu%% uspesnost - linka funguje, ale ne cista",
             static_cast<unsigned long>(ok * 100 / tries));
  } else {
    tk::fail("jen %lu%% uspesnost - zkontroluj R19, delku vodice, baud",
             static_cast<unsigned long>(ok * 100 / tries));
  }

  tk::section("vyhodnoceni");
  if (alive == 4) {
    tk::pass("vsechny ctyri drivery komunikuji");
  } else {
    tk::fail("komunikuje %u ze 4 driveru", alive);
  }

  tk::summary();
}
