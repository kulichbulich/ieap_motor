// Pin mapa desky esp32stepper, odectena z netlistu (PCB/*.kicad_sch).

#pragma once

#include <stdint.h>

// --- STEP, priamo z GPIO do BOB-0..3 ---
static constexpr uint8_t PIN_STEP[4] = {4, 5, 6, 7};

// --- 74HC595 (U4), DIR + EN vsech ctyr driveru ---
static constexpr uint8_t PIN_SR_SRCLK = 8;   // U4.11
static constexpr uint8_t PIN_SR_SER   = 9;   // U4.14
static constexpr uint8_t PIN_SR_RCLK  = 10;  // U4.12
// U4.13 (~OE) je natvrdo na GND, U4.10 (~SRCLR) na +3V3.

// Bitova mapa je PROLOZENA, ne blokova. Pri MSB-first zapisu:
//   bit0->QA=dir0  bit1->QB=en0  bit2->QC=dir1  bit3->QD=en1
//   bit4->QE=dir2  bit5->QF=en2  bit6->QG=dir3  bit7->QH=en3
static constexpr uint8_t SR_BIT_DIR[4] = {0, 2, 4, 6};
static constexpr uint8_t SR_BIT_EN[4]  = {1, 3, 5, 7};
// EN na SilentStepSticku je ENN -> aktivni v L (0 = driver povolen).
static constexpr uint8_t SR_ALL_DISABLED = 0xAA;  // vsechny EN v H, DIR v L

// --- TMC2209, jednodratova UART (IO17 -> R19 1k -> sbernice, IO18 cte) ---
static constexpr uint8_t PIN_TMC_TX = 17;
static constexpr uint8_t PIN_TMC_RX = 18;
// Adresy z pull-upu R13..R16 na MS1/MS2:
static constexpr uint8_t TMC_ADDR[4] = {0, 1, 2, 3};

// --- I2C sbernice + TCA9548A (U6) ---
static constexpr uint8_t PIN_SCL = 11;
static constexpr uint8_t PIN_SDA = 12;
static constexpr uint8_t I2C_ADDR_MUX = 0x70;  // A0/A1/A2 na GND
// U6.3 (~RESET) je jen pres R11 10k na +3V3 - firmware mux neresetuje.
// Enkoder N je na kanalu N muxu (konektory encoder0..3).

// --- Koncove spinace (10k pull-up na +3V3, 10R do serie) ---
static constexpr uint8_t PIN_ENDSWITCH[4] = {13, 14, 15, 16};

// --- Debug UART na listu J5 ---
// Puvodni komentar tvrdil IO1/IO2 (NE vychozi IO43/IO44) - podle physical
// zapojeni je to ale presne obracene, J5 je zadratovany na vychozi UART0
// (TXD0=IO43, RXD0=IO44).
static constexpr uint8_t PIN_UART_DBG_TX = 43;  // U0TXD
static constexpr uint8_t PIN_UART_DBG_RX = 44;  // U0RXD

// --- Volny pin na liste J4 ---
static constexpr uint8_t PIN_SPARE = 38;

// Zadna softwarove riditelna LED na desce neni - LED1 visi na PG pinu
// LT8610 pres NPN1.
