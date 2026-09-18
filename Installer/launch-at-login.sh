#!/bin/zsh
set -u

DOMAIN="ro.alexd.CellularLTE"
KEY="LaunchAtLogin"

# Missing preference preserves the historical default: enabled.
VALUE="$(/usr/bin/defaults read "$DOMAIN" "$KEY" 2>/dev/null || true)"

case "$VALUE" in
    0|false|FALSE|no|NO)
        exit 0
        ;;
esac

exec /usr/bin/open -g "/Applications/Cellular.app"
