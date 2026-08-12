#!/bin/zsh
set -euo pipefail

ENGINE="/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"
OLD_ENGINE="$HOME/EM7455Dev/LTE/mbim_lte"

if /usr/bin/pgrep -f "^${ENGINE}$" >/dev/null 2>&1; then
    echo "EROARE: Cellular este conectat acum."
    echo "Porneste Ethernet sau Wi-Fi si lasa Cellular sa se deconecteze,"
    echo "apoi ruleaza din nou probe-ul."
    exit 2
fi

if /usr/bin/pgrep -f "^${OLD_ENGINE}$" >/dev/null 2>&1; then
    echo "EROARE: ruleaza motorul vechi din ~/EM7455Dev/LTE."
    exit 3
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
USBPREFIX="$(brew --prefix libusb)"
BIN="/tmp/cellular_ipv6_probe"

echo "Compilez probe-ul IPv6..."
clang "$HERE/cellular_ipv6_probe.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -lpthread \
  -framework SystemConfiguration \
  -framework CoreFoundation \
  -o "$BIN"

echo
echo "Rulez bearer-ul temporar IPv4v6..."
echo

sudo "$BIN"

rm -f "$BIN"
