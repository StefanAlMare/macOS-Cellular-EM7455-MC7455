#!/bin/zsh
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
./publish-to-github.sh
STATUS=$?
echo
echo "Press Enter to close."
read
exit $STATUS
