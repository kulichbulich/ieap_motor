// TMC2209 na jednodratove UART sbernici.
//
// Vsechny ctyri drivery na desce visi na jednom vodici: IO17 vysila pres R19
// 1k na sbernici, IO18 ji cte. ESP32 tedy slysi i vlastni vysilani, a to se
// musi po kazdem ramci zahodit (discard_echo), jinak se echo zamenli za
// odpoved driveru. Adresu driveru davaji pull-upy R13..R16 na MS1/MS2.
//
// Zamerne to neni TMCStepper: knihovna umi radove vic, ale jednodratovy rezim
// s echem si stejne rezie navic; tady jde o pet registru a chceme videt, co
// presne jde po drate.

#pragma once

#include <Arduino.h>

namespace sm {

// Registry pouzivane timto firmwarem (datasheet TMC2209 kap. 5).
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

// Bity IOIN (0x06).
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

// Bity GSTAT (0x01). Nuluji se zapisem jednicky.
enum : uint8_t {
  GSTAT_RESET   = 0,  // driver nabehl a ztratil celou konfiguraci
  GSTAT_DRV_ERR = 1,  // vypnul se chybou (prehrati / zkrat)
  GSTAT_UV_CP   = 2,  // podpeti nabojove pumpy - typicky chybi 24 V
};

// Rozebrany DRV_STATUS (0x6F).
struct DrvStatus {
  uint32_t raw = 0;
  bool otpw = false;   // predzvest prehrati - jen varovani
  bool ot = false;     // prehrati, driver se vypnul
  bool s2ga = false;   // zkrat faze A na GND
  bool s2gb = false;   // zkrat faze B na GND
  bool s2vsa = false;  // zkrat faze A na VS
  bool s2vsb = false;  // zkrat faze B na VS
  bool stst = false;   // motor stoji
  uint8_t cs_actual = 0;  // skutecne pouzity CS - 0 znamena, ze netece proud

  // Chyba, na kterou se ma zastavit. ola/olb (rozpojena faze) se zamerne
  // nectou vubec: hlasi je i zdravy stojici motor pri stealthChop. otpw taky
  // neni chyba, jen predzvest prehrati.
  bool fault() const { return ot || s2ga || s2gb || s2vsa || s2vsb; }
};

// Prepocet proudu podle datasheetu kap. 9:
//   I_rms = (CS+1)/32 * V_fs/(R_sense+0.02) * 1/sqrt(2)
// V_fs = 0.325 V pri vsense=0, 0.180 V pri vsense=1. Nizsi rozsah ma jemnejsi
// krok, takze se pouzije, kdyz se do nej pozadovany proud vejde.
uint8_t cs_from_ma(uint16_t ma, bool* vsense);
uint16_t ma_from_cs(uint8_t cs, bool vsense);

class Tmc2209 {
 public:
  // node = adresa driveru na sbernici (TMC_ADDR[slot]).
  void begin(uint8_t node, uint32_t baud = 115200);

  bool read_reg(uint8_t reg, uint32_t* out);
  void write_reg(uint8_t reg, uint32_t value);  // driver zapis nekvituje

  // Odpovida driver a je to opravdu on? Kontroluje VERSION i to, ze MS1/MS2
  // kodujou adresu, na kterou odpovedel.
  bool ping(uint32_t* ioin_out = nullptr);

  // Jedno nastaveni pro celou jizdu: GCONF (UART ma prednost pred piny,
  // mikrokrok z registru), proud, mikrokrok, TPOWERDOWN. Overuje se ctenim
  // zpet a pres IFCNT, takze false znamena "zapisy neprosly", ne "nevim".
  bool configure(uint16_t run_ma, uint8_t hold_pct, uint16_t microsteps);

  bool read_status(DrvStatus* out);
  // Precte GSTAT a zaroven ho vynuluje (bity se nuluji zapisem jednicky).
  bool read_and_clear_gstat(uint32_t* out);

  // Co se doopravdy nastavilo - pro vypis na konzoli.
  uint8_t irun() const { return irun_; }
  uint8_t ihold() const { return ihold_; }
  bool vsense() const { return vsense_; }
  uint16_t microsteps() const { return microsteps_; }
  uint16_t run_ma_actual() const { return ma_from_cs(irun_, vsense_); }
  uint16_t hold_ma_actual() const { return ma_from_cs(ihold_, vsense_); }

  uint8_t node() const { return node_; }
  uint32_t bus_errors() const { return errors_; }

 private:
  void discard_echo(size_t n);
  bool read_exact(uint8_t* buf, size_t n, uint32_t timeout_ms);

  uint8_t node_ = 0;
  uint8_t irun_ = 0;
  uint8_t ihold_ = 0;
  bool vsense_ = false;
  uint16_t microsteps_ = 0;
  uint32_t errors_ = 0;
};

}  // namespace sm
