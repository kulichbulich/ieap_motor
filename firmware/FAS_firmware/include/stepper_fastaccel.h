// Prototyp generatoru kroku pro srovnani se stepper.h/.cpp: stejne DIR/EN pres
// 74HC595, ale STEP pulzy generuje knihovna FastAccelStepper (na ESP32 pres
// RMT) misto rucni smycky s delayMicroseconds. Verze knihovny je zamerne
// zamknuta v platformio.ini (gin66/FastAccelStepper@1.2.7 - cislo z
// PlatformIO registry / library.properties, NE z git tagu repozitare, ktery
// jde jinou radou verzi, napr. v0.34.0), ne "^" rozsah.
//
// DIR je zadrhel: FastAccelStepper pro zaporny pohyb bez DIR pinu vraci
// MOVE_ERR_NO_DIRECTION_PIN, a DIR pin normalne musi byt holy GPIO, ktery si
// knihovna sama prepina. Reseni je "externi" DIR pin (setDirectionPin s
// PIN_EXTERNAL_FLAG) - knihovna pri zmene smeru zavola nasi funkci
// set_dir_pin() a ceka, az potvrdime novou hodnotu. My v ni jen zapiseme bit
// do ShiftReg (stejna trida jako v stepper.h) a hned potvrdime - zapis do
// 74HC595 je synchronni (shiftOut + latch), zadna dalsi prodleva netreba.
//
// EN se do knihovny neregistruje (setEnablePin/setAutoEnable) - zustava rizeny
// primo z enable(), stejne jako u puvodni Stepper::enable().
//
// Callback pro externi pin je proste C-funkce (FastAccelStepperEngine ji
// neumi zavolat jako member), takze je staticka a pracuje pres jediny globalni
// ukazatel na aktivni instanci. Firmware pouziva jen jeden motor (fas_config.h
// MOTOR), takze je to v poradku - stejny predpoklad uz dela ShiftReg::state_
// v puvodni Stepper.

#pragma once

#include <Arduino.h>
#include <FastAccelStepper.h>

#include "board_pins.h"
#include "stepper.h"  // ShiftReg a MoveEnd - beze zmeny oproti puvodnimu generatoru

namespace fas {

class StepperFA {
 public:
  void begin(uint8_t motor);

  // start_rpm se prijima jen pro shodne rozhrani s puvodnim Stepperem, ale
  // FastAccelStepper zadnou "pocatecni rychlost" nezada - prvni krok po
  // rozjezdu z nuly si pocita sam ze zrychleni.
  bool set_profile(uint32_t steps_per_rev, float speed_rpm, float accel_rps2,
                   float start_rpm);

  void enable(bool on);
  bool enabled() const { return enabled_; }

  // + = smer A (DIR v L), - = smer B, stejne jako puvodni Stepper::move().
  // Blokuje az do konce pohybu (pollingem, ne busy-wait smyckou po kroku).
  MoveEnd move(int32_t steps);

  int32_t position() const;
  void zero();

  bool endstop_closed() const;
  void set_endstop_guard(bool on) { guard_ = on; }
  bool endstop_guard() const { return guard_; }

  uint32_t last_steps() const { return last_steps_; }
  uint32_t last_ms() const { return last_ms_; }
  uint32_t last_rate() const;

  float speed_steps_s() const { return v_max_; }
  float accel_steps_s2() const { return accel_; }
  // Stejny odhad jako Stepper::ramp_steps(), jen s v_start = 0 (viz set_profile).
  uint32_t ramp_steps() const;

 private:
  static bool set_dir_pin(uint8_t pin, uint8_t value);

  ShiftReg sr_;
  uint8_t motor_ = 0;
  bool enabled_ = false;
  bool guard_ = false;

  FastAccelStepperEngine engine_;
  FastAccelStepper* stepper_ = nullptr;

  float v_max_ = 1000.0f;
  float accel_ = 1000.0f;

  uint32_t last_steps_ = 0;
  uint32_t last_ms_ = 0;

  static StepperFA* active_;
};

}  // namespace fas
