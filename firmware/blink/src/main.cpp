// blink - blika na IO38 a nic jineho nedela.
//
// Zadny vypis na konzoli, zadna dalsi periferie, zadne cteni vstupu. Vsechny
// ostatni piny zustavaji tak, jak je nastavil boot (vstupy), takze je bezpecne
// tohle pustit i na cerstve osazene desce - 595 ani TMC se nebudi.
//
// IO38 je na pinove liste J4 ("spare"). Softwarove riditelna LED na desce
// neni, takze LED + ~330R patri externe mezi J4 (IO38) a GND. Bez LED se to
// da overit multimetrem: uroven se strida mezi 0 V a 3V3.

#include <Arduino.h>

static constexpr uint8_t LED_PIN = 38;
static constexpr uint32_t HALF_PERIOD_MS = 300;  // 300 ms svit, 300 ms tma

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(HALF_PERIOD_MS);
  digitalWrite(LED_PIN, LOW);
  delay(HALF_PERIOD_MS);
}
