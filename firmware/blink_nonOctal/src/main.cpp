// blink_nonOctal - blika na IO38 a nic jineho nedela.
//
// Zadny vypis na konzoli, zadna dalsi periferie, zadne cteni vstupu. Vsechny
// ostatni piny zustavaji tak, jak je nastavil boot (vstupy). IO19/IO20 se
// zamerne nepouzivaji - visi na nich nativni USB, pres ktere se flashuje.
//
// Verze pro ESP32-S3-DevKitC-1 (WROOM-1, quad flash). Kod je stejny jako
// v ../blink, jiny je jen platformio.ini.
//
// POZOR na devkitu: IO38 je u revize v1.1 privedena na datovy vstup adresne
// RGB LED (WS2812) - pomale prepinani urovne ji nerozsviti, zustane tmava.
// Overuje se tedy bud LED + ~330R externe mezi IO38 a GND, nebo multimetrem
// na headeru: uroven se strida mezi 0 V a 3V3.

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
