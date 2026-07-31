// Generator kroku pro jeden motor: STEP pin + DIR/EN pres 74HC595.
//
// Krok se generuje softwarove ve smycce, nikoli hardwarovym timerem nebo RMT -
// pri par tisicich krocich za sekundu na to ESP32-S3 ma a kod zustava citelny.
// Cena je, ze move() je blokujici: dokud pohyb neskonci, firmware nic jineho
// nedela. To je pro jeden motor a pohyb tam a zpet v poradku; az bude potreba
// jet ctyrmi motory najednou nebo mezitim cist enkodery, tohle se musi prepsat
// na timer (nebo pouzit FastAccelStepper - prototyp je v ../FAS_firmware,
// prostredi fas_accel).
//
// Rampa je trapezoidni s konstantnim zrychlenim: rychlost roste podle
// v = sqrt(v_start^2 + 2*a*s), na cestovni rychlosti se drzi a symetricky
// dojede zpatky na v_start. Kdyz je pohyb kratky, obe rampy se protnou
// a na cestovni rychlost se vubec nedojede (trojuhelnikovy profil) - to resi
// ten samy vzorec sam, bez zvlastni vetve.

#pragma once

#include <Arduino.h>

#include "board_pins.h"

namespace sm {

// 74HC595 (U4): DIR + EN vsech ctyr driveru. Vystupy jsou zive od napajeni
// (~OE na GND), takze prvni zapis musi byt SR_ALL_DISABLED.
class ShiftReg {
 public:
  void begin();
  void write(uint8_t value);
  uint8_t value() const { return state_; }
  void set_dir(uint8_t motor, bool high);
  void set_enable(uint8_t motor, bool enabled);  // true = driver povolen (ENN L)

 private:
  uint8_t state_ = SR_ALL_DISABLED;
};

// Jak pohyb skoncil.
enum class MoveEnd : uint8_t {
  done,      // ujel vsechny kroky
  endstop,   // sepnul koncovy spinac
  keypress,  // prerusil operator klavesou z konzole
};

class Stepper {
 public:
  // Bezpecny stav (vsechny drivery zakazane, vsechny STEP v L) a piny motoru.
  void begin(uint8_t motor);

  // Prepocet z otacek na kroky. Vraci false, kdyz musela byt cestovni
  // rychlost snizena na strop generatoru (MAX_STEP_RATE).
  bool set_profile(uint32_t steps_per_rev, float speed_rpm, float accel_rps2,
                   float start_rpm);

  void enable(bool on);

  // + = smer A (DIR v L), - = smer B. Blokuje az do konce pohybu. Jizdu
  // prerusi koncovy spinac (kdyz je hlidani zapnute) nebo klavesa z konzole.
  MoveEnd move(int32_t steps);

  int32_t position() const { return pos_; }
  void zero() { pos_ = 0; }

  bool endstop_closed() const;
  // Hlidani se zapina az v bring-upu, po overeni, ze spinac je v klidu
  // rozepnuty - jinak by se firmware nikdy nerozjel.
  void set_endstop_guard(bool on) { guard_ = on; }

  // Statistika posledniho pohybu - kolik kroku se ujelo a jak dlouho to trvalo.
  uint32_t last_steps() const { return last_steps_; }
  uint32_t last_ms() const { return last_ms_; }
  // Skutecne dosazena stredni rychlost [kroku/s]; s rampou je nizsi nez
  // cestovni, takze se s ni neda srovnavat pri kratkych pohybech.
  uint32_t last_rate() const;

  float speed_steps_s() const { return v_max_; }
  float accel_steps_s2() const { return accel_; }
  // Kolik kroku zabere rozjezd na cestovni rychlost (0, kdyz uz na ni startuje).
  uint32_t ramp_steps() const;

 private:
  ShiftReg sr_;
  uint8_t motor_ = 0;
  bool guard_ = false;
  int32_t pos_ = 0;

  float v_max_ = 1000.0f;   // cestovni rychlost [kroku/s]
  float accel_ = 1000.0f;   // zrychleni [kroku/s^2]
  float v_start_ = 100.0f;  // rychlost prvniho kroku [kroku/s]

  uint32_t last_steps_ = 0;
  uint32_t last_ms_ = 0;
};

}  // namespace sm
