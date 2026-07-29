// Spolecny vstupni bod bring-up testu.
//
// Dva rezimy, prepinaji se volbou prostredi v platformio.ini:
//
//   pio run -e menu -t upload            vsechny testy + menu na konzoli
//   pio run -e t00_chip_info -t upload   jen jeden test (-DTEST_ENTRY=...)
//
// Jednoucelove prostredi je uzitecne, kdyz deska pri nekterem testu tuhne
// nebo se resetuje - v binarce pak neni nic jineho, co by to mohlo zpusobit.

#include <Arduino.h>

#include "testkit.h"

#ifndef TEST_ENTRY
#include "test_registry.h"
#endif

namespace {

// Konzole je nativni USB CDC, a to ma jednu nemilou vlastnost: cokoli
// vypsaneho driv, nez hostitel otevre port, se nenavratne ztrati - typicky
// cely banner i vyzva. Deska pak jen blokuje na Serial.available() a terminal
// zustane prazdny, jako by nic nenabootovalo.
//
// Detekovat pripojeni terminalu nejde: HWCDC::operator bool() hlasi jen to,
// jestli je zapojeny USB kabel, ne jestli si nekdo port otevrel. Proto se
// pobidka dokud nedorazi prvni znak opakuje. Po prvni interakci uz vime, ze
// terminal posloucha, a opakovani se vypne, aby nezaplavovalo vypis testu.
void wait_for_input(const char* hint) {
  static bool seen_input = false;
  uint32_t last_hint = millis();
  while (!Serial.available()) {
    if (!seen_input && (millis() - last_hint) >= 2000) {
      last_hint = millis();
      Serial.println();
      Serial.println(hint);
    }
    delay(20);
  }
  seen_input = true;
}

}  // namespace

#ifdef TEST_ENTRY
extern void TEST_ENTRY();

void setup() {
  tk::console_begin();
  tk::safe_state();
  TEST_ENTRY();
}

void loop() {
  tk::info("test skoncil - stiskni klavesu pro opakovani");
  wait_for_input("[test skoncil, deska ceka - stisknij klavesu pro opakovani]");
  tk::flush_input();
  tk::safe_state();
  TEST_ENTRY();
}

#else  // interaktivni menu

namespace {

void print_menu() {
  Serial.println();
  tk::banner("esp32stepper - bring-up testy");
  Serial.println(F("  #  test                 co overuje / co potrebuje"));
  for (size_t i = 0; i < TEST_COUNT; ++i) {
    Serial.printf(" %2u  %-20s %s\r\n", static_cast<unsigned>(i),
                  TESTS[i].name, TESTS[i].desc);
    Serial.printf("     %-20s   potreba: %s\r\n", "", TESTS[i].needs);
  }
  Serial.println();
  Serial.println(F("  a   spustit 00-07 za sebou (bez pohybu motoru)"));
  Serial.println(F("  ?   znovu vypsat menu"));
  Serial.println();
}

void run_one(size_t i) {
  Serial.println();
  tk::safe_state();
  TESTS[i].fn();
  tk::safe_state();
  Serial.println();
}

void run_sequence() {
  uint16_t failed_tests = 0;
  for (size_t i = 0; i < TEST_COUNT; ++i) {
    if (TESTS[i].fn == &t08_motor_move) continue;  // pohyb jen rucne
    run_one(i);
    if (tk::fail_count() > 0) {
      ++failed_tests;
      if (!tk::prompt_yes_no("Test %s selhal. Pokracovat dal?",
                             TESTS[i].name)) {
        break;
      }
    }
  }
  tk::banner(failed_tests == 0 ? "sekvence: vse proslo"
                               : "sekvence: nektere testy selhaly");
}

}  // namespace

void setup() {
  tk::console_begin();
  tk::safe_state();
  print_menu();
}

void loop() {
  Serial.print(F("test> "));
  tk::flush_input();
  wait_for_input("[deska ceka na vyber - stisknij Enter pro vypis menu]");

  // Precti cislo (jedna nebo dve cislice) nebo prikaz.
  char buf[8];
  size_t n = 0;
  const uint32_t t0 = millis();
  while ((millis() - t0) < 2000) {
    if (!Serial.available()) {
      if (n > 0) break;
      delay(5);
      continue;
    }
    const int c = Serial.read();
    if (c == '\r' || c == '\n') break;
    if (n < sizeof(buf) - 1) buf[n++] = static_cast<char>(c);
  }
  buf[n] = '\0';
  Serial.println(buf);

  if (n == 0 || buf[0] == '?') {
    print_menu();
    return;
  }
  if (buf[0] == 'a' || buf[0] == 'A') {
    run_sequence();
    return;
  }

  char* end = nullptr;
  const long idx = strtol(buf, &end, 10);
  if (end == buf || idx < 0 || static_cast<size_t>(idx) >= TEST_COUNT) {
    Serial.printf("neznamy vyber '%s', platne je 0-%u, 'a' nebo '?'\r\n", buf,
                  static_cast<unsigned>(TEST_COUNT - 1));
    return;
  }
  run_one(static_cast<size_t>(idx));
}

#endif  // TEST_ENTRY
