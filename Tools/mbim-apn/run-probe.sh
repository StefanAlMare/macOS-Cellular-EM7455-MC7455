#!/bin/zsh
set -euo pipefail

SYSTEM_ENGINE="/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"
OLD_ENGINE="$HOME/EM7455Dev/LTE/mbim_lte"

if /usr/bin/pgrep -f "^${SYSTEM_ENGINE}$" >/dev/null 2>&1; then
    echo "EROARE: Cellular este conectat prin motorul sistem."
    echo "Porneste Ethernet/Wi-Fi si lasa Auto sa deconecteze Cellular."
    exit 2
fi

if /usr/bin/pgrep -f "^${OLD_ENGINE}$" >/dev/null 2>&1; then
    echo "EROARE: ruleaza motorul Cellular vechi din ~/EM7455Dev/LTE."
    exit 3
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
USBPREFIX="$(brew --prefix libusb)"
BIN="/tmp/mbim_apn_probe_v11"

echo "Compilez probe-ul v1.1..."
clang "$HERE/mbim_apn_probe_v11.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -lpthread \
  -framework SystemConfiguration \
  -framework CoreFoundation \
  -o "$BIN"

echo
echo "Rulez probe-ul MBIM read-only..."
echo

sudo "$BIN"

rm -f "$BIN"
