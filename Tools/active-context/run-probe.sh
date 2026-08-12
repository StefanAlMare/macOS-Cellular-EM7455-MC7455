#!/bin/zsh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
USBPREFIX="$(brew --prefix libusb)"
BIN="/tmp/cellular_active_context_probe"

echo "Compilez probe-ul read-only..."
clang "$HERE/cellular_active_context_probe.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -o "$BIN"

echo
echo "Rulez cu sesiunea Cellular in starea actuala..."
echo

sudo "$BIN"

rm -f "$BIN"
