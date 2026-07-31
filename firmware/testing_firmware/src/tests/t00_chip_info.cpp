// T00 - hlasi se cip?
//
// Nulty test cele sady: nesaha na zadnou perifernii desky, jen overi, ze
// modul nabootoval, konzole pres nativni USB CDC funguje a ze skutecna
// varianta modulu odpovida tomu, co predpoklada platformio.ini (quad flash
// alespon 8 MB; PSRAM se hlasi, ale sada testu ji nepotrebuje).
//
// Potrebuje: jen USB kabel.

#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_system.h>

#include "status_led.h"
#include "testkit.h"

namespace {

const char* flash_mode_name(FlashMode_t m) {
  switch (m) {
    case FM_QIO: return "QIO";
    case FM_QOUT: return "QOUT";
    case FM_DIO: return "DIO";
    case FM_DOUT: return "DOUT";
    case FM_FAST_READ: return "FAST_READ";
    case FM_SLOW_READ: return "SLOW_READ";
    default: return "neznamy/OPI";
  }
}

const char* reset_reason_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "externi reset";
    case ESP_RST_SW: return "softwarovy reset";
    case ESP_RST_PANIC: return "panic / vyjimka";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "jiny watchdog";
    case ESP_RST_BROWNOUT: return "brownout (propad napajeni!)";
    case ESP_RST_DEEPSLEEP: return "probuzeni z deep sleep";
    default: return "neznamy";
  }
}

}  // namespace

void t00_chip_info() {
  status_led_pause();
  tk::reset_results();
  tk::banner("T00 - identifikace cipu");

  tk::section("cip");
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  tk::info("model        : %s (rev %u)", ESP.getChipModel(), ci.revision);
  tk::info("jader        : %u", ci.cores);
  tk::info("CPU          : %lu MHz", static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  tk::info("ESP-IDF      : %s", ESP.getSdkVersion());

  const uint64_t mac = ESP.getEfuseMac();
  tk::info("eFuse MAC    : %02X:%02X:%02X:%02X:%02X:%02X",
           static_cast<uint8_t>(mac), static_cast<uint8_t>(mac >> 8),
           static_cast<uint8_t>(mac >> 16), static_cast<uint8_t>(mac >> 24),
           static_cast<uint8_t>(mac >> 32), static_cast<uint8_t>(mac >> 40));

  const esp_reset_reason_t rr = esp_reset_reason();
  tk::info("duvod resetu : %s", reset_reason_name(rr));

  tk::section("pamet");
  const uint32_t flash = ESP.getFlashChipSize();
  tk::info("flash        : %lu B (%lu MB), %lu MHz, mode %s",
           static_cast<unsigned long>(flash),
           static_cast<unsigned long>(flash / (1024UL * 1024UL)),
           static_cast<unsigned long>(ESP.getFlashChipSpeed() / 1000000UL),
           flash_mode_name(ESP.getFlashChipMode()));
  tk::info("PSRAM        : %lu B volne %lu B",
           static_cast<unsigned long>(ESP.getPsramSize()),
           static_cast<unsigned long>(ESP.getFreePsram()));
  tk::info("heap         : %lu B volne %lu B",
           static_cast<unsigned long>(ESP.getHeapSize()),
           static_cast<unsigned long>(ESP.getFreeHeap()));
  tk::info("firmware     : %lu B, volne misto %lu B",
           static_cast<unsigned long>(ESP.getSketchSize()),
           static_cast<unsigned long>(ESP.getFreeSketchSpace()));

  tk::section("kontroly");

  // Konzole zjevne funguje, kdyz tohle ctes - ale zaznamenej to jako krok.
  tk::pass("USB CDC konzole funguje (tento vypis dorazil)");

  if (ci.model == CHIP_ESP32S3) {
    tk::pass("cip je ESP32-S3");
  } else {
    tk::fail("cip NENI ESP32-S3 - deska osazena jinym modulem?");
  }

  if (flash >= 8UL * 1024UL * 1024UL) {
    tk::pass("flash %lu MB, staci na 8MB tabulku z platformio.ini",
             static_cast<unsigned long>(flash / (1024UL * 1024UL)));
  } else {
    tk::fail("flash jen %lu MB - platformio.ini predpoklada 8 MB "
             "(uprav board_upload.flash_size a partitions)",
             static_cast<unsigned long>(flash / (1024UL * 1024UL)));
  }

  // PSRAM nema zadny test v sade zapotrebi, takze jen informace - warn, ne fail.
  //
  // Pouzitelna velikost je vzdycky o neco mensi nez nominalni (cast si berou
  // rezervovane oblasti) - 2MB PSRAM hlasi ~2095 kB. Prah je proto 1 MB, jen
  // aby se poznala uplne nesmyslna hodnota; presnou variantu rekne vypis v B.
  if (!psramFound()) {
    tk::warn("PSRAM nenalezena - varianta modulu neni R*, nebo ma octal PSRAM "
             "(platformio.ini nastavuje quad). Testy ji nepotrebuji.");
  } else if (ESP.getPsramSize() >= 1024UL * 1024UL) {
    tk::pass("PSRAM nalezena, %lu B (~%lu MB)",
             static_cast<unsigned long>(ESP.getPsramSize()),
             static_cast<unsigned long>((ESP.getPsramSize() + 512UL * 1024UL) /
                                        (1024UL * 1024UL)));
  } else {
    tk::warn("PSRAM nalezena, ale jen %lu B - zkontroluj variantu modulu",
             static_cast<unsigned long>(ESP.getPsramSize()));
  }

  if (rr == ESP_RST_BROWNOUT) {
    tk::fail("posledni reset byl brownout - napajeni nestaci nebo kolisa");
  } else if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
             rr == ESP_RST_TASK_WDT) {
    tk::warn("posledni reset byl chybovy (%s) - podezrely predchozi beh",
             reset_reason_name(rr));
  } else {
    tk::pass("duvod resetu je v poradku: %s", reset_reason_name(rr));
  }

  tk::summary();
}
