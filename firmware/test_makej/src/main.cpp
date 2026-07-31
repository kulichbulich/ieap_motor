// test_makej - neinteraktivni odvozenina T08 (testing_firmware/src/tests/t08_motor_move.cpp).
//
// Zadne prompty za behu: parametry, na ktere se t08 ptala pres tk::read_int()
// (a bral jejich fallback, kdyz operator jen stiskl Enter), jsou tady pevne
// dane konstanty. Projede postupne vsechny 4 kanaly (0-3), jeden po druhem -
// posuvny registr se pro kazdy motor prepocita a zapise primo hodnotou, misto
// sekvence sr.set_dir()/sr.set_enable().

#include <Arduino.h>

#include "testkit.h"

namespace {

// Fallbacky z tk::read_int() volani v t08_motor_move.cpp - spolecne pro
// vsechny kanaly.
constexpr uint32_t STEPS = 1000;       // "pocet kroku"
constexpr uint32_t STEP_RATE_HZ = 800;  // "kroku za sekundu"
constexpr uint16_t CURRENT_MA = 500;    // "proud pri jizde [mA RMS]"

tk::ShiftReg sr;
tk::TmcBus bus;

// Stav posuvneho registru pro dany motor: vychozi SR_ALL_DISABLED (viz
// board_pins.h) se sklopenym bitem SR_BIT_EN[motor] - povoli driver tohoto
// motoru (EN je ENN, aktivni v L), ostatni tri zustavaji zakazane, vsechny
// DIR v L.
uint8_t shift_reg_value(uint8_t motor) {
  return static_cast<uint8_t>(SR_ALL_DISABLED & ~(1u << SR_BIT_EN[motor]));
}

void step_burst(uint8_t motor, uint32_t steps, uint32_t period_us) {
  const uint32_t half = period_us / 2;
  for (uint32_t i = 0; i < steps; ++i) {
    digitalWrite(PIN_STEP[motor], HIGH);
    delayMicroseconds(half);
    digitalWrite(PIN_STEP[motor], LOW);
    delayMicroseconds(half);
  }
}

// Stejne jako step_burst(), ale pulzuje vsechny 4 STEP piny soucasne v
// jednom cyklu - pro test, kdy maji jet vsechny motory najednou.
void step_burst_all(uint32_t steps, uint32_t period_us) {
  const uint32_t half = period_us / 2;
  for (uint32_t i = 0; i < steps; ++i) {
    for (uint8_t m = 0; m < 4; ++m) digitalWrite(PIN_STEP[m], HIGH);
    delayMicroseconds(half);
    for (uint8_t m = 0; m < 4; ++m) digitalWrite(PIN_STEP[m], LOW);
    delayMicroseconds(half);
  }
}

// Zapis GCONF/IHOLD_IRUN/TPOWERDOWN/CHOPCONF pro dany motor. Zustava
// nastavene i po sr.set_enable(false) - ENN jen vypina vystupni stupen,
// registry driveru zustavaji, dokud nedojde k UVLO/resetu.
void configure_tmc(uint8_t motor) {
  // Bez tohohle zapisu pres UART jede driver na vychozi (resetovy) proud -
  // deska nema VREF potenciometr jako v zapojeni bez UART, CS se nastavuje
  // jen takhle. pdn_disable=1 (UART ma prednost pred PDN pinem),
  // mstep_reg_select=1 (mikrokrok z registru, ne z MS1/MS2 - ty tu delaji
  // adresu driveru).
  const uint8_t node = TMC_ADDR[motor];
  const uint8_t irun = tk::cs_from_ma(CURRENT_MA);
  bus.write_reg(node, tk::TMC_GCONF, (1UL << 6) | (1UL << 7));
  const uint32_t ihold = static_cast<uint32_t>(irun) / 2;
  bus.write_reg(node, tk::TMC_IHOLD_IRUN,
                ihold | (static_cast<uint32_t>(irun) << 8) | (6UL << 16));
  bus.write_reg(node, tk::TMC_TPOWERDOWN, 20);

  // CHOPCONF.MRES je v resetu 0 (256 mikrokroku na krok). mstep_reg_select
  // uz je 1 (viz GCONF vyse), takze mikrokrok bere driver z tohohle registru,
  // ne z MS1/MS2 - a step_burst() pocita s PLNYMI kroky. Bez teto opravy je
  // 400 pulzu na STEP jen 400/256 = 1,6 skutecneho kroku motoru -
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
    tk::pass("motor %u: driver prijal zapisy, IFCNT=%lu", motor,
             static_cast<unsigned long>(ifcnt));
  } else {
    tk::fail("motor %u: IFCNT nelze precist - zapisy pravdepodobne neprosly",
             motor);
  }
}

void run_motor(uint8_t motor) {
  tk::section("motor");
  const uint8_t sr_value = shift_reg_value(motor);
  sr.write(sr_value);
  tk::info("posuvny registr <- 0x%02X (motor %u povolen, ostatni zakazane)",
           sr_value, motor);
  tk::info("konstanty: motor=%u steps=%lu rate=%lu Hz proud=%u mA", motor,
           static_cast<unsigned long>(STEPS),
           static_cast<unsigned long>(STEP_RATE_HZ), CURRENT_MA);

  configure_tmc(motor);

  const uint32_t period_us = 1000000UL / STEP_RATE_HZ;
  tk::info("jizda: motor %u, %lu kroku, %lu kr/s", motor,
           static_cast<unsigned long>(STEPS),
           static_cast<unsigned long>(STEP_RATE_HZ));
  step_burst(motor, STEPS, period_us);
  tk::info("hotovo");

  sr.write(SR_ALL_DISABLED);
}

// Posledni test: vsechny 4 motory najednou. sr_value=0x00 - vsechny EN bity
// v L (vsechny drivery povolene, ENN aktivni v L), vsechny DIR v L.
void run_all_motors() {
  tk::section("vsechny motory najednou");
  for (uint8_t motor = 0; motor < 4; ++motor) configure_tmc(motor);

  constexpr uint8_t kAllEnabled = 0x00;
  sr.write(kAllEnabled);
  tk::info("posuvny registr <- 0x%02X (vsechny 4 motory povoleny)",
           kAllEnabled);

  const uint32_t period_us = 1000000UL / STEP_RATE_HZ;
  tk::info("jizda: vsechny motory, %lu kroku, %lu kr/s",
           static_cast<unsigned long>(STEPS),
           static_cast<unsigned long>(STEP_RATE_HZ));
  step_burst_all(STEPS, period_us);
  tk::info("hotovo");

  sr.write(SR_ALL_DISABLED);
}

}  // namespace

void setup() {
  tk::console_begin();
  tk::safe_state();
  tk::reset_results();
  tk::banner("test_makej - pevne konstanty z T08, vsechny kanaly 0-3");

  while(1)
  {

  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  bus.begin(115200);
  sr.begin();

  for (uint8_t motor = 0; motor < 4; ++motor) {
    run_motor(motor);
  }
  run_all_motors();
  }

  tk::summary();
}

void loop() {}
