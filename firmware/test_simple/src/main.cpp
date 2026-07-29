// test_simple - overi jen jednu vec: ze ESP32 bezi a mluvi na konzoli.
//
// Nesaha na zadny periferni pin desky (595, STEP, TMC UART), takze je bezpecne
// tohle pustit na cerstve osazene desce. Tiskne soucasne na nativni USB CDC
// a na hardwarovy UART pro externi FTDI - staci se divat na to, co je po ruce.
//
// Pozn.: nativni USB CDC zahodi vsechno, co se vytiskne drive, nez si host
// otevre port. Proto se hlaska opakuje kazdou sekundu a zadne "jednorazove"
// uvitani se nepouziva.

#include <Arduino.h>

#ifndef DBG_UART_TX
#define DBG_UART_TX 2
#endif
#ifndef DBG_UART_RX
#define DBG_UART_RX 1
#endif

static HardwareSerial dbg(1);   // UART1 na piny podle prostredi

// Vytiskne stejny radek na obe rozhrani.
static void say(const String &s) {
  Serial.println(s);
  dbg.println(s);
}

void setup() {
  Serial.begin(115200);                                     // nativni USB CDC
  dbg.begin(115200, SERIAL_8N1, DBG_UART_RX, DBG_UART_TX);  // FTDI

  say("");
  say("=== test_simple: ESP32 nabootoval ===");
  say(String("chip: ") + ESP.getChipModel() + " rev " + ESP.getChipRevision() +
      ", " + ESP.getChipCores() + " jadra, " + getCpuFrequencyMhz() + " MHz");
  say(String("flash: ") + (ESP.getFlashChipSize() / 1024 / 1024) + " MB, heap: " +
      ESP.getFreeHeap() + " B");
  say(String("debug UART: TX=IO") + DBG_UART_TX + " RX=IO" + DBG_UART_RX);
  say("Kazdou sekundu se tiskne ALIVE. Cokoliv poslane zpet se vypise.");
}

void loop() {
  static uint32_t n = 0;
  say(String("ALIVE #") + (++n) + "  uptime " + (millis() / 1000) + " s");

  // Overeni, ze funguje i prijem - proklepne druhy smer dratu.
  while (Serial.available()) say(String("USB  RX: ") + (char)Serial.read());
  while (dbg.available()) say(String("FTDI RX: ") + (char)dbg.read());

  delay(1000);
}
