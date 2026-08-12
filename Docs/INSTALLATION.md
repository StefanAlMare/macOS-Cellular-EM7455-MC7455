# Installation

## Recommended path: macOS `.pkg`

The project includes a package builder rather than requiring end users to compile the application manually.

### Build-machine requirements

- macOS
- Intel/x86_64 currently validated
- Xcode Command Line Tools
- Homebrew
- libusb (the builder installs/uses it on the build machine)

Run:

```bash
cd Installer
chmod +x build-pkg.sh
./build-pkg.sh
```

Expected output:

```text
Installer/Release/Cellular-2.6.2.pkg
```

### Target-machine requirements

The built package is intended to be self-contained. The target machine should not need:

- Homebrew
- Xcode
- Command Line Tools
- a separately installed libusb

The package embeds `libusb-1.0.0.dylib` and rewrites the executable dependency paths before signing.

### Install

GUI: double-click the `.pkg`.

Terminal:

```bash
sudo installer -pkg Cellular-2.6.2.pkg -target /
```

## Installed components

```text
/Applications/Cellular.app
/Library/PrivilegedHelperTools/ro.alexd.mbim_lte
/Library/PrivilegedHelperTools/ro.alexd.em7455_status
/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper
/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist
/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist
/Library/Application Support/CellularLTE/
```

`/Library/Application Support/CellularLTE/` stores runtime state, APN cache, logs, commands and the bundled libusb library.

## First validation

1. Keep Ethernet or Wi-Fi connected during installation.
2. Confirm `Cellular.app` appears in the menu bar.
3. Confirm operator and signal are visible.
4. Disconnect Ethernet and Wi-Fi.
5. Expect `Cellular: Connecting…` and then `Cellular: Connected • utunX`.
6. Test normal Internet traffic.
7. Restore Ethernet or Wi-Fi and verify Cellular disconnects automatically.

## Diagnostics

Installed diagnostic scripts are placed under Application Support. The source copies are also in `Installer/`.

Useful files:

```text
/Library/Application Support/CellularLTE/helper.log
/Library/Application Support/CellularLTE/engine.log
/Library/Application Support/CellularLTE/state.json
/Library/Application Support/CellularLTE/apn-cache.tsv
```

## Signing and notarization

`build-pkg.sh` looks for `Developer ID Application` and `Developer ID Installer` identities. If they are present, it signs the relevant components/package. Otherwise it can build with ad-hoc code signing, but Gatekeeper warnings may appear on other Macs.

For public distribution, Developer ID signing and notarization should be used.

## Uninstall

Use the packaged uninstall script:

```bash
sudo "/Library/Application Support/CellularLTE/uninstall.sh"
```

Reboot or log out/in if LaunchServices/menu state remains cached.
