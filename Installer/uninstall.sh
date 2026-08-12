#!/bin/zsh
set -euo pipefail

APP="/Applications/Cellular.app"
OLD_APP="/Applications/Cellular LTE.app"

SUPPORT="/Library/Application Support/CellularLTE"
COMMANDS="$SUPPORT/Commands"

ENGINE="/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"
HELPER="/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper"
STATUS="/Library/PrivilegedHelperTools/ro.alexd.em7455_status"
OLD_OPERATOR="/Library/PrivilegedHelperTools/ro.alexd.em7455_operator"

DAEMON="/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist"
LOGIN="$HOME/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"

USER_UID="$(id -u)"

echo "Cellular 2.6.1 STABLE TIMED - dezinstalare"

sudo -v

pkill -KILL -x CellularLTE 2>/dev/null || true

PIDS="$(/usr/bin/pgrep -f "^${ENGINE}$" 2>/dev/null || true)"

if [[ -n "$PIDS" ]]; then
    for PID in ${(f)PIDS}; do
        sudo kill -USR1 "$PID" 2>/dev/null || true
    done

    for i in {1..60}; do
        /usr/bin/pgrep -f "^${ENGINE}$" >/dev/null 2>&1 || break
        sleep 0.25
    done
fi

launchctl bootout \
  "gui/$USER_UID/ro.alexd.CellularLTE.Menu" \
  2>/dev/null || true

rm -f "$LOGIN"

sudo launchctl bootout \
  system/ro.alexd.CellularLTE.Helper \
  2>/dev/null || true

sudo rm -f "$DAEMON"
sudo rm -f "$ENGINE"
sudo rm -f "$HELPER"
sudo rm -f "$STATUS"
sudo rm -f "$OLD_OPERATOR"

sudo rm -rf "$SUPPORT"
sudo rm -rf "$APP"
sudo rm -rf "$OLD_APP"

echo
echo "Cellular 2.6.1 STABLE TIMED a fost eliminat."
echo "Configuratia Wi-Fi/Ethernet nu a fost modificata."
echo "Folderul de rollback ~/EM7455Dev/LTE a ramas neatins."
