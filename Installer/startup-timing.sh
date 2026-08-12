#!/bin/zsh
set -e

SUPPORT="/Library/Application Support/CellularLTE"
ELOG="$SUPPORT/engine.log"
HLOG="$SUPPORT/helper.log"

echo "===== HELPER TIMING ====="
grep 'TIMING:' "$HLOG" 2>/dev/null | tail -20 || true

echo
echo "===== LAST ENGINE START / TIMING ====="

if [[ ! -f "$ELOG" ]]; then
    echo "engine.log nu exista."
    exit 0
fi

awk '
/===== START LTE / {
    block=""
}
{
    block = block $0 ORS
}
END {
    print block
}
' "$ELOG" | grep -E \
'===== START LTE|\[TIMING|APN CACHE HIT|APN DISCOVERY|APN FALLBACK|BEARER ACTIVATED|CONNECT QUERY|Command status|Activation state|IP CONFIGURATION REUSIT|UTUN|IPv4|Gateway|DNS ' \
|| true
