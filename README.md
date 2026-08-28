# macOS Cellular — Sierra EM7455 / MC7455

[![Build](https://github.com/StefanAlMare/macOS-Cellular-EM7455-MC7455/actions/workflows/build.yml/badge.svg)](https://github.com/StefanAlMare/macOS-Cellular-EM7455-MC7455/actions/workflows/build.yml)

A user-space cellular networking stack for **modern macOS** using Sierra Wireless **EM7455 / Dell DW5811e (M.2)** and **MC7455 (Mini PCIe)** WWAN modems.

The project restores practical cellular data connectivity on macOS systems where the modem is visible over USB but macOS no longer provides a usable native WWAN networking path for this hardware.

The implementation speaks **MBIM/NCM directly through libusb**, creates a macOS **utun** interface, publishes temporary network state through **SystemConfiguration**, and provides a menu-bar **Cellular.app** with automatic Ethernet/Wi-Fi fallback, carrier display, signal strength and APN handling.

> [!IMPORTANT]
> This is **not** an Apple WWAN driver and it is **not** native CoreTelephony integration. It is a working user-space implementation developed from hardware traces, Linux behaviour, USB descriptors, MBIM/NCM specifications and extensive testing on real hardware.

## Status

| Component | Status | Notes |
|---|---|---|
| Dell DW5811e / Sierra EM7455B | ✅ Hardware validated | Internal M.2 WWAN in Dell Precision 7720 |
| Sierra MC7455 | ✅ Hardware validated | Internal full-size Mini PCIe WWAN in Dell Precision M6800 |
| USB identity `1199:9071` | ✅ Validated on both | Sierra Generic identity used by the MBIM engine |
| MBIM control | ✅ Validated | Interface 12 |
| MBIM/NCM data | ✅ Validated | Interface 13, bulk OUT `0x04`, bulk IN `0x86` |
| AT telemetry | ✅ Validated | Interface 3, OUT `0x03`, IN `0x84` |
| LTE attach / data | ✅ Validated | Real Internet traffic tested |
| IPv4 | ✅ Production-ready on tested hardware | utun + routes + DNS |
| APN Auto | ✅ Validated | Per-carrier cache + active-context verification |
| Signal bars / dBm | ✅ Validated | Live modem telemetry |
| Automatic fallback | ✅ Validated | Cellular starts when Ethernet + Wi-Fi are unavailable |
| `.pkg` packaging | ✅ CI build validated / 🧪 hardware clean-install pending | GitHub Actions builds the self-contained package successfully |
| Cellular IPv6 | ⚠️ Carrier unavailable in current tests | Engine/UI are prepared; tested bearers returned IPv4 only |
| Native macOS WWAN integration | 🚧 Help wanted | See [Developer Call](Docs/DEVELOPER-CALL.md) and [Issue #1](https://github.com/StefanAlMare/macOS-Cellular-EM7455-MC7455/issues/1) |

## New: MC7455 Mini PCIe validation

The existing application has now been validated unchanged with a **Sierra Wireless MC7455 Mini PCIe** in a **Dell Precision M6800 running macOS Tahoe**.

The MC7455 was prepared with:

- Sierra Generic firmware **SWI9X30C_02.39.00.00**;
- Generic PRI **002.085_000**;
- Sierra Generic USB identity **`1199:9071`**;
- USB composition changed from the tested card's shipped/default **composition 7** to **composition 8 = DM / NMEA / AT / MBIM**.

After that preparation, the card exposed the **same AT + MBIM interfaces and endpoints already expected by the EM7455 engine**. Cellular.app was simply installed on macOS Tahoe and worked immediately. **No MC7455-specific source-code change was required.**

The final MC7455 composition 8 setup was also validated before macOS testing in:

- **Ubuntu** — `cdc_mbim`, ModemManager/NetworkManager, LTE attach and real Internet traffic;
- **Windows** — automatically recognized as Mobile Broadband / **Vodafone RO LTE**, connected and passed real Internet testing;
- **macOS Tahoe** — existing Cellular.app worked immediately after installation.

This confirms that the relevant compatibility boundary is the **validated Sierra Generic MBIM USB layout**, not whether the card is physically M.2 or Mini PCIe.

See [Docs/HARDWARE.md](Docs/HARDWARE.md) and [Docs/FIRMWARE.md](Docs/FIRMWARE.md) for the exact MC7455 preparation and the Dell Precision M6800 pin notes.

### macOS validation

Current real-hardware validation includes:

- **Dell Precision 7720 + Dell DW5811e / Sierra EM7455B (M.2) + macOS Tahoe**;
- **Dell Precision M6800 + Sierra MC7455 (Mini PCIe) + macOS Tahoe**.

The code targets modern macOS APIs and the package builder currently targets **Intel/x86_64**. The reference to M.2 above means the WWAN **form factor**, not Apple M2/Apple Silicon.

## What works

- internal EM7455/DW5811e and MC7455 cellular data without an external hotspot
- MBIM session activation and deactivation
- NCM/MBIM IP data transport in user space
- macOS `utun` interface creation
- temporary IPv4 and DNS publication through SystemConfiguration
- automatic `0.0.0.0/1` and `128.0.0.0/1` routing through Cellular
- automatic fallback when both Ethernet and Wi-Fi disappear
- automatic disconnect when Ethernet or Wi-Fi returns
- manual Connect / Disconnect mode
- carrier name in the macOS menu bar
- four live signal bars plus dBm reporting
- APN discovery/fallback with a per-carrier cache
- active APN verification through `AT+CGCONTRDP`
- clean SIGUSR1-based shutdown with route/utun cleanup
- startup timing diagnostics
- `.pkg` build path intended to remove Homebrew/Xcode requirements from target machines

## Architecture

```text
                     ┌──────────────────────┐
                     │     Cellular.app     │
                     │ menu / signal / APN  │
                     └──────────┬───────────┘
                                │ state + commands
                                ▼
                ┌──────────────────────────────┐
                │ privileged helper / daemon   │
                │ auto fallback + lifecycle    │
                └──────────────┬───────────────┘
                               │ launches
                               ▼
                ┌──────────────────────────────┐
                │      mbim_lte engine          │
                │ libusb + MBIM + NCM + utun    │
                └─────────┬───────────┬─────────┘
                          │           │
                   MBIM control    IP data
                    USB IF 12      USB IF 13
                          │           │
                          └─────┬─────┘
                                ▼
                 Sierra EM7455 / MC7455
                                │
                                ▼
                         LTE mobile network
```

Data plane:

```text
macOS utun -> AF_INET -> MBIM/NCM NTB16 -> USB 0x04 -> modem
modem -> USB 0x86 -> MBIM/NCM NTB16 -> AF_INET -> macOS utun
```

The current validated engine is intentionally IPv4-first. IPv6 capability probing is included under `Tools/` and documented separately.

## Tested hardware and USB layout

### EM7455 / M.2 validation

- **Laptop:** Dell Precision 7720
- **WWAN card:** Dell DW5811e / Sierra Wireless EM7455B
- **Form factor:** internal M.2 WWAN

### MC7455 / Mini PCIe validation

- **Laptop:** Dell Precision M6800
- **WWAN card:** Sierra Wireless MC7455
- **Form factor:** internal full-size Mini PCIe WWAN
- **Final USB composition:** `8 = DM / NMEA / AT / MBIM`

### Shared validated production layout

- **USB ID:** `1199:9071`
- **AT:** interface 3, OUT `0x03`, IN `0x84`
- **MBIM control:** interface 12, interrupt IN `0x87`
- **MBIM/NCM data:** interface 13, OUT `0x04`, IN `0x86`

The telemetry tools also recognize Dell OEM ID `413c:81b6`, but the production MBIM engine uses the validated Sierra Generic `1199:9071` layout.

See [Docs/HARDWARE.md](Docs/HARDWARE.md).

## Firmware and USB composition

The validated Generic target for the Sierra EM/MC7455 family is:

- firmware: **SWI9X30C_02.39.00.00**
- Generic PRI: **002.085_000**
- firmware file: `SWI9X30C_02.39.00.00.cwe`
- configuration file: `SWI9X30C_02.39.00.00_GENERIC_002.085_000.nvu`

The firmware binaries are **not redistributed** in this repository. Obtain them from the official Sierra Wireless / Semtech EM/MC74xx firmware page.

For the tested MC7455, Generic firmware/PRI was followed by a move from the card's shipped/default **composition 7** to the final validated **composition 8 (`DM / NMEA / AT / MBIM`)**. That composition is the common configuration that passed Ubuntu, Windows and macOS Tahoe testing.

> [!WARNING]
> Flashing firmware or changing USB composition can make the modem temporarily inaccessible. Do not copy a composition value blindly. Keep a Linux/Windows recovery path and verify the exact module before changing firmware or USB functions.

The Dell Precision M6800 additionally required platform-specific Mini PCIe pin isolation for the MC7455; this is **not a universal MC7455 requirement**. Exact details are in [Docs/HARDWARE.md](Docs/HARDWARE.md) and [Docs/FIRMWARE.md](Docs/FIRMWARE.md).

## Installation

The intended distribution method is a macOS installer package.

### Build the package

On a macOS build machine:

```bash
cd Installer
chmod +x build-pkg.sh
./build-pkg.sh
```

The builder produces:

```text
Installer/Release/Cellular-2.6.2.pkg
```

The **target Mac** does not need Homebrew, libusb or Xcode; libusb is embedded in the package payload. Homebrew + libusb + Xcode Command Line Tools are required only on the build machine.

### Install

Double-click the resulting `.pkg`, or:

```bash
sudo installer -pkg Cellular-2.6.2.pkg -target /
```

After installation:

- `/Applications/Cellular.app`
- `/Library/PrivilegedHelperTools/ro.alexd.mbim_lte`
- `/Library/PrivilegedHelperTools/ro.alexd.em7455_status`
- `/Library/PrivilegedHelperTools/ro.alexd.CellularLTEHelper`
- `/Library/LaunchDaemons/ro.alexd.CellularLTE.Helper.plist`
- `/Library/LaunchAgents/ro.alexd.CellularLTE.Menu.plist`
- `/Library/Application Support/CellularLTE/`

See [Docs/INSTALLATION.md](Docs/INSTALLATION.md).

## APN behaviour

The project deliberately does **not** trust stale modem profiles as the sole APN source.

The working strategy is:

1. use the per-carrier cached APN if already validated;
2. otherwise try network-default / NULL AccessString;
3. use the configured fallback when required;
4. after activation, query `AT+CGCONTRDP` and cache the APN actually used by the active bearer.

See [Docs/APN-AND-CARRIERS.md](Docs/APN-AND-CARRIERS.md).

## IPv6 status

IPv6 was tested rather than assumed. On current tested SIMs/operators, IPv4v6 requests established a bearer but the network/modem returned IPv4 configuration only; IPv6-only requests were rejected.

The current limitation is therefore **carrier/bearer availability in the tested configurations**, not a claim that macOS itself cannot support IPv6. Ethernet and Wi-Fi IPv6 remain untouched by Cellular.

See [Docs/IPV6.md](Docs/IPV6.md).

## Reverse-engineering history

The final engine came from a long sequence of hardware experiments: USB enumeration, MBIM/NCM discovery, Session 0 activation, IP configuration parsing, NTB16/NDP16 data framing, raw network proof, utun bridging, SystemConfiguration publication, lifecycle cleanup, carrier/signal telemetry and APN learning.

The second MC7455 validation is especially useful because it proves the existing data plane can be reused across **EM7455 M.2 and MC7455 Mini PCIe** when both expose the same Sierra Generic layout.

See [Docs/REVERSE-ENGINEERING.md](Docs/REVERSE-ENGINEERING.md). The raw development lineage is preserved under [`History/`](History/README.md).

## Tools

`Tools/` contains reproducible diagnostics used during development:

- `mbim-apn/` — read-only MBIM provider/provisioned-context probe
- `active-context/` — `COPS`, `CGACT`, `CGDCONT`, `CGCONTRDP`, `GSTATUS` probe
- `ipv6-capability/` — temporary IPv4v6 capability test
- `ipv6-matrix/` — broadband/NULL APN × IPv4v6/IPv6-only matrix

These are development tools, not required for normal installation.

## Help wanted: native macOS integration

The current stack proves that the hardware can be controlled and can carry real mobile data on modern macOS. The missing piece is a clean, maintainable **native macOS WWAN integration**.

Experienced macOS, DriverKit, NetworkExtension, IOKit, USB, MBIM/NCM and cellular developers are invited to help with native interface integration, system-extension architecture, carrier/APN/SIM UI integration and future IPv6 work.

Please read [Docs/DEVELOPER-CALL.md](Docs/DEVELOPER-CALL.md), [CONTRIBUTING.md](CONTRIBUTING.md), and join the discussion in [Issue #1](https://github.com/StefanAlMare/macOS-Cellular-EM7455-MC7455/issues/1).

## Known limitations

- production engine currently expects the validated Sierra Generic `1199:9071` interface/endpoint layout;
- production package is currently Intel/x86_64;
- current validated data path is IPv4;
- current carrier tests did not provide Cellular IPv6;
- no SMS/voice/eSIM integration;
- no native macOS WWAN preference pane / CoreTelephony integration;
- manual mode can intentionally produce split-stack behaviour if IPv4 is routed over Cellular while another interface still has IPv6;
- other Sierra layouts, and unrelated Quectel/Fibocom modems, require explicit porting and validation.

## Security and privacy

No IMEI, IMSI, ICCID, phone number, SIM secret or personal subscriber identifier belongs in this repository or in bug reports. Redact them before posting logs.

## Credits

- **StefanAlMare** — hardware owner, firmware modification/reflash, hardware validation, reverse-engineering test execution and project direction.
- **Sierra Wireless / Semtech** — EM7455/MC7455 hardware, firmware and AT-command documentation.
- **USB-IF** — USB NCM and MBIM specifications.
- **Linux kernel / libmbim / ModemManager developers** — invaluable public reference implementations and behavioural reference points for MBIM/NCM devices.
- **Apple** — macOS networking APIs, SystemConfiguration and utun control interface used by the current implementation.

See [CREDITS.md](CREDITS.md).

## License

Original code in this repository is released under the **MIT License** unless a file states otherwise. Third-party specifications, firmware and vendor binaries are not included and retain their respective licenses.

This is an independent community project. It is not affiliated with Apple, Dell, Sierra Wireless/Semtech, Telekom, Vodafone or any other mobile operator.
