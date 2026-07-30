#include "stepper.h"

#include <math.h>

#include "fas_config.h"

namespace fas {

// --- 74HC595 ----------------------------------------------------------------

void ShiftReg::begin() {
  pinMode(PIN_SR_SER, OUTPUT);
  pinMode(PIN_SR_SRCLK, OUTPUT);
  pinMode(PIN_SR_RCLK, OUTPUT);
  digitalWrite(PIN_SR_RCLK, LOW);
  digitalWrite(PIN_SR_SRCLK, LOW);
  write(SR_ALL_DISABLED);
}

void ShiftReg::write(uint8_t value) {
  state_ = value;
  // MSB-first: bit7 jde dovnitr prvni a skonci na QH, bit0 zustane na QA.
  shiftOut(PIN_SR_SER, PIN_SR_SRCLK, MSBFIRST, value);
  digitalWrite(PIN_SR_RCLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_SR_RCLK, LOW);
}

void ShiftReg::set_dir(uint8_t motor, bool high) {
  const uint8_t bit = SR_BIT_DIR[motor];
  write(high ? (state_ | (1u << bit)) : (state_ & ~(1u << bit)));
}

void ShiftReg::set_enable(uint8_t motor, bool enabled) {
  const uint8_t bit = SR_BIT_EN[motor];  // ENN je aktivni v L
  write(enabled ? (state_ & ~(1u << bit)) : (state_ | (1u << bit)));
}

void ShiftReg::disable_all() { write(static_cast<uint8_t>(state_ | 0xAA)); }

// --- Stepper ----------------------------------------------------------------

void Stepper::begin(uint8_t motor) {
  motor_ = motor;

  // Nejdriv STEP piny do L, pak zakazat drivery. V opacnem poradi by povoleny
  // driver mohl stihnout krok na STEP pinu, ktery je jeste ve vzduchu.
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  sr_.begin();  // SR_ALL_DISABLED
  enabled_ = false;

  pinMode(PIN_ENDSWITCH[motor_], INPUT);  // 10k pull-up je na desce
  pos_ = 0;
}

bool Stepper::set_profile(uint32_t steps_per_rev, float speed_rpm,
                          float accel_rps2, float start_rpm) {
  const float steps = static_cast<float>(steps_per_rev);
  bool within_limit = true;

  v_max_ = speed_rpm / 60.0f * steps;
  if (v_max_ > MAX_STEP_RATE) {
    v_max_ = MAX_STEP_RATE;
    within_limit = false;
  }
  accel_ = accel_rps2 * steps;
  v_start_ = start_rpm / 60.0f * steps;

  // Rampa nesmi zacinat od nuly (perioda by byla nekonecna) ani nad cestovni
  // rychlosti (pak by "rampa" zpomalovala).
  if (v_start_ < 1.0f) v_start_ = 1.0f;
  if (v_start_ > v_max_) v_start_ = v_max_;
  if (accel_ < 1.0f) accel_ = 1.0f;

  return within_limit;
}

void Stepper::enable(bool on) {
  sr_.set_enable(motor_, on);
  enabled_ = on;
  delay(10);  // driver nabehne na proud, nez prijde prvni krok
}

bool Stepper::endstop_closed() const {
  return digitalRead(PIN_ENDSWITCH[motor_]) == LOW;
}

uint32_t Stepper::last_rate() const {
  if (last_ms_ == 0) return 0;
  return static_cast<uint32_t>(static_cast<uint64_t>(last_steps_) * 1000ULL /
                               last_ms_);
}

uint32_t Stepper::ramp_steps() const {
  // s = (v^2 - v0^2) / (2a)
  const float s = (v_max_ * v_max_ - v_start_ * v_start_) / (2.0f * accel_);
  return (s <= 0.0f) ? 0u : static_cast<uint32_t>(s);
}

MoveEnd Stepper::move(int32_t steps) {
  last_steps_ = 0;
  last_ms_ = 0;
  if (steps == 0) return MoveEnd::done;

  const bool dir_b = steps < 0;
  const uint32_t n = static_cast<uint32_t>(dir_b ? -steps : steps);

  if (guard_ && endstop_closed()) return MoveEnd::endstop;

  sr_.set_dir(motor_, dir_b);
  delayMicroseconds(DIR_SETUP_US);

  const uint8_t pin = PIN_STEP[motor_];
  const float v0sq = v_start_ * v_start_;
  const uint32_t t_start = millis();
  uint32_t deadline = micros();
  MoveEnd end = MoveEnd::done;

  for (uint32_t i = 0; i < n; ++i) {
    // Rychlost je minimum z rozjezdu, dojezdu a cestovni rychlosti. Pri
    // kratkem pohybu se obe rampy protnou v pulce a na v_max se nedojede.
    const float v_acc = sqrtf(v0sq + 2.0f * accel_ * static_cast<float>(i));
    const float v_dec =
        sqrtf(v0sq + 2.0f * accel_ * static_cast<float>(n - 1 - i));
    float v = (v_acc < v_dec) ? v_acc : v_dec;
    if (v > v_max_) v = v_max_;

    uint32_t period = static_cast<uint32_t>(1000000.0f / v);
    if (period <= STEP_PULSE_US) period = STEP_PULSE_US + 1;

    digitalWrite(pin, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    digitalWrite(pin, LOW);
    ++last_steps_;
    pos_ += dir_b ? -1 : 1;

    deadline += period;
    // Cekani na dalsi krok. Kdyz uz je zpozdeni vetsi nez cela perioda
    // (dlouhy vypis, preruseni), termin se srovna s pritomnosti - jinak by
    // se dohanel davkou kroku bez prodlevy, tedy skokem rychlosti.
    if (static_cast<int32_t>(micros() - deadline) > static_cast<int32_t>(period)) {
      deadline = micros();
    }
    while (static_cast<int32_t>(micros() - deadline) < 0) {
    }

    // Pojistky se hlidaji jednou za 16 kroku - pri 3200 krocich/s je to 5 ms,
    // rychleji nez staci clovek pustit klavesu, a nezdrzuje to smycku.
    if ((i & 0x0F) == 0x0F) {
      if (guard_ && endstop_closed()) {
        end = MoveEnd::endstop;
        break;
      }
      if (STOP_ON_KEYPRESS && Serial.available()) {
        while (Serial.available()) Serial.read();
        end = MoveEnd::keypress;
        break;
      }
    }
  }

  last_ms_ = millis() - t_start;
  return end;
}

}  // namespace fas
