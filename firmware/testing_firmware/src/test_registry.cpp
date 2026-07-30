#include "test_registry.h"

// Poradi = doporucene poradi ozivovani. Kazdy test predpoklada, ze ty pred
// nim uz prosly. Posledni polozka radku je "hybe motorem" - takove testy
// sekvencni rezim ('a' v menu) vynechava.
const TestEntry TESTS[] = {
    {"t00_chip_info", "hlasi se cip, sedi flash/PSRAM varianta",
     "jen USB", &t00_chip_info, false},
    {"t01_blink_io38", "GPIO vystup na pinu J4", "LED+330R nebo multimetr",
     &t01_blink_io38, false},
    {"t02_shift_register", "74HC595 budi DIR a EN", "sonda na patky BOB",
     &t02_shift_register, false},
    {"t03_step_pins", "STEP pulsy na IO4..IO7", "osciloskop", &t03_step_pins,
     false},
    {"t04_endswitches", "koncove spinace IO13..IO16", "spinace, ruka",
     &t04_endswitches, false},
    {"t05_i2c_scan", "TCA9548A a kanaly enkoderu", "nic / enkodery",
     &t05_i2c_scan, false},
    {"t06_tmc_uart", "drivery odpovidaji na UART", "osazene BOB-0..3",
     &t06_tmc_uart, false},
    {"t07_wiring_selftest", "DIR/EN/STEP overene ctenim IOIN",
     "osazene BOB-0..3", &t07_wiring_selftest, false},
    {"t08_motor_move", "skutecny pohyb jednoho motoru", "24 V + motor",
     &t08_motor_move, true},
    {"t09_motor_jog", "pusteni motoru a jednoduche pohyby po krocich",
     "24 V + motor", &t09_motor_jog, true},
};

const size_t TEST_COUNT = sizeof(TESTS) / sizeof(TESTS[0]);
