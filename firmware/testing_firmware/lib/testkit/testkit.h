// Spolecna vybava pro bring-up testy desky esp32stepper.
//
// Kazdy test je jedna funkce bez parametru v src/tests/. Testkit dava
// konzoli s jednotnym formatem vysledku, ovladac 74HC595 a ovladac
// jednodratove TMC2209 UART sbernice.
#pragma once

#include <Arduino.h>

#include "board_pins.h"

namespace tk {

// --- konzole ---------------------------------------------------------------

// Ceka na otevreni USB CDC hostem (max ~3 s) a vypise hlavicku.
void console_begin();

void banner(const char* title);
void section(const char* name);

void info(const char* fmt, ...);
void pass(const char* fmt, ...);  // "[ OK ]" + pocitadlo
void fail(const char* fmt, ...);  // "[FAIL]" + pocitadlo
void warn(const char* fmt, ...);

// Pocitadla vysledku - test si je na zacatku vynuluje, na konci vytiskne.
void reset_results();
void summary();
uint16_t fail_count();

// Vstup z konzole.
void flush_input();
bool key_pressed();                    // nebloku­jici, znak zahodi
void wait_enter(const char* msg);      // ceka na Enter (nebo libovolny znak)
bool prompt_yes_no(const char* fmt, ...);
int read_int(const char* prompt, int lo, int hi, int fallback);

// --- bezpecny vychozi stav -------------------------------------------------

// STEP piny L, vsechny drivery zakazane. Vola se pred kazdym testem.
void safe_state();

// --- 74HC595 (U4) ----------------------------------------------------------

class ShiftReg {
 public:
  void begin();                                  // piny + SR_ALL_DISABLED
  void write(uint8_t value);                     // shiftOut MSB-first + latch
  uint8_t value() const { return state_; }
  void set_dir(uint8_t motor, bool high);
  void set_enable(uint8_t motor, bool enabled);  // true = driver povolen (ENN L)
  void disable_all();

 private:
  uint8_t state_ = SR_ALL_DISABLED;
};

// --- TMC2209 jednodratova UART --------------------------------------------

// Registry pouzivane testy.
enum : uint8_t {
  TMC_GCONF      = 0x00,
  TMC_GSTAT      = 0x01,
  TMC_IFCNT      = 0x02,
  TMC_IOIN       = 0x06,
  TMC_IHOLD_IRUN = 0x10,
  TMC_TPOWERDOWN = 0x11,
  TMC_CHOPCONF   = 0x6C,
  TMC_DRV_STATUS = 0x6F,
};

// Bity IOIN (0x06) na TMC2209.
enum : uint8_t {
  IOIN_ENN      = 0,
  IOIN_MS1      = 2,
  IOIN_MS2      = 3,
  IOIN_DIAG     = 4,
  IOIN_PDN_UART = 6,
  IOIN_STEP     = 7,
  IOIN_SPREAD   = 8,
  IOIN_DIR      = 9,
};
static constexpr uint8_t TMC2209_VERSION = 0x21;  // IOIN[31:24]

class TmcBus {
 public:
  void begin(uint32_t baud = 115200);
  bool read_reg(uint8_t node, uint8_t reg, uint32_t* out);
  bool write_reg(uint8_t node, uint8_t reg, uint32_t value);

  // Kolik ctecich pokusu selhalo od begin() - hrube merítko kvality linky.
  uint32_t errors() const { return errors_; }

 private:
  void discard_echo(size_t n);
  bool read_exact(uint8_t* buf, size_t n, uint32_t timeout_ms);

  uint32_t errors_ = 0;
};

// Dekodery pro citelny vypis.
void print_ioin(uint32_t ioin);
void print_drv_status(uint32_t st);

}  // namespace tk
