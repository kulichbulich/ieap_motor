#include "testkit.h"

#include <stdarg.h>

namespace tk {

namespace {

uint16_t g_pass = 0;
uint16_t g_fail = 0;

void vprint(const char* tag, const char* fmt, va_list ap) {
  char buf[256];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  Serial.print(tag);
  Serial.println(buf);
}

}  // namespace

// --- konzole ---------------------------------------------------------------

void console_begin() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {
    delay(10);
  }
  delay(200);  // host stihne otevrit stream
  Serial.println();
  Serial.println();
}

void banner(const char* title) {
  Serial.println(F("==================================================="));
  Serial.print(F("  "));
  Serial.println(title);
  Serial.println(F("==================================================="));
}

void section(const char* name) {
  Serial.println();
  Serial.print(F("--- "));
  Serial.print(name);
  Serial.println(F(" ---"));
}

void info(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprint("       ", fmt, ap);
  va_end(ap);
}

void pass(const char* fmt, ...) {
  ++g_pass;
  va_list ap;
  va_start(ap, fmt);
  vprint("[ OK ] ", fmt, ap);
  va_end(ap);
}

void fail(const char* fmt, ...) {
  ++g_fail;
  va_list ap;
  va_start(ap, fmt);
  vprint("[FAIL] ", fmt, ap);
  va_end(ap);
}

void warn(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprint("[WARN] ", fmt, ap);
  va_end(ap);
}

void reset_results() {
  g_pass = 0;
  g_fail = 0;
}

void summary() {
  Serial.println();
  Serial.printf("=== vysledek: %u OK, %u FAIL ===\r\n", g_pass, g_fail);
}

uint16_t fail_count() { return g_fail; }

void flush_input() {
  while (Serial.available()) {
    Serial.read();
  }
}

bool key_pressed() {
  if (!Serial.available()) {
    return false;
  }
  flush_input();
  return true;
}

void wait_enter(const char* msg) {
  Serial.print(F(">>> "));
  Serial.print(msg);
  Serial.println(F("  [Enter]"));
  flush_input();
  while (!Serial.available()) {
    delay(10);
  }
  flush_input();
}

bool prompt_yes_no(const char* fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  flush_input();
  for (;;) {
    Serial.print(F(">>> "));
    Serial.print(buf);
    Serial.println(F("  [a/n]"));
    while (!Serial.available()) {
      delay(10);
    }
    const int c = Serial.read();
    flush_input();
    if (c == 'a' || c == 'A' || c == 'y' || c == 'Y') return true;
    if (c == 'n' || c == 'N') return false;
  }
}

int read_int(const char* prompt, int lo, int hi, int fallback) {
  Serial.printf(">>> %s [%d-%d, Enter = %d]: ", prompt, lo, hi, fallback);
  flush_input();

  char buf[16];
  size_t n = 0;
  for (;;) {
    while (!Serial.available()) {
      delay(5);
    }
    const int c = Serial.read();
    if (c == '\r' || c == '\n') break;
    if (c >= '0' && c <= '9' && n < sizeof(buf) - 1) {
      buf[n++] = static_cast<char>(c);
      Serial.write(static_cast<char>(c));
    }
  }
  Serial.println();
  buf[n] = '\0';
  if (n == 0) return fallback;

  const int v = atoi(buf);
  return (v < lo || v > hi) ? fallback : v;
}

// --- bezpecny vychozi stav -------------------------------------------------

void safe_state() {
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
  }
  ShiftReg sr;
  sr.begin();  // zapise SR_ALL_DISABLED
}

// --- 74HC595 ---------------------------------------------------------------

void ShiftReg::begin() {
  pinMode(PIN_SR_SER, OUTPUT);
  pinMode(PIN_SR_SRCLK, OUTPUT);
  pinMode(PIN_SR_RCLK, OUTPUT);
  digitalWrite(PIN_SR_RCLK, LOW);
  digitalWrite(PIN_SR_SRCLK, LOW);
  write(SR_ALL_DISABLED);
}

void ShiftReg::write(uint8_t value) {
  state_ = value;
  // MSB-first: bit7 jde dovnitr prvni a skonci na QH, bit0 zustane na QA.
  shiftOut(PIN_SR_SER, PIN_SR_SRCLK, MSBFIRST, value);
  digitalWrite(PIN_SR_RCLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_SR_RCLK, LOW);
}

void ShiftReg::set_dir(uint8_t motor, bool high) {
  const uint8_t bit = SR_BIT_DIR[motor];
  write(high ? (state_ | (1u << bit)) : (state_ & ~(1u << bit)));
}

void ShiftReg::set_enable(uint8_t motor, bool enabled) {
  // ENN je aktivni v L.
  const uint8_t bit = SR_BIT_EN[motor];
  write(enabled ? (state_ & ~(1u << bit)) : (state_ | (1u << bit)));
}

void ShiftReg::disable_all() {
  write(static_cast<uint8_t>(state_ | 0xAA));
}

// --- TMC2209 UART ----------------------------------------------------------

namespace {

// CRC8, polynom x^8+x^2+x^1+1, dle datasheetu TMC2209.
uint8_t tmc_crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];
    for (uint8_t j = 0; j < 8; ++j) {
      if ((crc >> 7) ^ (b & 0x01)) {
        crc = static_cast<uint8_t>((crc << 1) ^ 0x07);
      } else {
        crc = static_cast<uint8_t>(crc << 1);
      }
      b >>= 1;
    }
  }
  return crc;
}

}  // namespace

void TmcBus::begin(uint32_t baud) {
  Serial1.begin(baud, SERIAL_8N1, PIN_TMC_RX, PIN_TMC_TX);
  delay(20);
  while (Serial1.available()) {
    Serial1.read();
  }
  errors_ = 0;
  echo_seen_ = false;
}

bool TmcBus::read_exact(uint8_t* buf, size_t n, uint32_t timeout_ms) {
  size_t got = 0;
  const uint32_t t0 = millis();
  while (got < n) {
    if (Serial1.available()) {
      buf[got++] = static_cast<uint8_t>(Serial1.read());
    } else if ((millis() - t0) > timeout_ms) {
      return false;
    }
  }
  return true;
}

bool TmcBus::discard_echo(size_t n) {
  // IO18 visi na stejne sbernici jako IO17, takze ctame i vlastni vysilani.
  // Jestli se echo vratilo, si pamatujeme - je to jedina informace, ktera
  // odlisi vadnou TX/RX cestu od driveru, ktery jen neodpovida.
  uint8_t sink[12];
  echo_seen_ = read_exact(sink, n, 20);
  return echo_seen_;
}

bool TmcBus::write_reg(uint8_t node, uint8_t reg, uint32_t value) {
  uint8_t d[8] = {0x05,
                  node,
                  static_cast<uint8_t>(reg | 0x80),
                  static_cast<uint8_t>(value >> 24),
                  static_cast<uint8_t>(value >> 16),
                  static_cast<uint8_t>(value >> 8),
                  static_cast<uint8_t>(value),
                  0};
  d[7] = tmc_crc8(d, 7);

  while (Serial1.available()) Serial1.read();
  Serial1.write(d, sizeof(d));
  Serial1.flush();
  discard_echo(sizeof(d));
  return true;
}

bool TmcBus::read_reg(uint8_t node, uint8_t reg, uint32_t* out) {
  uint8_t req[4] = {0x05, node, reg, 0};
  req[3] = tmc_crc8(req, 3);

  while (Serial1.available()) Serial1.read();
  Serial1.write(req, sizeof(req));
  Serial1.flush();
  discard_echo(sizeof(req));

  uint8_t rep[8];
  if (!read_exact(rep, sizeof(rep), 30)) {
    ++errors_;
    return false;
  }
  if (rep[0] != 0x05 || rep[1] != 0xFF || rep[2] != reg) {
    ++errors_;
    return false;
  }
  if (tmc_crc8(rep, 7) != rep[7]) {
    ++errors_;
    return false;
  }

  *out = (static_cast<uint32_t>(rep[3]) << 24) |
         (static_cast<uint32_t>(rep[4]) << 16) |
         (static_cast<uint32_t>(rep[5]) << 8) | rep[6];
  return true;
}

void print_ioin(uint32_t ioin) {
  const uint8_t addr = static_cast<uint8_t>(((ioin >> IOIN_MS2) & 1) << 1 |
                                            ((ioin >> IOIN_MS1) & 1));
  info("IOIN=0x%08lX  ver=0x%02lX  ENN=%lu STEP=%lu DIR=%lu DIAG=%lu "
       "PDN=%lu SPREAD=%lu  MS1/MS2 -> addr %u",
       static_cast<unsigned long>(ioin),
       static_cast<unsigned long>((ioin >> 24) & 0xFF),
       static_cast<unsigned long>((ioin >> IOIN_ENN) & 1),
       static_cast<unsigned long>((ioin >> IOIN_STEP) & 1),
       static_cast<unsigned long>((ioin >> IOIN_DIR) & 1),
       static_cast<unsigned long>((ioin >> IOIN_DIAG) & 1),
       static_cast<unsigned long>((ioin >> IOIN_PDN_UART) & 1),
       static_cast<unsigned long>((ioin >> IOIN_SPREAD) & 1), addr);
}

void print_drv_status(uint32_t st) {
  info("DRV_STATUS=0x%08lX  cs_actual=%lu stst=%lu",
       static_cast<unsigned long>(st),
       static_cast<unsigned long>((st >> 16) & 0x1F),
       static_cast<unsigned long>((st >> 31) & 1));
  if (st & (1u << 0)) warn("  otpw  - predzvest prehrati");
  if (st & (1u << 1)) warn("  ot    - prehrati, driver vypnut");
  if (st & (1u << 2)) warn("  s2ga  - zkrat faze A na GND");
  if (st & (1u << 3)) warn("  s2gb  - zkrat faze B na GND");
  if (st & (1u << 4)) warn("  s2vsa - zkrat faze A na VS");
  if (st & (1u << 5)) warn("  s2vsb - zkrat faze B na VS");
  if (st & (1u << 6)) warn("  ola   - rozpojena faze A (nebo motor stoji)");
  if (st & (1u << 7)) warn("  olb   - rozpojena faze B (nebo motor stoji)");
}

}  // namespace tk
