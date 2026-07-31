// T08 - skutecny pohyb jednoho motoru.
//
// Posledni prvek zebriku: az sem se nic nehybalo. Nastavi konzervativni
// proud, povoli JEDEN driver, ujede zadany pocet kroku tam a zpatky a
// porovna DRV_STATUS pred a po.
//
// Potrebuje: 24 V na barrel jacku, pripojeny motor k BOB-N, funkcni T06/T07.
// POZOR: rotor se roztoci - mechanika musi mit volnou drahu.

#include <Arduino.h>

#include "testkit.h"

namespace {

tk::ShiftReg sr;
tk::TmcBus bus;

void step_burst(uint8_t motor, uint32_t steps, uint32_t period_us) {
  const uint32_t half = period_us / 2;
  for (uint32_t i = 0; i < steps; ++i) {
    digitalWrite(PIN_STEP[motor], HIGH);
    delayMicroseconds(half);
    digitalWrite(PIN_STEP[motor], LOW);
    delayMicroseconds(half);
    if ((i & 0xFF) == 0 && tk::key_pressed()) {
      tk::warn("preruseno po %lu krocich", static_cast<unsigned long>(i));
      return;
    }
  }
}

}  // namespace

void t08_motor_move() {
  tk::reset_results();
  tk::banner("T08 - pohyb motoru");

  sr.begin();
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  bus.begin(115200);

  // Vychozi je slot 03 (ctvrty motor) - na nem se testuje jako na prvnim.
  const int m = tk::read_int("ktery motor", 0, 3, 3);
  const int steps = tk::read_int("pocet kroku", 1, 100000, 400);
  const int rate = tk::read_int("kroku za sekundu", 10, 20000, 800);
  const int ma = tk::read_int("proud pri jizde [mA RMS]", 55, 1768, 500);
  const uint8_t irun = tk::cs_from_ma(static_cast<uint16_t>(ma));
  tk::info("%d mA -> CS=%u (skutecne %u mA RMS)", ma, irun,
           tk::ma_from_cs(irun));

  const uint8_t node = TMC_ADDR[m];
  const uint32_t period_us = 1000000UL / static_cast<uint32_t>(rate);

  tk::section("kontrola pred jizdou");
  uint32_t ioin = 0;
  if (!bus.read_reg(node, tk::TMC_IOIN, &ioin)) {
    tk::fail("BOB-%d (addr %u) neodpovida - spust T06", m, node);
    tk::summary();
    return;
  }
  tk::print_ioin(ioin);

  uint32_t st_before = 0;
  bus.read_reg(node, tk::TMC_DRV_STATUS, &st_before);
  tk::print_drv_status(st_before);
  if (st_before & (1u << 1)) {
    tk::fail("driver hlasi prehrati - nechej vychladnout");
    tk::summary();
    return;
  }

  tk::section("konfigurace");
  // pdn_disable=1 (UART ma prednost pred PDN pinem),
  // mstep_reg_select=1 (mikrokrok z registru, ne z MS1/MS2 - ty tu delaji adresu)
  bus.write_reg(node, tk::TMC_GCONF, (1UL << 6) | (1UL << 7));
  // IHOLD = IRUN/2, IHOLDDELAY = 6
  const uint32_t ihold = static_cast<uint32_t>(irun) / 2;
  bus.write_reg(node, tk::TMC_IHOLD_IRUN,
                ihold | (static_cast<uint32_t>(irun) << 8) | (6UL << 16));
  bus.write_reg(node, tk::TMC_TPOWERDOWN, 20);

  // CHOPCONF.MRES je v resetu 0 (256 mikrokroku na krok). mstep_reg_select
  // uz je 1 (viz GCONF vyse), takze mikrokrok bere driver z tohohle registru,
  // ne z MS1/MS2 - a step_burst() pocita s PLNYMI kroky. Bez teto opravy je
  // "steps" pulzu na STEP jen steps/256 skutecnych kroku motoru -
  // prakticky neznatelne "nehybe se". MRES=8 -> 1 mikrokrok = 1 plny krok;
  // intpol dovoluje driveru si to interne interpolovat na hladsi chod.
  uint32_t chopconf = 0;
  bus.read_reg(node, tk::TMC_CHOPCONF, &chopconf);
  chopconf &= ~(0xFUL << 24);  // MRES pryc
  chopconf |= (8UL << 24);     // MRES=8 -> plny krok
  chopconf |= (1UL << 28);     // intpol
  bus.write_reg(node, tk::TMC_CHOPCONF, chopconf);

  uint32_t ifcnt = 0;
  if (bus.read_reg(node, tk::TMC_IFCNT, &ifcnt)) {
    tk::pass("driver prijal zapisy, IFCNT=%lu",
             static_cast<unsigned long>(ifcnt));
  } else {
    tk::fail("IFCNT nelze precist - zapisy pravdepodobne neprosly");
  }

  tk::warn("motor %d se za chvili roztoci: %d kroku, %d kr/s, %d mA", m,
           steps, rate, ma);
  if (!tk::prompt_yes_no("Je mechanika volna?")) {
    tk::warn("zruseno operatorem");
    tk::summary();
    return;
  }

  tk::section("jizda");
  sr.set_enable(static_cast<uint8_t>(m), true);
  delay(10);

  for (uint8_t dir = 0; dir < 2; ++dir) {
    sr.set_dir(static_cast<uint8_t>(m), dir != 0);
    delayMicroseconds(200);
    tk::info("smer %s, %d kroku", dir ? "B" : "A", steps);
    const uint32_t t0 = millis();
    step_burst(static_cast<uint8_t>(m), static_cast<uint32_t>(steps),
               period_us);
    tk::info("  hotovo za %lu ms", static_cast<unsigned long>(millis() - t0));

    uint32_t st = 0;
    if (bus.read_reg(node, tk::TMC_DRV_STATUS, &st)) {
      tk::print_drv_status(st);
      const uint8_t cs = static_cast<uint8_t>((st >> 16) & 0x1F);
      if (cs == 0) {
        tk::warn("  cs_actual=0 - do motoru netece proud");
      }
    }
    delay(500);
  }

  sr.set_enable(static_cast<uint8_t>(m), false);
  sr.begin();

  tk::section("vyhodnoceni");
  uint32_t st_after = 0;
  bus.read_reg(node, tk::TMC_DRV_STATUS, &st_after);
  if (st_after & 0x3F) {
    tk::fail("DRV_STATUS hlasi chybu po jizde: 0x%08lX",
             static_cast<unsigned long>(st_after));
  } else {
    tk::pass("DRV_STATUS bez chyb po jizde");
  }

  if (tk::prompt_yes_no("Otacel se motor %d obema smery?", m)) {
    tk::pass("motor %d se toci", m);
  } else {
    tk::fail("motor %d se netoci - zkontroluj 24 V, faze A+/A-/B+/B- a proud",
             m);
  }

  tk::summary();
}
