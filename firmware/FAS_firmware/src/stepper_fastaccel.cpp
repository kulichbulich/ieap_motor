#include "stepper_fastaccel.h"

#include "fas_config.h"

namespace fas {

StepperFA* StepperFA::active_ = nullptr;

// Zavola FastAccelStepper, kdyz chce prepnout externi DIR pin. `pin` je to,
// co jsme dali do setDirectionPin() (motor_ | PIN_EXTERNAL_FLAG), `value` je
// pozadovana uroven (0/1). Vracime hned potvrzenou hodnotu - ShiftReg::write()
// uz je hotovy zapis, ne asynchronni operace.
bool StepperFA::set_dir_pin(uint8_t pin, uint8_t value) {
  if (!active_) return value;
  const uint8_t motor = pin & ~PIN_EXTERNAL_FLAG;
  active_->sr_.set_dir(motor, value != 0);
  return value;
}

void StepperFA::begin(uint8_t motor) {
  motor_ = motor;

  // Stejne bezpecnostni poradi jako Stepper::begin(): STEP piny do L, pak
  // zakazat drivery pres 74HC595 - driv, nez cokoli jineho rozjede motor.
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  sr_.begin();  // SR_ALL_DISABLED
  enabled_ = false;

  active_ = this;
  engine_.init();
  engine_.setExternalCallForPin(&StepperFA::set_dir_pin);
  stepper_ = engine_.stepperConnectToPin(PIN_STEP[motor_]);
  if (stepper_) {
    // dirHighCountsUp = false, aby kladny pohyb (pozice nahoru) odpovidal
    // DIR v L = "smer A", stejne jako v puvodnim Stepperu.
    stepper_->setDirectionPin(static_cast<uint8_t>(motor_) | PIN_EXTERNAL_FLAG,
                               /*dirHighCountsUp=*/false);
  }
  // stepper_ == nullptr by znamenalo, ze RMT/MCPWM prostredku na desce uz
  // nejsou - pri jednom motoru by k tomu nemelo dojit.

  pinMode(PIN_ENDSWITCH[motor_], INPUT);
}

bool StepperFA::set_profile(uint32_t steps_per_rev, float speed_rpm,
                            float accel_rps2, float /*start_rpm*/) {
  const float steps = static_cast<float>(steps_per_rev);
  bool within_limit = true;

  v_max_ = speed_rpm / 60.0f * steps;
  if (v_max_ > MAX_STEP_RATE) {
    v_max_ = MAX_STEP_RATE;
    within_limit = false;
  }
  accel_ = accel_rps2 * steps;
  if (accel_ < 1.0f) accel_ = 1.0f;

  if (stepper_) {
    stepper_->setSpeedInHz(static_cast<uint32_t>(v_max_ + 0.5f));
    stepper_->setAcceleration(static_cast<int32_t>(accel_ + 0.5f));
  }
  return within_limit;
}

void StepperFA::enable(bool on) {
  sr_.set_enable(motor_, on);
  enabled_ = on;
  delay(10);  // driver nabehne na proud, nez prijde prvni krok
}

bool StepperFA::endstop_closed() const {
  return digitalRead(PIN_ENDSWITCH[motor_]) == LOW;
}

int32_t StepperFA::position() const {
  return stepper_ ? stepper_->getCurrentPosition() : 0;
}

void StepperFA::zero() {
  if (stepper_) stepper_->setCurrentPosition(0);
}

uint32_t StepperFA::last_rate() const {
  if (last_ms_ == 0) return 0;
  return static_cast<uint32_t>(static_cast<uint64_t>(last_steps_) * 1000ULL /
                               last_ms_);
}

uint32_t StepperFA::ramp_steps() const {
  // s = v_max^2 / (2a), tedy Stepper::ramp_steps() se start_rpm = 0 - presne
  // to, jak FastAccelStepper rozjezd z klidu pocita sam.
  const float s = (v_max_ * v_max_) / (2.0f * accel_);
  return (s <= 0.0f) ? 0u : static_cast<uint32_t>(s);
}

MoveEnd StepperFA::move(int32_t steps) {
  last_steps_ = 0;
  last_ms_ = 0;
  if (steps == 0 || !stepper_) return MoveEnd::done;

  if (guard_ && endstop_closed()) return MoveEnd::endstop;

  const int32_t p0 = stepper_->getCurrentPosition();
  const uint32_t t_start = millis();

  if (stepper_->move(steps) != MoveResultCode::OK) {
    // Rychlost/zrychleni jeste nebyly nastaveny (set_profile se nezavolal) -
    // DIR samo o sobe nemuze selhat, resi ho externi callback vyse.
    last_ms_ = millis() - t_start;
    return MoveEnd::done;
  }

  MoveEnd end = MoveEnd::done;
  bool stopping = false;
  while (stepper_->isRunning()) {
    if (!stopping) {
      if (guard_ && endstop_closed()) {
        stepper_->forceStop();
        end = MoveEnd::endstop;
        stopping = true;
      } else if (STOP_ON_KEYPRESS && Serial.available()) {
        while (Serial.available()) Serial.read();
        stepper_->forceStop();
        end = MoveEnd::keypress;
        stopping = true;
      }
    }
    // Bez guardu/klavesy se jen ceka, az stepper task (bezi sam na pozadi)
    // frontu dojede. Po forceStop() se pollu castejc, at se drain nezdrzuje.
    delay(stopping ? 1 : 5);
  }

  last_ms_ = millis() - t_start;
  const int32_t delta = stepper_->getCurrentPosition() - p0;
  last_steps_ = static_cast<uint32_t>(delta < 0 ? -delta : delta);
  return end;
}

}  // namespace fas
