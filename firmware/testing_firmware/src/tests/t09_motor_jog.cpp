// T09 - pusteni motoru a jednoduche pohyby (jog).
//
// T08 odpovi na otazku "otoci se to vubec". T09 stoji za ni: driver se jednou
// nastavi, povoli a zustane povoleny, a z konzole se pak zadavaji jednotlive
// pohyby - dopredu, dozadu, tam a zpet nekolikrat, plynula jizda, rampa.
// Na tom se hleda proud a rychlost, na kterych motor jeste netika
// a nevynechava, a proklepe se mechanika.
//
// Zadava se to v KROCICH, stejne jako v T08: pocet kroku na jeden pohyb
// a rychlost v krocich za sekundu. Krok je jeden mikrokrok pri nastavenem
// rozliseni - kolik jich je na otacku, zavisi na motoru (typicky 200 plnych
// kroku) a na mikrokroku, takze pri 1/16 je otacka 3200 kroku.
//
// Poloha se pocita v krocich od startu (+ = smer A), takze po symetrickem
// pohybu musi hridel skoncit na stejne znacce. Bez enkoderu je to nejcitlivejsi
// kontrola ztracenych kroku, jaka jde udelat - proto se citac polohy vypisuje
// u kazdeho pohybu.
//
// Skutecne dosazena rychlost je o par procent nizsi nez zadana (digitalWrite
// + delayMicroseconds nejsou zdarma), proto se u kazdeho pohybu vypisuje
// i namerena hodnota.
//
// Vychozi motor je slot 03 (ctvrty) - na nem je zapojeny motor, na kterem se
// oziva. Jiny se vybere pri zadavani parametru nebo prikazem 'm'.
//
// Prikaz 'r' beh ukonci a spusti test znovu od zacatku (nove parametry), 'x'
// se vrati do menu. Driver se v obou pripadech zakaze.
//
// Potrebuje: 24 V na barrel jacku, motor na BOB-N, prosle T06 a T07 (a T08).
// POZOR: rotor se toci na prikaz, u plynule jizdy dokud nestisknes klavesu.
//        Koncovy spinac daneho motoru jizdu zastavi - ale jen kdyz je v klidu
//        rozepnuty (H). Kdyz na vstupu nic neni, hlidani se vypne a jedinou
//        pojistkou je klavesa.

#include <Arduino.h>

#include "testkit.h"

namespace {

tk::ShiftReg sr;
tk::TmcBus bus;

// Stav jog sezeni. Na zacatku testu se cely prepise (j = Jog()), aby druhy
// beh nezdedil polohu ani priznaky z prvniho.
struct Jog {
  uint8_t motor = 3;  // slot 03 - tam je zapojeny motor, viz hlavicka
  uint8_t node = 3;
  uint32_t steps = 400;  // kroku na jeden pohyb ('f' / 'b')
  int rate = 800;        // kroku za sekundu
  int irun = 8;
  uint16_t usteps = 16;
  int32_t pos = 0;       // kroku od startu, + = smer A
  uint32_t moves = 0;
  bool guard = true;     // hlidat koncovy spinac
  bool driver_error = false;
};

Jog j;

uint16_t round_pow2(int v) {
  uint16_t p = 1;
  while (p < 256 && p < static_cast<uint16_t>(v)) p = static_cast<uint16_t>(p * 2);
  return p;
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

// Strop je 20000 kroku/s - vys uz delayMicroseconds nema rozliseni a rezie
// digitalWrite prevazi nad periodou. Spodni hranice je stejna jako v T08.
int clamp_rate(int want) {
  if (want < 10) return 10;
  return (want > 20000) ? 20000 : want;
}

void set_rate(int want) {
  const int v = clamp_rate(want);
  if (v != want) {
    tk::warn("rychlost omezena na %d kroku/s (rozsah 10-20000)", v);
  }
  j.rate = v;
  tk::info("rychlost %d kroku/s (perioda %lu us)", j.rate,
           static_cast<unsigned long>(1000000UL / static_cast<uint32_t>(j.rate)));
}

uint32_t period_us(int rate) {
  return 1000000UL / static_cast<uint32_t>(clamp_rate(rate));
}

bool endstop_hit() {
  return j.guard && digitalRead(PIN_ENDSWITCH[j.motor]) == LOW;
}

// Vraci false, kdyz driver hlasi chybu. ola/olb (bity 6,7) se ignoruji -
// stojici motor je hlasi taky, stejne jako v T08.
bool check_status(bool verbose) {
  uint32_t st = 0;
  if (!bus.read_reg(j.node, tk::TMC_DRV_STATUS, &st)) {
    tk::warn("DRV_STATUS nelze precist - sbernice UART vypadla");
    return true;
  }
  if (verbose) tk::print_drv_status(st);
  if (st & 0x3F) {
    if (!verbose) tk::print_drv_status(st);
    tk::fail("driver hlasi chybu, DRV_STATUS=0x%08lX",
             static_cast<unsigned long>(st));
    j.driver_error = true;
    return false;
  }
  return true;
}

// Ujede zadany pocet kroku. Vraci, kolik jich skutecne ujel - mensi cislo nez
// zadane znamena preruseni klavesou nebo koncovym spinacem.
uint32_t move_steps(uint32_t steps, bool dir_b, int rate, bool ramp_in,
                    bool ramp_out, bool quiet) {
  if (steps == 0) return 0;
  if (endstop_hit()) {
    tk::warn("koncovy spinac motoru %u je sepnuty - jizda se nespusti", j.motor);
    return 0;
  }

  const uint32_t p_fast = period_us(rate);
  const uint32_t p_slow = period_us(rate / 4);
  const uint32_t ramp_steps = (ramp_in || ramp_out) ? (steps / 4) : 0;

  sr.set_dir(j.motor, dir_b);
  delayMicroseconds(200);  // DIR musi byt stabilni pred hranou STEP

  const uint8_t pin = PIN_STEP[j.motor];
  const uint32_t t0 = micros();
  uint32_t done = 0;
  for (uint32_t i = 0; i < steps; ++i) {
    // Rampa je linearni v periode, ne v rychlosti - na proklepnuti rozjezdu to
    // staci a vejde se do celociselne aritmetiky.
    uint32_t p = p_fast;
    if (ramp_steps) {
      uint32_t d = ramp_steps;
      if (ramp_in && i < ramp_steps) d = i;
      if (ramp_out && i >= steps - ramp_steps) d = steps - 1 - i;
      if (d < ramp_steps) {
        p = p_slow - static_cast<uint32_t>(
                         (static_cast<uint64_t>(p_slow - p_fast) * d) /
                         ramp_steps);
      }
    }
    const uint32_t half = (p > 2) ? (p / 2) : 1;

    digitalWrite(pin, HIGH);
    delayMicroseconds(half);
    digitalWrite(pin, LOW);
    delayMicroseconds(half);
    ++done;

    if ((i & 0x0F) == 0x0F) {
      if (endstop_hit()) {
        tk::warn("koncovy spinac sepnul - stop po %lu krocich",
                 static_cast<unsigned long>(done));
        break;
      }
      if (tk::key_pressed()) {
        tk::warn("preruseno klavesou po %lu krocich",
                 static_cast<unsigned long>(done));
        break;
      }
    }
  }
  const uint32_t dt_ms = (micros() - t0) / 1000;

  j.pos += dir_b ? -static_cast<int32_t>(done) : static_cast<int32_t>(done);
  ++j.moves;

  if (!quiet) {
    const unsigned long sps =
        dt_ms ? static_cast<unsigned long>(static_cast<uint64_t>(done) *
                                           1000ULL / dt_ms)
              : 0UL;
    tk::info("smer %s: %lu kroku za %lu ms (~%lu kroku/s), poloha %+ld",
             dir_b ? "B" : "A", static_cast<unsigned long>(done),
             static_cast<unsigned long>(dt_ms), sps,
             static_cast<long>(j.pos));
  }
  check_status(false);
  return done;
}

// Symetricky pohyb: kdyz motor neztraci kroky, hridel skonci presne tam, kde
// zacala. Rozjezd i dojezd s rampou, aby se netestoval jen trhany start.
void back_and_forth(int cycles) {
  const int32_t start = j.pos;
  for (int k = 0; k < cycles; ++k) {
    tk::info("cyklus %d/%d, %lu kroku tam a zpet", k + 1, cycles,
             static_cast<unsigned long>(j.steps));
    if (move_steps(j.steps, false, j.rate, true, true, false) < j.steps) break;
    delay(300);
    if (move_steps(j.steps, true, j.rate, true, true, false) < j.steps) break;
    delay(300);
  }
  if (j.pos == start) {
    tk::info("citac polohy je zpatky na %+ld - zkontroluj znacku na hrideli",
             static_cast<long>(start));
  } else {
    tk::warn("citac polohy skoncil na %+ld misto %+ld - jizda byla prerusena",
             static_cast<long>(j.pos), static_cast<long>(start));
  }
}

// Plynula jizda po davkach nastaveneho poctu kroku, dokud nekdo nestisne
// klavesu (nebo dokud nesepne koncovy spinac).
void run_continuous(bool dir_b) {
  tk::info("plynula jizda smer %s, %d kroku/s - ukonci libovolnou klavesou",
           dir_b ? "B" : "A", j.rate);
  tk::flush_input();
  uint32_t batches = 0;
  for (;;) {
    // Rampa jen na prvni davce (mekky rozjezd), dal uz konstantni rychlost.
    if (move_steps(j.steps, dir_b, j.rate, batches == 0, false, true) <
        j.steps) {
      break;
    }
    ++batches;
    tk::info("  %lu x %lu kroku, poloha %+ld",
             static_cast<unsigned long>(batches),
             static_cast<unsigned long>(j.steps), static_cast<long>(j.pos));
  }
  tk::info("plynula jizda skoncila po %lu celych davkach",
           static_cast<unsigned long>(batches));
}

// Rozjezd na dvojnasobek nastavene rychlosti a dojezd zpatky, na trojnasobku
// poctu kroku. Kdyz motor vynechava az od nejake rychlosti, ukaze se to tady.
void ramp_demo() {
  const int fast = clamp_rate(j.rate * 2);
  tk::info("rampa: rozjezd z %d na %d kroku/s a zpatky, %lu kroku smer A",
           clamp_rate(fast / 4), fast,
           static_cast<unsigned long>(j.steps * 3));
  move_steps(j.steps * 3, false, fast, true, true, false);
}

// Nastavi driver pro jog: UART ma prednost pred piny (pdn_disable),
// mikrokrok z registru (mstep_reg_select - MS1/MS2 tady delaji adresu),
// proud a zpozdeni do klidoveho proudu.
bool configure() {
  bus.write_reg(j.node, tk::TMC_GCONF, (1UL << 6) | (1UL << 7));
  const uint32_t ihold = static_cast<uint32_t>(j.irun) / 2;
  bus.write_reg(j.node, tk::TMC_IHOLD_IRUN,
                ihold | (static_cast<uint32_t>(j.irun) << 8) | (6UL << 16));
  bus.write_reg(j.node, tk::TMC_TPOWERDOWN, 20);

  uint32_t chop = 0;
  if (!bus.read_reg(j.node, tk::TMC_CHOPCONF, &chop)) {
    tk::fail("CHOPCONF nelze precist - BOB-%u neodpovida, spust T06", j.motor);
    return false;
  }
  const uint8_t code = mres_code(j.usteps);
  chop = (chop & ~(0xFUL << 24)) | (static_cast<uint32_t>(code) << 24);
  bus.write_reg(j.node, tk::TMC_CHOPCONF, chop);

  uint32_t back = 0;
  if (!bus.read_reg(j.node, tk::TMC_CHOPCONF, &back) ||
      ((back >> 24) & 0xF) != code) {
    tk::fail("MRES se nezapsal (CHOPCONF=0x%08lX), mikrokrok neni %u",
             static_cast<unsigned long>(back), j.usteps);
    return false;
  }

  uint32_t ifcnt = 0;
  if (!bus.read_reg(j.node, tk::TMC_IFCNT, &ifcnt)) {
    tk::fail("IFCNT nelze precist - zapisy pravdepodobne neprosly");
    return false;
  }
  tk::pass("driver %u nastaven: %u mikrokroku, %u mA RMS (CS=%d), IFCNT=%lu",
           j.motor, j.usteps, tk::ma_from_cs(static_cast<uint8_t>(j.irun)),
           j.irun, static_cast<unsigned long>(ifcnt));
  return true;
}

void switch_motor() {
  sr.set_enable(j.motor, false);  // stary driver nejdriv pust
  const int m = tk::read_int("ktery motor", 0, 3, j.motor);
  j.motor = static_cast<uint8_t>(m);
  j.node = TMC_ADDR[j.motor];
  j.pos = 0;

  uint32_t ioin = 0;
  if (!bus.read_reg(j.node, tk::TMC_IOIN, &ioin)) {
    tk::fail("BOB-%u (addr %u) neodpovida - spust T06", j.motor, j.node);
    tk::warn("driver zustava zakazany, motor se nerozjede");
    return;
  }
  if (!configure()) {
    tk::warn("driver zustava zakazany, motor se nerozjede");
    return;
  }

  j.guard = digitalRead(PIN_ENDSWITCH[j.motor]) != LOW;
  if (!j.guard) {
    tk::warn("koncovy spinac motoru %u je v klidu sepnuty - hlidani vypnuto",
             j.motor);
  }
  sr.set_enable(j.motor, true);
  delay(10);
  tk::info("motor %u povolen, citac polohy vynulovan", j.motor);
}

void status_dump() {
  uint32_t v = 0;
  if (bus.read_reg(j.node, tk::TMC_IOIN, &v)) {
    tk::print_ioin(v);
  } else {
    tk::warn("IOIN nelze precist");
  }
  check_status(true);
  if (bus.read_reg(j.node, tk::TMC_IFCNT, &v)) {
    tk::info("IFCNT=%lu, selhanych cteni na sbernici: %lu",
             static_cast<unsigned long>(v),
             static_cast<unsigned long>(bus.errors()));
  }
  tk::info("motor %u (addr %u), %lu kroku na pohyb, %d kroku/s, %u mA RMS "
           "(CS=%d), %u mikrokroku, poloha %+ld",
           j.motor, j.node, static_cast<unsigned long>(j.steps), j.rate,
           tk::ma_from_cs(static_cast<uint8_t>(j.irun)), j.irun, j.usteps,
           static_cast<long>(j.pos));
  tk::info("koncovy spinac IO%u = %s%s", PIN_ENDSWITCH[j.motor],
           digitalRead(PIN_ENDSWITCH[j.motor]) ? "H (rozepnuto)" : "L (sepnuto)",
           j.guard ? "" : "   [hlidani vypnuto]");
}

void print_help() {
  Serial.println();
  Serial.println(F("  prikazy (jeden znak + Enter):"));
  Serial.println(F("   f / b    ujede nastaveny pocet kroku smer A / smer B"));
  Serial.println(F("   F / B    jeden krok smer A / smer B (dokrokovani)"));
  Serial.println(F("   n        zadej pocet kroku a smer jednorazove"));
  Serial.println(F("   d        zmen pocet kroku na jeden pohyb"));
  Serial.println(F("   v        zmen rychlost [kroku/s]"));
  Serial.println(F("   + / -    rychlost o 25 % vys / niz"));
  Serial.println(F("   t        tam a zpet 3x - vule a ztracene kroky"));
  Serial.println(F("   c / C    plynula jizda smer A / B, konec klavesou"));
  Serial.println(F("   s        rampa: rozjezd na 2x rychlost a dojezd"));
  Serial.println(F("   a        zmen proud [mA]"));
  Serial.println(F("   u        zmen mikrokrok (1-256)"));
  Serial.println(F("   m        zmen motor (driver se prepne bezpecne)"));
  Serial.println(F("   0        vynuluj citac polohy (tady je start)"));
  Serial.println(F("   i        stav driveru a koncoveho spinace"));
  Serial.println(F("   ?        tato napoveda"));
  Serial.println(F("   r        konec a znovu od zacatku (nove parametry)"));
  Serial.println(F("   x        konec - driver se zakaze a test se vyhodnoti"));
  Serial.println();
}

// Jeden znak z konzole. Zbytek radky (CR/LF) se zahodi, aby dalsi vyzva
// necetla ohlodky predchoziho prikazu.
char read_cmd() {
  Serial.print(F("jog> "));
  tk::flush_input();
  while (!Serial.available()) {
    delay(10);
  }
  const char c = static_cast<char>(Serial.read());
  delay(20);
  tk::flush_input();
  if (c != '\r' && c != '\n') {
    Serial.println(c);
  } else {
    Serial.println();
  }
  return c;
}

void set_usteps(int want) {
  const uint16_t p = round_pow2(want);
  if (p != static_cast<uint16_t>(want)) {
    tk::info("mikrokrok zaokrouhlen nahoru na %u (TMC2209 umi jen 2^n)", p);
  }
  j.usteps = p;
}

// Jeden beh testu, od zadani parametru po vyhodnoceni. Vraci true, kdyz si
// operator vyzadal "konec a znovu od zacatku" - viz prikaz 'r'.
bool jog_session() {
  tk::reset_results();
  tk::banner("T09 - pusteni motoru a jednoduche pohyby");

  j = Jog();
  bool restart = false;

  sr.begin();
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PIN_STEP[i], OUTPUT);
    digitalWrite(PIN_STEP[i], LOW);
    pinMode(PIN_ENDSWITCH[i], INPUT);  // externi 10k pull-up na desce
  }
  bus.begin(115200);

  tk::section("parametry");
  // Vychozi je slot 03 (ctvrty motor) - na nem se testuje jako na prvnim.
  j.motor = static_cast<uint8_t>(tk::read_int("ktery motor", 0, 3, 3));
  j.node = TMC_ADDR[j.motor];
  {
    const int ma = tk::read_int("proud pri jizde [mA RMS]", 55, 1768, 500);
    j.irun = tk::cs_from_ma(static_cast<uint16_t>(ma));
    tk::info("%d mA -> CS=%d (skutecne %u mA RMS)", ma, j.irun,
             tk::ma_from_cs(static_cast<uint8_t>(j.irun)));
  }
  set_usteps(tk::read_int("mikrokroku na plny krok (1-256)", 1, 256, 16));
  j.steps = static_cast<uint32_t>(
      tk::read_int("kroku na jeden pohyb", 1, 200000, 400));
  set_rate(tk::read_int("kroku za sekundu", 10, 20000, 800));
  tk::info("pri 200 plnych krocich na otacku je otacka %lu kroku",
           static_cast<unsigned long>(200UL * j.usteps));

  tk::section("kontrola pred pustenim");
  uint32_t ioin = 0;
  if (!bus.read_reg(j.node, tk::TMC_IOIN, &ioin)) {
    tk::fail("BOB-%u (addr %u) neodpovida - spust T06", j.motor, j.node);
    tk::summary();
    return tk::prompt_yes_no("Zkusit znovu od zacatku?");
  }
  tk::print_ioin(ioin);

  uint32_t st = 0;
  if (bus.read_reg(j.node, tk::TMC_DRV_STATUS, &st)) {
    tk::print_drv_status(st);
    if (st & (1u << 1)) {
      tk::fail("driver hlasi prehrati - nechej vychladnout");
      tk::summary();
      return tk::prompt_yes_no("Zkusit znovu od zacatku?");
    }
  }

  j.guard = digitalRead(PIN_ENDSWITCH[j.motor]) != LOW;
  if (j.guard) {
    tk::info("koncovy spinac IO%u je rozepnuty - jizdu zastavi, kdyz sepne",
             PIN_ENDSWITCH[j.motor]);
  } else {
    tk::warn("koncovy spinac IO%u je v klidu sepnuty (nebo nezapojeny bez "
             "pull-upu) - hlidani vypnuto, jedina pojistka je klavesa",
             PIN_ENDSWITCH[j.motor]);
  }

  tk::section("konfigurace driveru");
  if (!configure()) {
    tk::summary();
    return tk::prompt_yes_no("Zkusit znovu od zacatku?");
  }

  tk::warn("motor %u se rozjede az na prikaz, ale pak i na %lu kroku v kuse",
           j.motor, static_cast<unsigned long>(j.steps * 3));
  if (!tk::prompt_yes_no("Je mechanika volna a smi se motor rozjet?")) {
    tk::warn("zruseno operatorem");
    tk::summary();
    return tk::prompt_yes_no("Zadat parametry znovu od zacatku?");
  }

  tk::section("jog");
  sr.set_enable(j.motor, true);
  delay(10);
  tk::info("driver %u povolen - do motoru tece klidovy proud ~%u mA RMS "
           "(IHOLD=%d), hridel je drzena",
           j.motor, tk::ma_from_cs(static_cast<uint8_t>(j.irun / 2)),
           j.irun / 2);
  tk::info("udelej si znacku na hrideli, at je videt, kde je start");
  print_help();

  bool running = true;
  while (running) {
    const char c = read_cmd();
    switch (c) {
      case 'f':
        move_steps(j.steps, false, j.rate, false, false, false);
        break;
      case 'b':
        move_steps(j.steps, true, j.rate, false, false, false);
        break;
      case 'F':
        move_steps(1, false, j.rate, false, false, false);
        break;
      case 'B':
        move_steps(1, true, j.rate, false, false, false);
        break;
      case 'n': {
        const uint32_t steps = static_cast<uint32_t>(
            tk::read_int("pocet kroku", 1, 1000000, static_cast<int>(j.steps)));
        const bool dir_b = !tk::prompt_yes_no("smer A? (n = smer B)");
        move_steps(steps, dir_b, j.rate, true, true, false);
        break;
      }
      case 'd':
        j.steps = static_cast<uint32_t>(tk::read_int(
            "kroku na jeden pohyb", 1, 200000, static_cast<int>(j.steps)));
        tk::info("pocet kroku na pohyb: %lu",
                 static_cast<unsigned long>(j.steps));
        break;
      case 'v':
        set_rate(tk::read_int("kroku za sekundu", 10, 20000, j.rate));
        break;
      case '+':
        set_rate(j.rate + j.rate / 4 + 1);
        break;
      case '-':
        set_rate(j.rate - j.rate / 4 - 1);
        break;
      case 't':
        back_and_forth(3);
        break;
      case 'c':
        run_continuous(false);
        break;
      case 'C':
        run_continuous(true);
        break;
      case 's':
        ramp_demo();
        break;
      case 'a': {
        const int ma_now =
            static_cast<int>(tk::ma_from_cs(static_cast<uint8_t>(j.irun)));
        const int ma = tk::read_int("proud pri jizde [mA RMS]", 55, 1768, ma_now);
        j.irun = tk::cs_from_ma(static_cast<uint16_t>(ma));
        tk::info("%d mA -> CS=%d (skutecne %u mA RMS)", ma, j.irun,
                 tk::ma_from_cs(static_cast<uint8_t>(j.irun)));
        configure();
        break;
      }
      case 'u':
        set_usteps(tk::read_int("mikrokroku na plny krok (1-256)", 1, 256,
                                j.usteps));
        configure();
        break;
      case 'm':
        switch_motor();
        break;
      case '0':
        j.pos = 0;
        tk::info("citac polohy vynulovan - tady je start");
        break;
      case 'i':
        status_dump();
        break;
      case 'h':
      case '?':
        print_help();
        break;
      case 'r':
      case 'R':
        restart = true;
        running = false;
        break;
      case 'x':
      case 'X':
        running = false;
        break;
      case '\r':
      case '\n':
        break;
      default:
        tk::info("neznamy prikaz '%c', '?' vypise napovedu", c);
        break;
    }
  }

  sr.set_enable(j.motor, false);
  sr.begin();  // SR_ALL_DISABLED, STEP uz je v L

  tk::section("vyhodnoceni");
  if (j.moves == 0) {
    tk::warn("nebyl zadan zadny pohyb - test nic neoveril");
    tk::summary();
    return restart;
  }
  tk::info("%lu pohybu, koncova poloha %+ld kroku od startu",
           static_cast<unsigned long>(j.moves), static_cast<long>(j.pos));

  if (!j.driver_error) {
    tk::pass("DRV_STATUS po celou dobu bez chyb");
  }

  // Pri 'r' se otazky na operatora preskakuji - jde o pokracovani v ladeni,
  // ne o zaver testu.
  if (restart) {
    tk::info("znovu od zacatku, driver je zatim zakazany");
    tk::summary();
    return true;
  }

  if (tk::prompt_yes_no("Bezel motor plynule, bez tikani a preskakovani?")) {
    tk::pass("motor %u jede plynule pri %d krocich/s, %u mA RMS, %u mikrokroku",
             j.motor, j.rate, tk::ma_from_cs(static_cast<uint8_t>(j.irun)),
             j.usteps);
  } else {
    tk::fail("motor %u nejede plynule - zkus nizsi rychlost, vyssi proud nebo "
             "vic mikrokroku",
             j.motor);
  }

  if (j.pos == 0) {
    if (tk::prompt_yes_no("Citac polohy je na nule. Je znacka na hrideli "
                          "zpatky na startu?")) {
      tk::pass("motor neztraci kroky - hridel se vratila na start");
    } else {
      tk::fail("citac je na nule, ale hridel neni na startu - motor ztracel "
               "kroky (maly proud, vysoka rychlost, tuha mechanika)");
    }
  } else {
    tk::info("citac polohy neni na nule, symetricky pohyb se nedojel - "
             "ztracene kroky tenhle beh neoveril");
  }

  tk::summary();
  return false;
}

}  // namespace

void t09_motor_jog() {
  // 'r' v jog rezimu znamena "konec a znovu": parametry se zadaji od zacatku,
  // takze jina rychlost, proud i motor jdou zkusit bez vyskoku do menu.
  while (jog_session()) {
  }
}
