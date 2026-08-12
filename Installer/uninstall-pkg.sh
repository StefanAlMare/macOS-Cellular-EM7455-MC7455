#!/bin/zsh
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo "$0" "$@"
fi

SUPPORT="/Library/Application Support/CellularLTE"
ENGINE="/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"

echo "Dezinstalez Cellular..."

launchctl bootout system/ro.alexd.CellularLTE.Helper 2>/dev/null || true

PIDS="$(pgrep -f "^${ENGINE}$" 2>/dev/null || true)"
for PID in ${(f)PIDS}; do
    kill -USR1 "$PID" 2>/dev/null || true
done
sleep 1

pkill -TERM -x Cellular 2>/dev/null || true

rm -rf "/Applications/Cellular.app"
rm -rf "/Applications/Cellular LTE.app"

rm -f "/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"
rm -f "/Library/PrivilegedHelperTools/ro.alexd.em7455_status"
rm -f "/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper"

rm -f "/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist"
rm -f "/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"

rm -rf "$SUPPORT"

pkgutil --forget ro.alexd.Cellular >/dev/null 2>&1 || true

echo "Cellular a fost dezinstalat."
