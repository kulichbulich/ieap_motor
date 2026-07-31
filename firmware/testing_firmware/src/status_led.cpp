#include <Arduino.h>
#include <Ticker.h>

#include "board_pins.h"
#include "status_led.h"

namespace {

constexpr uint32_t kPeriodMs = 300;

Ticker ticker;
bool paused = false;

void toggle() {
  static bool level = false;
  level = !level;
  digitalWrite(PIN_SPARE, level ? HIGH : LOW);
}

}  // namespace

void status_led_begin() {
  pinMode(PIN_SPARE, OUTPUT);
  digitalWrite(PIN_SPARE, LOW);
  ticker.attach_ms(kPeriodMs, toggle);
}

void status_led_pause() {
  if (paused) return;
  paused = true;
  ticker.detach();
  digitalWrite(PIN_SPARE, LOW);
}

void status_led_resume() {
  if (!paused) return;
  paused = false;
  ticker.attach_ms(kPeriodMs, toggle);
}
