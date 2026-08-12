#!/bin/zsh
set +e

echo "=================================================="
echo "CELLULAR 2.6.1 STABLE TIMED - DIAGNOSTIC READ ONLY"
echo "=================================================="
echo

echo "===== APP ====="
ls -ld "/Applications/Cellular.app" 2>&1
ps aux | grep -E '[C]ellular$|[C]ellularLTE$'
echo

echo "===== HELPER ====="
sudo launchctl print system/ro.alexd.CellularLTE.Helper 2>&1 | head -100
echo

echo "===== ENGINE ====="
ps aux | grep '[r]o.alexd.mbim_lte'
echo

echo "===== ACTIVE APN (READ ONLY) ====="
sudo /Library/PrivilegedHelperTools/ro.alexd.em7455_status active 2>&1 || true
echo

echo "===== APN CACHE ====="
cat "/Library/Application Support/CellularLTE/apn-cache.tsv" 2>&1
echo

echo "===== STATE ====="
cat "/Library/Application Support/CellularLTE/state.json" 2>&1
echo

echo "===== NETWORK PATHS ====="
scutil --nwi
echo

echo "===== SERVICE ORDER ====="
networksetup -listnetworkserviceorder
echo

echo "===== WI-FI SETTINGS ====="
WIFI_DEV="$(
networksetup -listallhardwareports |
awk '
/Hardware Port: (Wi-Fi|AirPort)/ {
    getline
    if ($1 == "Device:") {
        print $2
        exit
    }
}'
)"

if [[ -n "$WIFI_DEV" ]]; then
    echo "Wi-Fi device: $WIFI_DEV"
    networksetup -getairportpower "$WIFI_DEV"
fi
echo

echo "===== IPv4 DEFAULT / LTE ====="
route -n get default 2>&1 | grep -E 'gateway|interface'
route -n get 1.1.1.1 2>&1 | grep -E 'gateway|interface'
netstat -rn -f inet | grep -E '^default|^0/1|^128\.0/1'
echo

echo "===== IPv6 ====="
netstat -rn -f inet6 | grep -E '^default|en[0-9]|utun' | head -100
echo

echo "===== LAST HELPER LOG ====="
tail -80 "/Library/Application Support/CellularLTE/helper.log" 2>&1
echo

echo "===== LAST ENGINE LOG ====="
tail -100 "/Library/Application Support/CellularLTE/engine.log" 2>&1
echo

echo "=================================================="
echo "GATA - NIMIC NU A FOST MODIFICAT"
echo "=================================================="
