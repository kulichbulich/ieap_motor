// T06 - komunikace s TMC2209 po jednodratove UART.
//
// Vsechny ctyri drivery visi na jednom vodici: IO17 vysila pres R19 1k na
// sbernici, IO18 ji cte. ESP32 tedy slysi i vlastni vysilani - echo se
// zahazuje v testkitu. Adresa driveru je dana pull-upy R13..R16 na MS1/MS2,
// ocekavame 0..3.
//
// POZOR na napajeni: 24 V NENI volitelne. Digitalni jadro TMC2209 se napaji
// z pinu VCC, ktery je na SilentStepSticku spojeny s 5VOUT - vystupem
// interniho regulatoru zivenym z VS, tedy z motoroveho napeti. VIO (+3V3)
// napaji jen urovne pinu. Bez 24 V je jadro vcetne UART periferie mrtve a
// driver mlci uplne stejne, jako by nebyl osazeny. (Driv tu stalo, ze
// registrove cteni jede uz z 3V3 logiky - to je vecne spatne.)
//
// Test se na zacatku pta, ktere patice jsou osazene, a neosazene preskoci -
// jinak by s jednim driverem hlasil FAIL vzdycky.
//
// Potrebuje: aspon jeden osazeny driver, napajeni desky vcetne 24 V.

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

  tk::info("bitmaska osazenych patic: 1=BOB-0, 2=BOB-1, 4=BOB-2, 8=BOB-3");
  tk::info("  jediny driver v BOB-2 => 4, vsechny ctyri => 15");
  const uint8_t populated =
      static_cast<uint8_t>(tk::read_int("osazene patice", 1, 15, 15));

  uint8_t n_populated = 0;
  int8_t first_slot = -1;
  for (uint8_t m = 0; m < 4; ++m) {
    if (!(populated & (1u << m))) continue;
    ++n_populated;
    if (first_slot < 0) first_slot = static_cast<int8_t>(m);
  }
  tk::info("testuje se %u patic z 4", n_populated);

  const int baud = tk::read_int("baud (1=115200, 2=19200, 3=9600)", 1, 3, 1);
  const uint32_t baudrate = (baud == 1) ? 115200 : (baud == 2) ? 19200 : 9600;
  bus.begin(baudrate);
  tk::info("TX=IO%u RX=IO%u, %lu Bd", PIN_TMC_TX, PIN_TMC_RX,
           static_cast<unsigned long>(baudrate));

  // Nejdriv se overi samotna TX/RX cesta. Vysilane bajty se pres R19 vraci na
  // IO18 bez ohledu na to, jestli je nejaky driver osazeny a napajeny, takze
  // chybejici echo ukazuje na desku, ne na driver. Bez tohohle kroku vypadaji
  // obe pricny v protokolu uplne stejne - jako "neodpovida".
  tk::section("A) TX/RX cesta (echo pres R19)");
  uint32_t probe = 0;
  bus.read_reg(TMC_ADDR[first_slot], tk::TMC_IOIN, &probe);
  const bool echo = bus.echo_seen();
  if (echo) {
    tk::pass("ESP32 slysi vlastni vysilani - IO%u -> R19 -> IO%u je v poradku",
             PIN_TMC_TX, PIN_TMC_RX);
  } else {
    tk::fail("ESP32 neslysi ani vlastni vysilani na IO%u", PIN_TMC_RX);
    tk::info("  chyba je na desce, ne v driveru. Zkontroluj R19 1k, spoj");
    tk::info("  IO%u/IO%u a jestli sbernici neco nedrzi natvrdo v L.",
             PIN_TMC_TX, PIN_TMC_RX);
  }

  uint8_t alive = 0;

  for (uint8_t m = 0; m < 4; ++m) {
    if (!(populated & (1u << m))) continue;
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

  // Linkova zkouska ma smysl jen proti osazene patici - proti prazdne by
  // merila vzdycky 0 % a nerekla nic o kvalite vodice.
  tk::section("spolehlivost linky");
  const uint8_t probe_addr = TMC_ADDR[first_slot];
  uint32_t ok = 0;
  const uint32_t tries = 200;
  for (uint32_t i = 0; i < tries; ++i) {
    uint32_t v;
    if (bus.read_reg(probe_addr, tk::TMC_IOIN, &v)) ++ok;
  }
  tk::info("200 cteni z adresy %u (BOB-%u): %lu uspesnych", probe_addr,
           first_slot, static_cast<unsigned long>(ok));
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
  if (alive == n_populated) {
    tk::pass("vsech %u osazenych driveru komunikuje", n_populated);
  } else {
    tk::fail("komunikuje %u z %u osazenych driveru", alive, n_populated);
  }

  // Nejcastejsi pricina "linka jede, ale nikdo neodpovida" je chybejici VM.
  if (alive == 0 && echo) {
    tk::info("");
    tk::info("Sbernice funguje, ale zadny driver se neozval. Nejcastejsi");
    tk::info("pricina je chybejici 24 V: jadro TMC2209 se napaji z 5VOUT,");
    tk::info("tedy z VM, nikoli z VIO. Bez motoroveho napajeni driver po");
    tk::info("UARTu neodpovi nikdy. Dalsi v poradi je spatna adresa z MS1/MS2");
    tk::info("a driver osazeny v jine patici, nez rika bitmaska.");
  }

  tk::summary();
}
