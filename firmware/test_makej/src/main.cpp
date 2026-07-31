// test_makej - neinteraktivni odvozenina T08 (testing_firmware/src/tests/t08_motor_move.cpp).
//
// Zadne prompty za behu: parametry, na ktere se t08 ptala pres tk::read_int()
// (a bral jejich fallback, kdyz operator jen stiskl Enter), jsou tady pevne
// dane konstanty. Posuvny registr (74HC595, U4) se misto sekvence
// sr.set_dir()/sr.set_enable() zapise rovnou hodnotou SHIFT_REG_VALUE.

#include <Arduino.h>

#include "testkit.h"

namespace {

// Fallbacky z tk::read_int() volani v t08_motor_move.cpp.
constexpr uint8_t MOTOR = 3;            // "ktery motor" (0-3, vychozi 3)
constexpr uint32_t STEPS = 40000;         // "pocet kroku"
constexpr uint32_t STEP_RATE_HZ = 800;  // "kroku za sekundu"
constexpr uint16_t CURRENT_MA = 500;    // "proud pri jizde [mA RMS]"

// Stav posuvneho registru: vychozi SR_ALL_DISABLED (viz board_pins.h) se
// sklopenym bitem SR_BIT_EN[MOTOR] - povoli driver zvoleneho motoru (EN je
// ENN, aktivni v L), ostatni tri zustavaji zakazane, vsechny DIR v L.
constexpr uint8_t SHIFT_REG_VALUE =
    static_cast<uint8_t>(SR_ALL_DISABLED & ~(1u << SR_BIT_EN[MOTOR]));

tk::ShiftReg sr;
tk::TmcBus bus;

void step_burst(uint8_t motor, uint32_t steps, uint32_t period_us) {
  const uint32_t half = period_us / 2;
  for (uint32_t i = 0; i < steps; ++i) {
    digitalWrite(PIN_STEP[motor], HIGH);
    delayMicroseconds(half);
    digitalWrite(PIN_STEP[motor], LOW);
    delayMicroseconds(half);
  }
}

}  // namespace

void setup() {
  tk::console_begin();
  tk::safe_state();
  tk::reset_results();
  tk::banner("test_makej - pevne konstanty z T08");

  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  bus.begin(115200);

  sr.begin();
  sr.write(SHIFT_REG_VALUE);
  tk::info("posuvny registr <- 0x%02X (motor %u povolen, ostatni zakazane)",
           SHIFT_REG_VALUE, MOTOR);
  tk::info("konstanty: motor=%u steps=%lu rate=%lu Hz proud=%u mA", MOTOR,
           static_cast<unsigned long>(STEPS),
           static_cast<unsigned long>(STEP_RATE_HZ), CURRENT_MA);

  // Bez tohohle zapisu pres UART jede driver na vychozi (resetovy) proud -
  // deska nema VREF potenciometr jako v zapojeni bez UART, CS se nastavuje
  // jen takhle. pdn_disable=1 (UART ma prednost pred PDN pinem),
  // mstep_reg_select=1 (mikrokrok z registru, ne z MS1/MS2 - ty tu delaji
  // adresu driveru).
  const uint8_t node = TMC_ADDR[MOTOR];
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
    tk::pass("driver prijal zapisy, IFCNT=%lu",
             static_cast<unsigned long>(ifcnt));
  } else {
    tk::fail("IFCNT nelze precist - zapisy pravdepodobne neprosly");
  }

  const uint32_t period_us = 1000000UL / STEP_RATE_HZ;
  tk::info("jizda: motor %u, %lu kroku, %lu kr/s", MOTOR,
           static_cast<unsigned long>(STEPS),
           static_cast<unsigned long>(STEP_RATE_HZ));
  step_burst(MOTOR, STEPS, period_us);
  tk::info("hotovo");

  sr.write(SR_ALL_DISABLED);
  tk::summary();
}

void loop() {}
