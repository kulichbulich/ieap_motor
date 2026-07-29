// T03 - STEP piny (IO4..IO7 primo do BOB-0..3).
//
// Generuje merítelne pulsni vlaky. Drivery zustavaji po celou dobu zakazane
// (SR = 0xAA), takze se nic nehne ani kdyz je pripojeno 24 V a motory.
//
// Potrebuje: osciloskop nebo logicky analyzator na patce STEP na BOB-0..3.
//            Bez sondy je test bezcenny - pro automatickou verifikaci STEP
//            cesty pouzij T07.

#include <Arduino.h>

#include "testkit.h"

namespace {

void pulse_train(uint8_t pin, uint32_t freq_hz, uint32_t duration_ms) {
  const uint32_t half_us = 500000UL / freq_hz;
  const uint32_t t0 = millis();
  while ((millis() - t0) < duration_ms) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(half_us);
    digitalWrite(pin, LOW);
    delayMicroseconds(half_us);
  }
}

}  // namespace

void t03_step_pins() {
  tk::reset_results();
  tk::banner("T03 - STEP piny");

  tk::ShiftReg sr;
  sr.begin();
  tk::info("drivery zakazane (SR=0x%02X), motory se nepohnou", sr.value());

  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }

  const int freq = tk::read_int("frekvence [Hz]", 10, 20000, 1000);

  tk::section("A) staticke urovne");
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(PIN_STEP[i], HIGH);
    tk::info("STEP%u (IO%u) = H", i, PIN_STEP[i]);
    delay(800);
    digitalWrite(PIN_STEP[i], LOW);
    tk::info("STEP%u (IO%u) = L", i, PIN_STEP[i]);
    delay(300);
  }

  tk::section("B) pulsni vlak po jednom");
  for (uint8_t i = 0; i < 4; ++i) {
    tk::info("STEP%u (IO%u): %d Hz po dobu 3 s", i, PIN_STEP[i], freq);
    pulse_train(PIN_STEP[i], static_cast<uint32_t>(freq), 3000);
    delay(300);
  }

  tk::section("C) vsechny STEP soucasne");
  tk::info("%d Hz na IO4..IO7 po dobu 5 s", freq);
  const uint32_t half_us = 500000UL / static_cast<uint32_t>(freq);
  const uint32_t t0 = millis();
  while ((millis() - t0) < 5000) {
    for (uint8_t i = 0; i < 4; ++i) digitalWrite(PIN_STEP[i], HIGH);
    delayMicroseconds(half_us);
    for (uint8_t i = 0; i < 4; ++i) digitalWrite(PIN_STEP[i], LOW);
    delayMicroseconds(half_us);
  }

  for (uint8_t i = 0; i < 4; ++i) digitalWrite(PIN_STEP[i], LOW);

  if (tk::prompt_yes_no("Videl jsi ocekavane pulsy na vsech ctyrech STEP?")) {
    tk::pass("STEP0..STEP3 generuji pulsy");
  } else {
    tk::fail("nektery STEP nefunguje - viz sonda, zkontroluj pajeni IO4..IO7");
  }

  tk::summary();
}
