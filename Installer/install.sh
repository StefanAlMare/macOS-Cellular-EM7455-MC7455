#!/bin/zsh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$HERE/.." && pwd)"
SOURCES="$PROJECT_ROOT/Sources"
ASSETS="$PROJECT_ROOT/Assets"
BUILD="$(mktemp -d /tmp/Cellular-v261-stable-timed.XXXXXX)"

APP="/Applications/Cellular.app"
OLD_APP="/Applications/Cellular LTE.app"

SUPPORT="/Library/Application Support/CellularLTE"
COMMANDS="$SUPPORT/Commands"

PRIV="/Library/PrivilegedHelperTools"
HELPER="$PRIV/ro.alexd.CellularLTEHelper"
ENGINE="$PRIV/ro.alexd.mbim_lte"
STATUS="$PRIV/ro.alexd.em7455_status"
OLD_OPERATOR="$PRIV/ro.alexd.em7455_operator"

DAEMON="/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist"
LOGIN="$HOME/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"

OLD_ENGINE="$HOME/EM7455Dev/LTE/mbim_lte"

USER_UID="$(id -u)"
EXPECTED_SHA="8b3a8396399979c21cc39aebdae5271548352423ff14485d753a14d0d03133f3"

cleanup() {
    rm -rf "$BUILD"
}
trap cleanup EXIT

echo "=================================================="
echo " Cellular 2.6.1 STABLE TIMED - instalare"
echo "=================================================="
echo

if ! id -Gn | tr ' ' '\n' | grep -qx admin; then
    echo "EROARE: utilizatorul curent nu este in grupul admin."
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "EROARE: Homebrew nu este instalat."
    exit 1
fi

if ! brew list --versions libusb >/dev/null 2>&1; then
    echo "EROARE: libusb nu este instalat."
    echo "Ruleaza: brew install libusb"
    exit 1
fi

if ! xcrun --find swiftc >/dev/null 2>&1; then
    echo "EROARE: swiftc nu este disponibil prin Xcode."
    exit 1
fi

echo "Solicit privilegii administrator pentru instalarea sistem..."
sudo -v

USBPREFIX="$(brew --prefix libusb)"

echo
echo "1. Verific motorul Cellular 2.6.1 stabil + timing..."
ACTUAL_SHA="$(shasum -a 256 "$SOURCES/mbim_lte.c" | awk '{print $1}')"

if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
    echo "EROARE: sursa mbim_lte nu este motorul v1.5 validat."
    echo "Expected: $EXPECTED_SHA"
    echo "Actual  : $ACTUAL_SHA"
    exit 1
fi

echo "   SHA256 OK: $ACTUAL_SHA"

echo
echo "2. Oprire aplicatie/engine vechi..."

pkill -KILL -x CellularLTE 2>/dev/null || true

stop_engine_path() {
    local PATH_TO_ENGINE="$1"
    local PATTERN="^${PATH_TO_ENGINE}$"
    local PIDS

    PIDS="$(/usr/bin/pgrep -f "$PATTERN" 2>/dev/null || true)"

    [[ -z "$PIDS" ]] && return 0

    echo "   SIGUSR1 -> $PATH_TO_ENGINE"

    for PID in ${(f)PIDS}; do
        sudo kill -USR1 "$PID" 2>/dev/null || true
    done

    for i in {1..60}; do
        /usr/bin/pgrep -f "$PATTERN" >/dev/null 2>&1 || return 0
        sleep 0.25
    done

    PIDS="$(/usr/bin/pgrep -f "$PATTERN" 2>/dev/null || true)"

    if [[ -n "$PIDS" ]]; then
        echo "   SIGTERM -> $PATH_TO_ENGINE"

        for PID in ${(f)PIDS}; do
            sudo kill -TERM "$PID" 2>/dev/null || true
        done

        sleep 2
    fi

    PIDS="$(/usr/bin/pgrep -f "$PATTERN" 2>/dev/null || true)"

    if [[ -n "$PIDS" ]]; then
        echo "   SIGKILL -> $PATH_TO_ENGINE"

        for PID in ${(f)PIDS}; do
            sudo kill -KILL "$PID" 2>/dev/null || true
        done

        sleep 1
    fi
}

stop_engine_path "$OLD_ENGINE"

if [[ -x "$ENGINE" ]]; then
    stop_engine_path "$ENGINE"
fi

echo
echo "3. Curat eventuale componente experimentale v2..."

sudo launchctl bootout system/ro.alexd.CellularLTEHelper 2>/dev/null || true
sudo launchctl bootout system/ro.alexd.CellularLTE.Helper 2>/dev/null || true

launchctl bootout "gui/$USER_UID/ro.alexd.CellularLTE" 2>/dev/null || true
launchctl bootout "gui/$USER_UID/ro.alexd.CellularLTE.Menu" 2>/dev/null || true

sudo rm -f /Library/LaunchDaemons/ro.alexd.CellularLTEHelper.plist
sudo rm -f /Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper
sudo rm -f /Library/PrivilegedHelperTools/ro.alexd.em7455_operator
sudo rm -f /Library/PrivilegedHelperTools/ro.alexd.em7455_status

rm -f "$HOME/Library/LaunchAgents/ro.alexd.CellularLTE.plist"

echo
echo "4. Compilez motorul stabil v2.6 cu instrumentare timing..."

clang "$SOURCES/mbim_lte.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -lpthread \
  -framework SystemConfiguration \
  -framework CoreFoundation \
  -o "$BUILD/mbim_lte"

echo
echo "5. Compilez telemetria Cellular (operator + semnal)..."

clang "$SOURCES/em7455_status.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -o "$BUILD/em7455_status"

echo
echo "6. Compilez helper-ul automat..."

xcrun swiftc \
  -O \
  -framework Foundation \
  -framework Network \
  "$SOURCES/CellularLTEHelper.swift" \
  -o "$BUILD/CellularLTEHelper"

echo
echo "7. Compilez aplicatia menu bar..."

mkdir -p "$BUILD/Cellular.app/Contents/MacOS"
mkdir -p "$BUILD/Cellular.app/Contents/Resources"
cp "$HERE/Info.plist" "$BUILD/Cellular.app/Contents/Info.plist"
cp "$ASSETS/Cellular.icns" "$BUILD/Cellular.app/Contents/Resources/Cellular.icns"

xcrun swiftc \
  -O \
  -framework AppKit \
  -framework Foundation \
  "$SOURCES/CellularLTE.swift" \
  -o "$BUILD/Cellular.app/Contents/MacOS/Cellular"

chmod 755 "$BUILD/Cellular.app/Contents/MacOS/Cellular"

codesign \
  --force \
  --sign - \
  --timestamp=none \
  "$BUILD/Cellular.app" >/dev/null

echo
echo "8. Instalez componentele root..."

sudo mkdir -p "$PRIV"
sudo mkdir -p "$SUPPORT"
sudo mkdir -p "$COMMANDS"

sudo install -o root -g wheel -m 755 \
  "$BUILD/mbim_lte" \
  "$ENGINE"

sudo install -o root -g wheel -m 755 \
  "$BUILD/em7455_status" \
  "$STATUS"

sudo install -o root -g wheel -m 755 \
  "$BUILD/CellularLTEHelper" \
  "$HELPER"

sudo chown root:admin "$SUPPORT"
sudo chmod 0770 "$SUPPORT"

sudo chown root:admin "$COMMANDS"
sudo chmod 0770 "$COMMANDS"

for FILE in state.json config apn-cache.tsv helper.log engine.log launchd.log; do
    sudo touch "$SUPPORT/$FILE"
    sudo chown root:admin "$SUPPORT/$FILE"
done

if ! sudo grep -q '^AUTO=' "$SUPPORT/config" 2>/dev/null; then
    echo "AUTO=1" | sudo tee "$SUPPORT/config" >/dev/null
fi

sudo chmod 0644 "$SUPPORT/state.json"
sudo chmod 0644 "$SUPPORT/config"
sudo chmod 0644 "$SUPPORT/helper.log"
sudo chmod 0644 "$SUPPORT/engine.log"
sudo chmod 0644 "$SUPPORT/launchd.log"


echo
echo "8A. Seed APN cache din bearer-ul demonstrat..."

if ! sudo grep -qi $'^TELEKOM.RO\t' "$SUPPORT/apn-cache.tsv" 2>/dev/null; then
    printf 'TELEKOM.RO\tbroadband\n' | \
      sudo tee -a "$SUPPORT/apn-cache.tsv" >/dev/null
    echo "   TELEKOM.RO -> broadband"
else
    echo "   Cache TELEKOM.RO exista deja; il pastrez."
fi

sudo chown root:admin "$SUPPORT/apn-cache.tsv"
sudo chmod 0644 "$SUPPORT/apn-cache.tsv"

echo
echo "9. Instalez aplicatia in /Applications..."

sudo rm -rf "$APP"
sudo rm -rf "$OLD_APP"
sudo cp -R "$BUILD/Cellular.app" "$APP"
sudo chown -R root:wheel "$APP"

rm -rf "$HOME/Applications/Cellular LTE.app" 2>/dev/null || true
rm -rf "$HOME/Applications/Cellular.app" 2>/dev/null || true

echo
echo "10. Instalez daemonul root..."

sudo install -o root -g wheel -m 644 \
  "$HERE/ro.alexd.CellularLTE.Helper.plist" \
  "$DAEMON"

sudo launchctl bootstrap system "$DAEMON"
sudo launchctl enable system/ro.alexd.CellularLTE.Helper
sudo launchctl kickstart -k system/ro.alexd.CellularLTE.Helper

echo
echo "11. Instalez pornirea la login..."

mkdir -p "$HOME/Library/LaunchAgents"

cp "$HERE/ro.alexd.CellularLTE.Menu.plist" "$LOGIN"
chmod 0644 "$LOGIN"

launchctl bootstrap "gui/$USER_UID" "$LOGIN"
launchctl enable "gui/$USER_UID/ro.alexd.CellularLTE.Menu"

echo
echo "12. Pornesc aplicatia..."

open -g "$APP"

echo
echo "=================================================="
echo " INSTALARE TERMINATA - CELLULAR 2.6.1 STABLE TIMED"
echo "=================================================="
echo
echo "Aplicatie:"
echo "  /Applications/Cellular.app"
echo
echo "Motor validat:"
echo "  $ENGINE"
echo "  SHA256 sursa: $EXPECTED_SHA"
echo
echo "Automatic fallback:"
echo "  ON implicit"
echo "  5 sec fara Wi-Fi/Ethernet -> LTE ON"
echo "  3 sec dupa revenirea Wi-Fi/Ethernet -> LTE OFF"
echo
echo "IMPORTANT:"
echo "  Wi-Fi/Ethernet NU sunt reconfigurate."
echo "  IPv6 Wi-Fi/Ethernet NU este modificat."
echo "  LTE ramane IPv4-only in aceasta versiune."
echo
echo "Diagnosticul read-only:"
echo "  \"$HERE/diagnostic.sh\""
