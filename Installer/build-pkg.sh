#!/bin/zsh
set -euo pipefail

VERSION="2.6.2"
PKG_ID="ro.alexd.Cellular"
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$HERE/.." && pwd)"
SOURCES="$PROJECT_ROOT/Sources"
ASSETS="$PROJECT_ROOT/Assets"
WORK="$(mktemp -d /tmp/CellularPkgBuild.XXXXXX)"
BUILD="$WORK/build"
ROOT="$WORK/root"
SCRIPTS="$WORK/scripts"
OUTDIR="$HERE/Release"

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "=================================================="
echo " Cellular $VERSION - PACKAGE BUILDER"
echo "=================================================="
echo

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "EROARE: pachetul trebuie construit pe macOS."
    exit 1
fi

if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "ATENTIE: aceasta ramura a fost validata pe Intel/x86_64."
    echo "Build-ul continua, dar pachetul rezultat nu este declarat universal."
fi

for TOOL in xcrun clang swiftc codesign pkgbuild otool install_name_tool; do
    if ! command -v "$TOOL" >/dev/null 2>&1; then
        echo "EROARE: lipseste $TOOL."
        echo "Instaleaza Xcode Command Line Tools."
        exit 1
    fi
done

if ! command -v brew >/dev/null 2>&1; then
    echo "EROARE: Homebrew este necesar DOAR pe calculatorul de build."
    exit 1
fi

if ! brew list --versions libusb >/dev/null 2>&1; then
    echo "Instalez libusb pe calculatorul de build..."
    brew install libusb
fi

USBPREFIX="$(brew --prefix libusb)"
mkdir -p "$BUILD" "$ROOT" "$SCRIPTS" "$OUTDIR"

echo "1. Compilez motorul Cellular..."
clang -O2 -arch x86_64 "$SOURCES/mbim_lte.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -lpthread \
  -framework SystemConfiguration \
  -framework CoreFoundation \
  -o "$BUILD/ro.alexd.mbim_lte"

echo "2. Compilez telemetria modemului..."
clang -O2 -arch x86_64 "$SOURCES/em7455_status.c" \
  -I"$USBPREFIX/include/libusb-1.0" \
  -L"$USBPREFIX/lib" \
  -lusb-1.0 \
  -o "$BUILD/ro.alexd.em7455_status"

echo "3. Compilez helper-ul root..."
xcrun swiftc \
  -O \
  -target x86_64-apple-macos13.0 \
  -framework Foundation \
  -framework Network \
  "$SOURCES/CellularLTEHelper.swift" \
  -o "$BUILD/ro.alexd.CellularLTEHelper"

echo "4. Compilez Cellular.app..."
mkdir -p "$BUILD/Cellular.app/Contents/MacOS"
mkdir -p "$BUILD/Cellular.app/Contents/Resources"

cp "$HERE/Info.plist" \
   "$BUILD/Cellular.app/Contents/Info.plist"

cp "$ASSETS/Cellular.icns" \
   "$BUILD/Cellular.app/Contents/Resources/Cellular.icns"

xcrun swiftc \
  -O \
  -target x86_64-apple-macos13.0 \
  -framework AppKit \
  -framework Foundation \
  "$SOURCES/CellularLTE.swift" \
  -o "$BUILD/Cellular.app/Contents/MacOS/Cellular"

chmod 755 "$BUILD/Cellular.app/Contents/MacOS/Cellular"

echo "5. Integrez libusb in pachet (targetul nu are nevoie de Homebrew)..."

LIBUSB_SRC="$USBPREFIX/lib/libusb-1.0.0.dylib"

if [[ ! -f "$LIBUSB_SRC" ]]; then
    LIBUSB_SRC="$(find "$USBPREFIX/lib" -maxdepth 1 -name 'libusb-1.0*.dylib' | head -1)"
fi

if [[ -z "${LIBUSB_SRC:-}" || ! -f "$LIBUSB_SRC" ]]; then
    echo "EROARE: nu gasesc dylib-ul libusb."
    exit 1
fi

LIB_DEST="/Library/Application Support/CellularLTE/lib/libusb-1.0.0.dylib"
cp "$LIBUSB_SRC" "$BUILD/libusb-1.0.0.dylib"

OLD_ENGINE_LIB="$(otool -L "$BUILD/ro.alexd.mbim_lte" | awk '/libusb/{print $1; exit}')"
OLD_STATUS_LIB="$(otool -L "$BUILD/ro.alexd.em7455_status" | awk '/libusb/{print $1; exit}')"

if [[ -z "$OLD_ENGINE_LIB" || -z "$OLD_STATUS_LIB" ]]; then
    echo "EROARE: nu pot identifica dependenta libusb."
    exit 1
fi

install_name_tool -id "$LIB_DEST" "$BUILD/libusb-1.0.0.dylib"
install_name_tool -change "$OLD_ENGINE_LIB" "$LIB_DEST" "$BUILD/ro.alexd.mbim_lte"
install_name_tool -change "$OLD_STATUS_LIB" "$LIB_DEST" "$BUILD/ro.alexd.em7455_status"

echo "6. Semnez componentele..."

APP_IDENTITY="$(
    security find-identity -v -p codesigning 2>/dev/null |
    sed -n 's/.*"\(Developer ID Application:.*\)"/\1/p' |
    head -1
)"

sign_one() {
    local ITEM="$1"

    if [[ -n "$APP_IDENTITY" ]]; then
        codesign \
          --force \
          --options runtime \
          --timestamp \
          --sign "$APP_IDENTITY" \
          "$ITEM"
    else
        codesign \
          --force \
          --sign - \
          --timestamp=none \
          "$ITEM"
    fi
}

sign_one "$BUILD/libusb-1.0.0.dylib"
sign_one "$BUILD/ro.alexd.mbim_lte"
sign_one "$BUILD/ro.alexd.em7455_status"
sign_one "$BUILD/ro.alexd.CellularLTEHelper"
sign_one "$BUILD/Cellular.app"

if [[ -n "$APP_IDENTITY" ]]; then
    echo "   Developer ID Application: DA"
else
    echo "   Developer ID Application: NU - folosesc ad-hoc"
fi

echo "7. Construiesc payload-ul..."

mkdir -p "$ROOT/Applications"
mkdir -p "$ROOT/Library/PrivilegedHelperTools"
mkdir -p "$ROOT/Library/LaunchDaemons"
mkdir -p "$ROOT/Library/LaunchAgents"
mkdir -p "$ROOT/Library/Application Support/CellularLTE/lib"
mkdir -p "$ROOT/Library/Application Support/CellularLTE/Commands"

cp -R "$BUILD/Cellular.app" "$ROOT/Applications/Cellular.app"

cp "$BUILD/ro.alexd.mbim_lte" \
   "$ROOT/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"

cp "$BUILD/ro.alexd.em7455_status" \
   "$ROOT/Library/PrivilegedHelperTools/ro.alexd.em7455_status"

cp "$BUILD/ro.alexd.CellularLTEHelper" \
   "$ROOT/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper"

cp "$BUILD/libusb-1.0.0.dylib" \
   "$ROOT/Library/Application Support/CellularLTE/lib/libusb-1.0.0.dylib"

cp "$HERE/ro.alexd.CellularLTE.Helper.plist" \
   "$ROOT/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist"

cp "$HERE/ro.alexd.CellularLTE.Menu.plist" \
   "$ROOT/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"

cp "$HERE/uninstall-pkg.sh" \
   "$ROOT/Library/Application Support/CellularLTE/uninstall.sh"

cp "$HERE/diagnostic.sh" \
   "$ROOT/Library/Application Support/CellularLTE/diagnostic.sh"

cp "$HERE/startup-timing.sh" \
   "$ROOT/Library/Application Support/CellularLTE/startup-timing.sh"

touch "$ROOT/Library/Application Support/CellularLTE/state.json"
touch "$ROOT/Library/Application Support/CellularLTE/config"
touch "$ROOT/Library/Application Support/CellularLTE/apn-cache.tsv"
touch "$ROOT/Library/Application Support/CellularLTE/helper.log"
touch "$ROOT/Library/Application Support/CellularLTE/engine.log"
touch "$ROOT/Library/Application Support/CellularLTE/launchd.log"

chmod 755 \
  "$ROOT/Library/PrivilegedHelperTools/ro.alexd.mbim_lte" \
  "$ROOT/Library/PrivilegedHelperTools/ro.alexd.em7455_status" \
  "$ROOT/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper" \
  "$ROOT/Library/Application Support/CellularLTE/uninstall.sh" \
  "$ROOT/Library/Application Support/CellularLTE/diagnostic.sh" \
  "$ROOT/Library/Application Support/CellularLTE/startup-timing.sh"

chmod 644 \
  "$ROOT/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist" \
  "$ROOT/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"

cat > "$SCRIPTS/preinstall" <<'PREINSTALL'
#!/bin/zsh
set -e

ENGINE="/Library/PrivilegedHelperTools/ro.alexd.mbim_lte"

# Stop current menu app.
pkill -TERM -x Cellular 2>/dev/null || true
pkill -TERM -x CellularLTE 2>/dev/null || true

# Gracefully stop the active cellular engine before replacing it.
PIDS="$(pgrep -f "^${ENGINE}$" 2>/dev/null || true)"

if [[ -n "$PIDS" ]]; then
    for PID in ${(f)PIDS}; do
        kill -USR1 "$PID" 2>/dev/null || true
    done

    for i in {1..40}; do
        pgrep -f "^${ENGINE}$" >/dev/null 2>&1 || break
        sleep 0.25
    done
fi

launchctl bootout system/ro.alexd.CellularLTE.Helper 2>/dev/null || true
exit 0
PREINSTALL

cat > "$SCRIPTS/postinstall" <<'POSTINSTALL'
#!/bin/zsh
set -e

SUPPORT="/Library/Application Support/CellularLTE"
COMMANDS="$SUPPORT/Commands"
DAEMON="/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist"
AGENT="/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"

# Remove obsolete app name, if present.
rm -rf "/Applications/Cellular LTE.app"

# System component ownership.
chown -R root:wheel "/Applications/Cellular.app"
chmod 755 "/Applications/Cellular.app/Contents/MacOS/Cellular"

chown root:wheel \
  "/Library/PrivilegedHelperTools/ro.alexd.mbim_lte" \
  "/Library/PrivilegedHelperTools/ro.alexd.em7455_status" \
  "/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper"

chmod 755 \
  "/Library/PrivilegedHelperTools/ro.alexd.mbim_lte" \
  "/Library/PrivilegedHelperTools/ro.alexd.em7455_status" \
  "/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper"

chown -R root:wheel "$SUPPORT/lib"
chmod 755 "$SUPPORT/lib"
chmod 755 "$SUPPORT/lib/libusb-1.0.0.dylib"

chown root:wheel "$DAEMON" "$AGENT"
chmod 644 "$DAEMON" "$AGENT"

# State and command channel.
mkdir -p "$COMMANDS"
chown root:admin "$SUPPORT" "$COMMANDS"
chmod 0770 "$SUPPORT" "$COMMANDS"

for FILE in state.json config apn-cache.tsv helper.log engine.log launchd.log; do
    touch "$SUPPORT/$FILE"
    chown root:admin "$SUPPORT/$FILE"
    chmod 0644 "$SUPPORT/$FILE"
done

chmod 0755 \
  "$SUPPORT/uninstall.sh" \
  "$SUPPORT/diagnostic.sh" \
  "$SUPPORT/startup-timing.sh"

if ! grep -q '^AUTO=' "$SUPPORT/config" 2>/dev/null; then
    echo "AUTO=1" > "$SUPPORT/config"
fi

# Preserve learned APNs. Seed only the bearer actually validated.
if ! grep -qi $'^TELEKOM.RO\t' "$SUPPORT/apn-cache.tsv" 2>/dev/null; then
    printf 'TELEKOM.RO\tbroadband\n' >> "$SUPPORT/apn-cache.tsv"
fi

# Remove old per-user LaunchAgent for the current console user.
CONSOLE_USER="$(stat -f '%Su' /dev/console 2>/dev/null || true)"

if [[ -n "$CONSOLE_USER" &&
      "$CONSOLE_USER" != "root" &&
      "$CONSOLE_USER" != "loginwindow" ]]; then

    USER_HOME="$(dscl . -read "/Users/$CONSOLE_USER" NFSHomeDirectory 2>/dev/null | awk '{print $2}')"
    USER_UID="$(id -u "$CONSOLE_USER" 2>/dev/null || true)"

    if [[ -n "$USER_HOME" ]]; then
        rm -f "$USER_HOME/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist"
    fi

    if [[ -n "$USER_UID" ]]; then
        launchctl bootout "gui/$USER_UID/ro.alexd.CellularLTE.Menu" 2>/dev/null || true
    fi
fi

# Root helper.
launchctl bootstrap system "$DAEMON" 2>/dev/null || true
launchctl enable system/ro.alexd.CellularLTE.Helper
launchctl kickstart -k system/ro.alexd.CellularLTE.Helper

# /Library/LaunchAgents applies at login for all users.
if [[ -n "${USER_UID:-}" ]]; then
    launchctl bootstrap "gui/$USER_UID" "$AGENT" 2>/dev/null || true
    launchctl enable "gui/$USER_UID/ro.alexd.CellularLTE.Menu" 2>/dev/null || true
    launchctl kickstart -k "gui/$USER_UID/ro.alexd.CellularLTE.Menu" 2>/dev/null || true
fi

exit 0
POSTINSTALL

chmod 755 "$SCRIPTS/preinstall" "$SCRIPTS/postinstall"

echo "8. Construiesc .pkg..."

UNSIGNED="$OUTDIR/Cellular-${VERSION}-unsigned.pkg"
FINAL="$OUTDIR/Cellular-${VERSION}.pkg"

rm -f "$UNSIGNED" "$FINAL"

pkgbuild \
  --root "$ROOT" \
  --scripts "$SCRIPTS" \
  --identifier "$PKG_ID" \
  --version "$VERSION" \
  --install-location / \
  --ownership recommended \
  "$UNSIGNED"

INSTALLER_ID="$(
    security find-identity -v 2>/dev/null |
    sed -n 's/.*"\(Developer ID Installer:.*\)"/\1/p' |
    head -1
)"

if [[ -n "$INSTALLER_ID" ]]; then
    echo "9. Semnez pachetul cu Developer ID Installer..."
    productsign \
      --sign "$INSTALLER_ID" \
      "$UNSIGNED" \
      "$FINAL"
    rm -f "$UNSIGNED"
    SIGNED="YES"
else
    mv "$UNSIGNED" "$FINAL"
    SIGNED="NO"
fi

echo
echo "=================================================="
echo " PACHET CREAT"
echo "=================================================="
echo
echo "$FINAL"
echo
echo "SHA256:"
shasum -a 256 "$FINAL"
echo
echo "Self-contained target:"
echo "  - targetul NU are nevoie de Homebrew"
echo "  - targetul NU are nevoie de Xcode/Command Line Tools"
echo
echo "Signed with Developer ID Installer: $SIGNED"

if [[ "$SIGNED" == "NO" ]]; then
    echo
    echo "ATENTIE:"
    echo "Pachetul este functional, dar nesemnat Developer ID."
    echo "Pentru distributie fara avertismente Gatekeeper, foloseste"
    echo "Developer ID Application + Developer ID Installer si notarizare."
fi
