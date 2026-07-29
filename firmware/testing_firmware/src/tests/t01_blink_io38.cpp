// T01 - blikani na IO38 (pin lista J4 "spare").
//
// Prvni test, ktery neco fyzicky budi. Overuje, ze GPIO vystup opravdu
// prepina uroven na pinu vyvedenem z modulu.
//
// Na desce NENI softwarove riditelna LED - LED1 visi na PG vystupu LT8610
// pres NPN1. Pro vizualni kontrolu pripoj externe LED + ~330R mezi J4 a GND,
// nebo mer multimetrem / sondou.
//
// Potrebuje: externi LED nebo multimetr na J4.

#include <Arduino.h>

#include "testkit.h"

void t01_blink_io38() {
  tk::reset_results();
  tk::banner("T01 - blikani na IO38 (J4)");

  tk::info("Na desce neni zadna ridiitelna LED. Pripoj LED + 330R mezi");
  tk::info("J4 a GND, nebo mer multimetrem mezi J4 a GND.");

  const int period_ms = tk::read_int("perioda [ms]", 100, 10000, 2000);

  pinMode(PIN_SPARE, OUTPUT);
  tk::info("blikam na IO38 s periodou %d ms, ukonci libovolnou klavesou",
           period_ms);
  tk::flush_input();

  uint32_t cycles = 0;
  bool level = false;
  for (;;) {
    level = !level;
    digitalWrite(PIN_SPARE, level ? HIGH : LOW);
    tk::info("IO38 = %s", level ? "H (3V3)" : "L (0V)");
    if (level) ++cycles;

    const uint32_t t0 = millis();
    while ((millis() - t0) < static_cast<uint32_t>(period_ms) / 2) {
      if (tk::key_pressed()) {
        digitalWrite(PIN_SPARE, LOW);
        tk::info("zastaveno po %lu cyklech, IO38 = L",
                 static_cast<unsigned long>(cycles));
        if (tk::prompt_yes_no("Menila se uroven na J4 podle vypisu?")) {
          tk::pass("GPIO vystup IO38 funguje");
        } else {
          tk::fail("na IO38 nebyla zadna zmena - zkontroluj pajeni J4 / modul");
        }
        tk::summary();
        return;
      }
      delay(5);
    }
  }
}
