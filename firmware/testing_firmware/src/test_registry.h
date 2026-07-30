// Seznam vsech bring-up testu. Prekladá se jen v prostredi "menu";
// jednoucelova prostredi tXX_* tento soubor pres build_src_filter vynechavaji.
#pragma once

#include <stddef.h>

struct TestEntry {
  const char* name;
  const char* desc;
  const char* needs;  // co je potreba mit pripojene
  void (*fn)();
  bool moves_motor;   // true = hybe motorem, sekvencni rezim ho vynecha
};

extern const TestEntry TESTS[];
extern const size_t TEST_COUNT;

// Deklarace jednotlivych testu (definice v src/tests/).
void t00_chip_info();
void t01_blink_io38();
void t02_shift_register();
void t03_step_pins();
void t04_endswitches();
void t05_i2c_scan();
void t06_tmc_uart();
void t07_wiring_selftest();
void t08_motor_move();
void t09_motor_jog();
