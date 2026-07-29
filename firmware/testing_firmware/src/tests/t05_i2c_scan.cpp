// T05 - I2C sbernice a multiplexer TCA9548A.
//
// Sbernice je na IO11 (SCL) / IO12 (SDA). Na ni visi mux U6 na adrese 0x70
// (A0/A1/A2 na GND). Enkoder N je na kanalu N muxu (konektory encoder0..3).
// U6 ~RESET je jen pres R11 na +3V3 - firmware mux resetovat neumi.
//
// Potrebuje: nic navic pro nalezeni muxu; pro kanaly pripojene enkodery.

#include <Arduino.h>
#include <Wire.h>

#include "testkit.h"

namespace {

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Vrati pocet nalezenych zarizeni, volitelne krome samotneho muxu.
uint8_t scan(bool skip_mux) {
  uint8_t found = 0;
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    if (skip_mux && a == I2C_ADDR_MUX) continue;
    if (probe(a)) {
      ++found;
      tk::info("  nalezeno 0x%02X", a);
    }
  }
  if (found == 0) tk::info("  nic");
  return found;
}

bool mux_select(uint8_t mask) {
  Wire.beginTransmission(I2C_ADDR_MUX);
  Wire.write(mask);
  return Wire.endTransmission() == 0;
}

}  // namespace

void t05_i2c_scan() {
  tk::reset_results();
  tk::banner("T05 - I2C a multiplexer TCA9548A");

  Wire.begin(PIN_SDA, PIN_SCL, 100000UL);
  delay(10);

  tk::section("A) zakladni sbernice");
  tk::info("SCL=IO%u SDA=IO%u, 100 kHz", PIN_SCL, PIN_SDA);

  if (probe(I2C_ADDR_MUX)) {
    tk::pass("TCA9548A odpovida na 0x%02X", I2C_ADDR_MUX);
  } else {
    tk::fail("na 0x%02X nic neodpovida - zkontroluj U6, pull-upy a napajeni",
             I2C_ADDR_MUX);
    tk::info("scan cele sbernice:");
    scan(false);
    tk::summary();
    return;
  }

  // Se vsemi kanaly zavrenymi nesmi byt videt nic krome muxu.
  mux_select(0x00);
  tk::info("vsechny kanaly muxu zavrene, scan:");
  const uint8_t leaked = scan(true);
  if (leaked == 0) {
    tk::pass("pri zavrenych kanalech neni videt zadne dalsi zarizeni");
  } else {
    tk::warn("videt %u zarizeni i pri zavrenem muxu - visi primo na sbernici",
             leaked);
  }

  tk::section("B) kanaly muxu 0..3 (enkodery)");
  uint8_t channels_with_device = 0;
  for (uint8_t ch = 0; ch < 4; ++ch) {
    if (!mux_select(static_cast<uint8_t>(1u << ch))) {
      tk::fail("kanal %u: mux neprijal vyber", ch);
      continue;
    }
    tk::info("kanal %u (encoder%u):", ch, ch);
    const uint8_t n = scan(true);
    if (n > 0) {
      ++channels_with_device;
      tk::pass("kanal %u: %u zarizeni", ch, n);
    } else {
      tk::warn("kanal %u: prazdny (enkoder nepripojen?)", ch);
    }
  }
  mux_select(0x00);

  tk::section("vyhodnoceni");
  if (channels_with_device == 4) {
    tk::pass("vsechny ctyri enkoderove kanaly osazene");
  } else if (channels_with_device > 0) {
    tk::warn("osazeno %u ze 4 kanalu - pokud ma byt vic, zkontroluj kabelaz",
             channels_with_device);
  } else {
    tk::warn("na zadnem kanalu neni zarizeni - mux funguje, enkodery chybi");
  }

  tk::summary();
}
