#!/usr/bin/env bash
# flash.sh - nahrani firmware do desky esp32stepper (ESP32-S3-WROOM-2 N32R16V).
#
# Proc to nejde pres "pio run -t upload" a co delat, kdyz to nejde vubec, je
# ve FLASH.md (HW-00174) a report.md (HW-00173).
#
# Skript je zamerne IDEMPOTENTNI a nic nepredpoklada o stroji: sam si najde
# pio i esptool, na novem stroji je dotahne, na uz pouzitem jen zkontroluje
# verzi. Nic nemaze a nesaha na efuse.
#
# Pouziti:
#   firmware/flash.sh                                  # projekt = aktualni adresar, env = default_envs
#   firmware/flash.sh -d firmware/testing_firmware     # projekt explicitne
#   firmware/flash.sh -d firmware/test_simple -e usb_j5
#   firmware/flash.sh -p /dev/ttyACM1                  # kdyz je pripojenych vic desek
#   firmware/flash.sh --check                          # jen diagnostika flash, NIC nezapisuje
#   firmware/flash.sh --no-build                       # pouzij uz prelozeny build
#   firmware/flash.sh --help                           # tenhle text + co ktery test dela
#   firmware/flash.sh --list                           # jen seznam prostredi a testu
#
# --help i --list vypisou prostredi, ktera jde dat za -e, a u testu i to, co
# overuji a co k tomu musi byt pripojene - tedy to same, co vypise menu na
# konzoli desky. Bez -d se to vezme z aktualniho adresare, jinak ze vsech
# projektu vedle skriptu.
#
# Prepsat cestu k venv jde promennou ESPTOOL_VENV.

set -euo pipefail

ESPTOOL_MIN="5.3.1"
VENV="${ESPTOOL_VENV:-$HOME/venv-esptool}"
PIO_CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT=""
ENVNAME=""
PORT=""
DO_BUILD=1
CHECK_ONLY=0
SHOW_HELP=0
SHOW_LIST=0

die()  { printf '\n[flash.sh] CHYBA: %s\n' "$*" >&2; exit 1; }
note() { printf '[flash.sh] %s\n' "$*"; }

while [ $# -gt 0 ]; do
  case "$1" in
    -d|--dir)    PROJECT="${2:?-d chce adresar}"; shift 2 ;;
    -e|--env)    ENVNAME="${2:?-e chce nazev prostredi}"; shift 2 ;;
    -p|--port)   PORT="${2:?-p chce zarizeni}"; shift 2 ;;
    --no-build)  DO_BUILD=0; shift ;;
    --check)     CHECK_ONLY=1; DO_BUILD=0; shift ;;
    -h|--help)   SHOW_HELP=1; shift ;;
    -l|--list)   SHOW_LIST=1; shift ;;
    *)           die "neznamy prepinac '$1', viz --help" ;;
  esac
done

# --- napoveda a seznam prostredi -------------------------------------------
# Nic se nikde nedrzi podruhe: prostredi se ctou z platformio.ini a popisy
# testu ze src/test_registry.cpp, tedy z tehoz zdroje, ze ktereho je vypisuje
# menu na konzoli desky. Kdyz se prida test, vypis se zmeni sam.

default_env() {  # $1 = platformio.ini
  sed -n 's/^[[:space:]]*default_envs[[:space:]]*=[[:space:]]*//p' "$1" \
    | head -1 | tr -d '[:space:]' | cut -d, -f1
}

env_list() {  # $1 = platformio.ini
  sed -n 's/^\[env:\([^]]*\)\].*/\1/p' "$1"
}

# "jmeno<TAB>co overuje<TAB>co potrebuje<TAB>hybe motorem (1/0)" pro kazdy
# radek registru testu. Zaznamy jsou pres vic radku, proto se soubor nejdriv
# slepi do jednoho retezce a pak se z nej berou nejvnitrnejsi { ... }.
registry_table() {  # $1 = adresar projektu
  local reg="$1/src/test_registry.cpp"
  [ -f "$reg" ] || return 0
  awk '
    /^[[:space:]]*\/\// { next }
    { buf = buf " " $0 }
    END {
      while (match(buf, /\{[^{}]*\}/)) {
        rec = substr(buf, RSTART + 1, RLENGTH - 2)
        buf = substr(buf, RSTART + RLENGTH)
        n = 0
        while (match(rec, /"[^"]*"/)) {
          f[++n] = substr(rec, RSTART + 1, RLENGTH - 2)
          rec = substr(rec, RSTART + RLENGTH)
        }
        if (n >= 3) printf "%s\t%s\t%s\t%s\n", f[1], f[2], f[3], (rec ~ /true/ ? 1 : 0)
      }
    }' "$reg"
}

# Prostredi, ktere neni test (napr. "menu"), popisuje prvni radek komentare
# tesne nad jeho sekci v platformio.ini.
env_comment() {  # $1 = platformio.ini, $2 = env
  awk -v want="$2" '
    /^[[:space:]]*;/ {
      line = $0
      sub(/^[[:space:]]*;[[:space:]]*/, "", line)
      sub(/[[:space:]]+$/, "", line)
      if (first == "" && line !~ /^-*$/) first = line
      next
    }
    $0 ~ ("^\\[env:" want "\\][[:space:]]*$") { print first; exit }
    { first = "" }
  ' "$1"
}

print_project_envs() {  # $1 = adresar projektu
  local ini="$1/platformio.ini"
  local def name desc needs moves mark flag
  def="$(default_env "$ini")"

  local -A DESC NEEDS MOVES
  while IFS="$(printf '\t')" read -r name desc needs moves; do
    if [ -n "$name" ]; then
      DESC["$name"]="$desc"
      NEEDS["$name"]="$needs"
      MOVES["$name"]="$moves"
    fi
  done < <(registry_table "$1")

  printf '\n%s\n' "prostredi pro -e v $1:"
  while read -r name; do
    [ -n "$name" ] || continue
    mark="  "
    if [ "$name" = "$def" ]; then mark=" *"; fi
    if [ -n "${DESC[$name]:-}" ]; then
      printf '%s %-20s %s\n' "$mark" "$name" "${DESC[$name]}"
      flag=""
      if [ "${MOVES[$name]:-0}" = "1" ]; then flag="   POZOR: hybe motorem"; fi
      printf '   %-20s   potreba: %s%s\n' "" "${NEEDS[$name]}" "$flag"
    else
      printf '%s %-20s %s\n' "$mark" "$name" "$(env_comment "$ini" "$name")"
    fi
  done < <(env_list "$ini")
  printf '   %s\n' "* = default_envs, pouzije se bez -e"
}

if [ "$SHOW_HELP" -eq 1 ] || [ "$SHOW_LIST" -eq 1 ]; then
  if [ "$SHOW_HELP" -eq 1 ]; then
    awk 'NR>1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
  fi
  if [ -n "$PROJECT" ] && [ -f "$PROJECT/platformio.ini" ]; then
    print_project_envs "$(cd "$PROJECT" && pwd)"
  elif [ -f platformio.ini ]; then
    print_project_envs "$PWD"
  else
    for ini in "$SCRIPT_DIR"/*/platformio.ini; do
      [ -f "$ini" ] && print_project_envs "$(dirname "$ini")"
    done
  fi
  exit 0
fi

# --- projekt ---------------------------------------------------------------
# Bez -d se bere aktualni adresar; kdyz v nem platformio.ini neni, vypis
# projekty, ktere lezi vedle skriptu, misto nic nerikajiciho selhani.
if [ -z "$PROJECT" ]; then
  if [ -f platformio.ini ]; then
    PROJECT="$PWD"
  elif [ "$CHECK_ONLY" -eq 0 ]; then
    printf '[flash.sh] v %s neni platformio.ini. Projekty v repu:\n' "$PWD" >&2
    for ini in "$SCRIPT_DIR"/*/platformio.ini; do
      [ -f "$ini" ] && printf '    -d %s\n' "$(dirname "$ini")" >&2
    done
    die "vyber projekt prepinacem -d (nebo spust skript z jeho adresare)"
  fi
fi
if [ -n "$PROJECT" ]; then
  [ -d "$PROJECT" ] || die "adresar '$PROJECT' neexistuje"
  PROJECT="$(cd "$PROJECT" && pwd)"
  [ -f "$PROJECT/platformio.ini" ] || die "v '$PROJECT' neni platformio.ini"
fi

# --- pio -------------------------------------------------------------------
# Na cistem stroji po instalaci PlatformIO IDE neni pio v PATH, zije jen
# v penv. Zkousi se oboji, PATH ma prednost.
PIO=""
if command -v pio >/dev/null 2>&1; then
  PIO="$(command -v pio)"
elif [ -x "$PIO_CORE/penv/bin/pio" ]; then
  PIO="$PIO_CORE/penv/bin/pio"
fi
if [ "$DO_BUILD" -eq 1 ] && [ -z "$PIO" ]; then
  die "pio nenalezeno (ani v PATH, ani v $PIO_CORE/penv/bin/pio).
       Nainstaluj PlatformIO Core: python3 -m pip install --user platformio
       Nebo prelozi build v VSCode a spust skript s --no-build."
fi

# --- prostredi (env) -------------------------------------------------------
if [ -z "$ENVNAME" ] && [ -n "$PROJECT" ]; then
  ENVNAME="$(default_env "$PROJECT/platformio.ini")"
  [ -n "$ENVNAME" ] && note "prostredi z default_envs: $ENVNAME"
fi
if [ "$CHECK_ONLY" -eq 0 ] && [ -z "$ENVNAME" ]; then
  die "projekt nema default_envs, zadej prostredi prepinacem -e"
fi

# --- esptool 5.3.1+ --------------------------------------------------------
# esptool 4.5.1 zabudovany v PlatformIO na teto desce pada na BrokenPipeError,
# proto samostatny venv. Verze se kontroluje pri kazdem spusteni - na stroji,
# kde venv uz je, muze byt stary.
esptool_version() {
  [ -x "$VENV/bin/python" ] || return 1
  "$VENV/bin/python" -m esptool version 2>/dev/null \
    | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1
}
version_ok() {  # $1 >= $ESPTOOL_MIN ?
  [ -n "$1" ] && [ "$(printf '%s\n%s\n' "$ESPTOOL_MIN" "$1" | sort -V | head -1)" = "$ESPTOOL_MIN" ]
}

HAVE="$(esptool_version || true)"
if ! version_ok "$HAVE"; then
  if [ -z "$HAVE" ]; then
    note "esptool venv v $VENV chybi nebo je rozbity, zakladam ho"
  else
    note "esptool $HAVE je starsi nez $ESPTOOL_MIN, aktualizuji"
  fi
  [ -x "$VENV/bin/python" ] || python3 -m venv "$VENV" \
    || die "nepodarilo se vytvorit venv v $VENV (chybi python3-venv?)"
  "$VENV/bin/python" -m pip install --quiet --upgrade pip \
    || die "pip se nepodarilo aktualizovat (offline stroj? nakopiruj venv z jineho)"
  "$VENV/bin/python" -m pip install --quiet --upgrade "esptool>=$ESPTOOL_MIN" \
    || die "instalace esptoolu selhala (offline stroj? nakopiruj venv z jineho)"
  HAVE="$(esptool_version || true)"
  version_ok "$HAVE" || die "ve venv je esptool '$HAVE', potreba je $ESPTOOL_MIN+"
fi
note "esptool $HAVE ($VENV)"
ESPTOOL=("$VENV/bin/python" -m esptool)

# --- port ------------------------------------------------------------------
# Deska se hlasi jako nativni USB-Serial/JTAG, tedy /dev/ttyACM*. Pri jedne
# pripojene desce se bere automaticky, pri vic se musi rict ktera.
if [ -z "$PORT" ]; then
  mapfile -t PORTS < <(ls /dev/ttyACM* 2>/dev/null || true)
  case "${#PORTS[@]}" in
    0) die "zadne /dev/ttyACM* nenalezeno. Pripojena USB1 na desce? Bezi na desce
       firmware s nativnim USB CDC, nebo je cip v bootloaderu?" ;;
    1) PORT="${PORTS[0]}" ;;
    *) die "pripojeno vic zarizeni (${PORTS[*]}), vyber jedno prepinacem -p" ;;
  esac
fi
[ -e "$PORT" ] || die "port '$PORT' neexistuje"
if [ ! -r "$PORT" ] || [ ! -w "$PORT" ]; then
  die "na '$PORT' nejsou prava. Pridej se do skupiny dialout a odhlas/prihlas se:
       sudo usermod -aG dialout \$USER"
fi
note "port $PORT"

# --- build -----------------------------------------------------------------
# Zamerne BEZ -t upload: nahrava se az nize samostatnym esptoolem.
BUILD_DIR="$PROJECT/.pio/build/$ENVNAME"
if [ "$DO_BUILD" -eq 1 ]; then
  note "build: pio run -e $ENVNAME"
  ( cd "$PROJECT" && "$PIO" run -e "$ENVNAME" )
fi

# --- diagnostika flash pred zapisem ---------------------------------------
# Kdyz flash zustane zaseknuta v OPI rezimu, zapis tise selze - lepsi to
# zachytit tady a rict co s tim, nez to lovit ve vypisu write-flash.
note "kontrola flash"
FLASH_ID="$( "${ESPTOOL[@]}" --port "$PORT" --before usb-reset --connect-attempts 5 \
             flash-id 2>&1 | tee /dev/stderr || true )"

if printf '%s' "$FLASH_ID" | grep -qiE 'Manufacturer: *00|Failed to communicate with the flash'; then
  die "flash neodpovida (zaseknuta v OPI rezimu).
       Reset pres EN flash die NERESETUJE. Odpoj USB *i* 24V barrel jack,
       pockej ~10 s na vybiti kondenzatoru, zapoj jen USB a spust znovu."
fi
if ! printf '%s' "$FLASH_ID" | grep -qi 'Manufacturer: *c2'; then
  note "POZOR: neocekavany vyrobce flash, cekano c2 (Macronix MX25UM25645G)."
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
  note "--check: hotovo, nic se nezapisovalo"
  exit 0
fi

# --- soubory k nahrani -----------------------------------------------------
BOOT_APP0=""
for cand in "$PIO_CORE"/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin; do
  [ -f "$cand" ] && BOOT_APP0="$cand" && break
done
[ -n "$BOOT_APP0" ] || die "boot_app0.bin nenalezen v $PIO_CORE/packages/framework-arduinoespressif32*/
       Prelozil se build aspon jednou (framework se stahuje az pri buildu)?"

for f in bootloader.bin partitions.bin firmware.bin; do
  [ -f "$BUILD_DIR/$f" ] || die "chybi $BUILD_DIR/$f - prelozi build (bez --no-build)"
done

# --- zapis -----------------------------------------------------------------
# "keep" u mode/freq/size je dulezite: necha hlavicku z buildu byt, aby ji
# esptool neprepsal na jinou velikost nebo rezim flash.
note "zapis do flash"
"${ESPTOOL[@]}" --port "$PORT" --baud 460800 \
    --before usb-reset --connect-attempts 5 \
    write-flash --flash-mode keep --flash-freq keep --flash-size keep \
    0x0     "$BUILD_DIR/bootloader.bin" \
    0x8000  "$BUILD_DIR/partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$BUILD_DIR/firmware.bin" \
  || die "zapis selhal. Kdyz to bylo 'MD5 of file does not match' nebo 'the written
       flash region is empty', jsou nastavene block-protect bity ve flash:
         ${ESPTOOL[*]} --port $PORT read-flash-status --bytes 1
         ${ESPTOOL[*]} --port $PORT write-flash-status --bytes 1 --non-volatile 0x00
       Podrobne v report.md, problem c. 2."

if [ -n "$PIO" ]; then
  note "hotovo. Konzole: $PIO device monitor -d $PROJECT -e $ENVNAME"
else
  note "hotovo. Konzole: pio device monitor -d $PROJECT -e $ENVNAME"
fi
