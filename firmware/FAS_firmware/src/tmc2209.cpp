#include "tmc2209.h"

#include <math.h>

#include "board_pins.h"
#include "fas_config.h"

namespace fas {

namespace {

// CRC8, polynom x^8+x^2+x^1+1, dle datasheetu TMC2209 kap. 4.2.
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

constexpr float V_FS_HIGH = 0.325f;  // vsense = 0
constexpr float V_FS_LOW  = 0.180f;  // vsense = 1
constexpr float SQRT2 = 1.41421356f;

int cs_for(uint16_t ma, float v_fs) {
  const float cs = (static_cast<float>(ma) / 1000.0f) * SQRT2 * 32.0f *
                       (R_SENSE_OHM + 0.02f) / v_fs -
                   1.0f;
  return static_cast<int>(lroundf(cs));
}

// MRES v CHOPCONF[27:24]: 0 = 256 mikrokroku, kazdy dalsi kod je pulka,
// 8 = plny krok.
uint8_t mres_code(uint16_t usteps) {
  uint8_t code = 8;
  while (usteps > 1) {
    usteps = static_cast<uint16_t>(usteps / 2);
    --code;
  }
  return code;
}

// TMC2209 umi jen 2^n; cokoli jineho zaokrouhli nahoru, aby se nikdy
// nenastavil hrubsi krok, nez si nekdo vyzadal.
uint16_t round_pow2(uint16_t v) {
  uint16_t p = 1;
  while (p < 256 && p < v) p = static_cast<uint16_t>(p * 2);
  return p;
}

}  // namespace

uint8_t cs_from_ma(uint16_t ma, bool* vsense) {
  int cs = cs_for(ma, V_FS_HIGH);
  bool low_range = false;

  // Pod polovinou rozsahu je krok proudu zbytecne hruby - jemnejsi rozsah dava
  // pro stejny proud dvojnasobne CS, tedy pulku chyby zaokrouhleni.
  if (cs < 16) {
    const int cs_low = cs_for(ma, V_FS_LOW);
    if (cs_low <= 31) {
      cs = cs_low;
      low_range = true;
    }
  }

  if (cs < 0) cs = 0;
  if (cs > 31) cs = 31;
  if (vsense) *vsense = low_range;
  return static_cast<uint8_t>(cs);
}

uint16_t ma_from_cs(uint8_t cs, bool vsense) {
  const float v_fs = vsense ? V_FS_LOW : V_FS_HIGH;
  const float ma = (static_cast<float>(cs) + 1.0f) / 32.0f * v_fs /
                   (R_SENSE_OHM + 0.02f) / SQRT2 * 1000.0f;
  return static_cast<uint16_t>(lroundf(ma));
}

// --- sbernice ---------------------------------------------------------------

void Tmc2209::begin(uint8_t node, uint32_t baud) {
  node_ = node;
  Serial1.begin(baud, SERIAL_8N1, PIN_TMC_RX, PIN_TMC_TX);
  delay(20);
  while (Serial1.available()) {
    Serial1.read();
  }
  errors_ = 0;
}

bool Tmc2209::read_exact(uint8_t* buf, size_t n, uint32_t timeout_ms) {
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

void Tmc2209::discard_echo(size_t n) {
  uint8_t sink[12];
  read_exact(sink, n, 20);
}

void Tmc2209::write_reg(uint8_t reg, uint32_t value) {
  uint8_t d[8] = {0x05,
                  node_,
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
}

bool Tmc2209::read_reg(uint8_t reg, uint32_t* out) {
  uint8_t req[4] = {0x05, node_, reg, 0};
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

// --- vyssi vrstva -----------------------------------------------------------

bool Tmc2209::ping(uint32_t* ioin_out) {
  uint32_t ioin = 0;
  if (!read_reg(TMC_IOIN, &ioin)) return false;
  if (ioin_out) *ioin_out = ioin;

  if (static_cast<uint8_t>((ioin >> 24) & 0xFF) != TMC2209_VERSION) return false;

  // Kontrola, ze odpovedel ten driver, na ktery se ptame: MS1/MS2 kodujou
  // adresu, takze musi dat totez cislo jako node.
  const uint8_t addr_pins = static_cast<uint8_t>(
      (((ioin >> IOIN_MS2) & 1) << 1) | ((ioin >> IOIN_MS1) & 1));
  return addr_pins == node_;
}

bool Tmc2209::configure(uint16_t run_ma, uint8_t hold_pct, uint16_t microsteps) {
  const uint16_t usteps = round_pow2(microsteps);

  uint32_t chop = 0;
  if (!read_reg(TMC_CHOPCONF, &chop)) return false;
  uint32_t ifcnt_before = 0;
  if (!read_reg(TMC_IFCNT, &ifcnt_before)) return false;

  bool vsense = false;
  const uint8_t irun = cs_from_ma(run_ma, &vsense);
  // IHOLD se pocita ze stejneho rozsahu jako IRUN (jinak by procenta
  // nesouhlasila) a jde pres "+1", protoze proud je (CS+1)/32 z rozsahu -
  // ne CS/32. Bez toho vyjde klidovy proud vzdy o krok vyssi.
  int ihold_calc =
      static_cast<int>((static_cast<uint32_t>(irun) + 1) * hold_pct / 100) - 1;
  if (ihold_calc < 0) ihold_calc = 0;
  const uint32_t ihold = static_cast<uint32_t>(ihold_calc);

  // CHOPCONF jde PRVNI, a to zamerne (viz firmware/TMC2209_MS1_MS2.md,
  // HW-00201): dokud je GCONF bit7 (mstep_reg_select) nula, bere driver
  // mikrokrok z pinu MS1/MS2. V okamziku, kdy se ten bit prehodi, zacne platit
  // hodnota v registru - takze v nem uz musi byt ta spravna. V obracenem
  // poradi by driver mezitim jel podle toho, co v MRES zbylo (po resetu 0,
  // tedy 1/256), a na stejny pocet pulzu by se hridel otocila 16x mene.
  //
  // Cteme a menime jen dva udaje - MRES a vsense. Ostatni bity (TOFF, TBL,
  // hystereze, intpol) zustavaji, jak je nastavil driver sam.
  chop = (chop & ~(0xFUL << 24)) |
         (static_cast<uint32_t>(mres_code(usteps)) << 24);
  chop = vsense ? (chop | (1UL << 17)) : (chop & ~(1UL << 17));
  write_reg(TMC_CHOPCONF, chop);

  // GCONF, tri bity zamerne a zbytek na nulu:
  //   bit6 pdn_disable=1     UART ma prednost pred PDN pinem (jinak driver
  //                          povazuje sbernici za "power down" signal)
  //   bit7 mstep_reg_select=1 mikrokrok z registru - MS1/MS2 tady drzi adresu
  //   bit8 multistep_filt=1   filtr STEP vstupu, datasheet ho doporucuje pri
  //                          externim zdroji kroku (a je to vychozi stav)
  // Nula na bit0 (I_scale_analog) NENI opomenuti, ale rozhodnuti: s jednickou
  // by proud skaloval pin VREF a prepocet mA -> CS by nic neznamenal. Modul
  // driveru VREF na tehle desce ani nevyvadi. Bit2 (en_spreadCycle) nula =
  // stealthChop. Pozn.: ../testing_firmware pise 0xC0, tedy bez filtru STEP.
  write_reg(TMC_GCONF, (1UL << 6) | (1UL << 7) | (1UL << 8));
  write_reg(TMC_TPOWERDOWN, TPOWERDOWN);

  write_reg(TMC_IHOLD_IRUN, (ihold & 0x1F) |
                                (static_cast<uint32_t>(irun) << 8) |
                                (static_cast<uint32_t>(IHOLDDELAY) << 16));

  // Overeni. IFCNT je citac prijatych zapisu - kdyz nestoupl o ctyri, neco
  // po drate nedoslo a proud v motoru neni ten, ktery si myslime.
  uint32_t ifcnt_after = 0;
  if (!read_reg(TMC_IFCNT, &ifcnt_after)) return false;
  if (static_cast<uint8_t>(ifcnt_after - ifcnt_before) != 4) return false;

  // Ctenim zpatky se overuji jen ty bity, ktere jsme opravdu nastavovali.
  // Cely registr se nesrovnava zamerne: kdyby nektery rezervovany bit cetl
  // jinak, nez se zapsal, firmware by halil na neexistujici problem.
  constexpr uint32_t CHOP_MASK = (0xFUL << 24) | (1UL << 17);  // MRES + vsense
  constexpr uint32_t GCONF_MASK = (1UL << 6) | (1UL << 7) | (1UL << 8);
  uint32_t back = 0;
  if (!read_reg(TMC_CHOPCONF, &back)) return false;
  if ((back & CHOP_MASK) != (chop & CHOP_MASK)) return false;
  if (!read_reg(TMC_GCONF, &back)) return false;
  if ((back & GCONF_MASK) != GCONF_MASK) return false;

  irun_ = irun;
  ihold_ = static_cast<uint8_t>(ihold & 0x1F);
  vsense_ = vsense;
  microsteps_ = usteps;
  return true;
}

bool Tmc2209::read_status(DrvStatus* out) {
  uint32_t st = 0;
  if (!read_reg(TMC_DRV_STATUS, &st)) return false;

  out->raw = st;
  out->otpw = st & (1u << 0);
  out->ot = st & (1u << 1);
  out->s2ga = st & (1u << 2);
  out->s2gb = st & (1u << 3);
  out->s2vsa = st & (1u << 4);
  out->s2vsb = st & (1u << 5);
  out->ola = st & (1u << 6);
  out->olb = st & (1u << 7);
  out->cs_actual = static_cast<uint8_t>((st >> 16) & 0x1F);
  out->stst = st & (1ul << 31);
  return true;
}

bool Tmc2209::read_and_clear_gstat(uint32_t* out) {
  uint32_t g = 0;
  if (!read_reg(TMC_GSTAT, &g)) return false;
  if (g & 0x07) write_reg(TMC_GSTAT, g & 0x07);  // bity se nuluji jednickou
  *out = g;
  return true;
}

}  // namespace fas
