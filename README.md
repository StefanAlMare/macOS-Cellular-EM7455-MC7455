# macOS-Cellular-EM7455

[![Build](https://github.com/StefanAlMare/macOS-Cellular-EM7455/actions/workflows/build.yml/badge.svg)](https://github.com/StefanAlMare/macOS-Cellular-EM7455/actions/workflows/build.yml)

A user-space cellular networking stack for **modern macOS** using the internal **Sierra Wireless EM7455 / Dell DW5811e** WWAN modem.

This project restores practical cellular data connectivity on macOS systems where the modem is visible over USB but macOS no longer provides a usable native WWAN networking path for this hardware.

The current implementation speaks **MBIM/NCM directly through libusb**, creates a macOS **utun** interface, publishes temporary network state through **SystemConfiguration**, and provides a menu-bar **Cellular.app** with automatic Ethernet/Wi-Fi fallback, carrier display, signal strength and APN handling.

> [!IMPORTANT]
> This is **not** an Apple WWAN driver and it is **not** native CoreTelephony integration. It is a working user-space implementation developed from hardware traces, Linux behaviour, USB descriptors, MBIM/NCM specifications and extensive testing on real hardware.

## Status

| Component | Status | Notes |
|---|---|---|
| Dell DW5811e / Sierra EM7455 | ✅ Hardware validated | Internal M.2 WWAN in Dell Precision 7720 |
| USB identity `1199:9071` | ✅ Validated | Sierra Generic identity used by the MBIM engine |
| MBIM control | ✅ Validated | Interface 12 |
| MBIM/NCM data | ✅ Validated | Interface 13, bulk OUT `0x04`, bulk IN `0x86` |
| AT telemetry | ✅ Validated | Interface 3, OUT `0x03`, IN `0x84` |
| LTE attach / data | ✅ Validated | Real Internet traffic tested |
| IPv4 | ✅ Production-ready on tested hardware | utun + routes + DNS |
| APN Auto | ✅ Validated | Per-carrier cache + active-context verification |
| Signal bars / dBm | ✅ Validated | Live modem telemetry |
| Automatic fallback | ✅ Validated | Cellular starts when Ethernet + Wi-Fi are unavailable |
| `.pkg` packaging | ✅ CI build validated / 🧪 hardware clean-install pending | GitHub Actions builds the self-contained package successfully; clean-install validation on target hardware remains the release gate |
| Cellular IPv6 | ⚠️ Carrier unavailable in current tests | Engine/UI are prepared for future work, but current tested SIMs did not receive IPv6 |
| Native macOS WWAN integration | 🚧 Help wanted | See [Developer Call](Docs/DEVELOPER-CALL.md) and [Issue #1](https://github.com/StefanAlMare/macOS-Cellular-EM7455/issues/1) |

### macOS validation

The current hardware validation was performed on a **Dell Precision 7720 Hackintosh running macOS Tahoe**. Exact compatibility with other macOS builds should be reported separately rather than assumed.

The code targets modern macOS APIs and the package builder currently targets **Intel/x86_64**.

## What works

- internal EM7455/DW5811e cellular data without an external hotspot
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
                 Sierra EM7455 / Dell DW5811e
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

## Tested hardware

- **Laptop:** Dell Precision 7720
- **WWAN card:** Dell DW5811e / Sierra Wireless EM7455B
- **Form factor:** internal M.2 WWAN
- **Working USB ID for the data engine:** `1199:9071`
- **AT interface:** USB interface 3
- **MBIM control interface:** USB interface 12
- **MBIM/NCM data interface:** USB interface 13

The telemetry tools also recognize Dell OEM ID `413c:81b6`, but the current production MBIM engine is built around the validated Sierra Generic `1199:9071` layout.

See [Docs/HARDWARE.md](Docs/HARDWARE.md).

## Firmware

The card used to develop this project was manually reflashed/modified by **StefanAlMare** while recovering a usable modern MBIM configuration for macOS testing.

Historical observations included Dell/OEM firmware **SWI9X30C_02.24.03.00**. The Generic target used for this project is:

- firmware: **SWI9X30C_02.39.00.00**
- Generic PRI: **002.085_000**
- firmware file: `SWI9X30C_02.39.00.00.cwe`
- configuration file: `SWI9X30C_02.39.00.00_GENERIC_002.085_000.nvu`

The firmware binaries are **not redistributed** in this repository. Obtain them from the official Sierra Wireless / Semtech EM/MC74xx firmware page.

> [!WARNING]
> Flashing firmware or changing USB composition can make the modem temporarily inaccessible. Do not copy a composition value blindly. Keep a Linux/Windows recovery path and verify your exact module before changing firmware or USB functions.

See [Docs/FIRMWARE.md](Docs/FIRMWARE.md).

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

During development, the current TELEKOM.RO SIM registered correctly while the modem still contained an old Vodafone profile (`internet.vodafone.ro`). Selecting that stale profile caused repeated connection failure.

The working strategy became:

1. use the per-carrier cached APN if already validated;
2. otherwise try network-default / NULL AccessString;
3. use `broadband` as fallback on the tested setup;
4. after activation, query `AT+CGCONTRDP` and cache the APN actually used by the active bearer.

For the tested TELEKOM.RO bearer, the active context reported **Broadband**.

See [Docs/APN-AND-CARRIERS.md](Docs/APN-AND-CARRIERS.md).

## IPv6 status

IPv6 was tested rather than assumed.

Four temporary MBIM scenarios were executed:

| Scenario | CONNECT | IPv6 config | IPv6 packets |
|---|---:|---:|---:|
| `broadband + IPv4v6` | ✅ | ❌ | ❌ |
| `NULL APN + IPv4v6` | ✅ | ❌ | ❌ |
| `broadband + IPv6-only` | ❌ | ❌ | ❌ |
| `NULL APN + IPv6-only` | ❌ | ❌ | ❌ |

In both IPv4v6 cases the network/modem downgraded the bearer to IPv4 (`IP type = 1`, IPv6 flags `0`). The IPv6-only requests were rejected.

Therefore the current limitation is **carrier/bearer availability**, not macOS IPv6 support in general. Ethernet and Wi-Fi IPv6 remain untouched by Cellular.

The app already distinguishes Cellular IPv6 state, and the repository includes probes needed to continue development when a SIM/operator providing native IPv6 is available.

See [Docs/IPV6.md](Docs/IPV6.md).

## Reverse-engineering history

The final engine came from a long sequence of hardware experiments:

1. enumerate the EM7455 USB tree and descriptors;
2. identify the Sierra Generic `1199:9071` MBIM layout;
3. reproduce the Linux-like NCM initialization sequence (`ALT1 -> ALT0 -> GET_NTB_PARAMETERS -> ALT1`);
4. implement `MBIM_OPEN` and asynchronous `OPEN_DONE` handling;
5. query Subscriber Ready, Register State, Packet Service and Signal State;
6. activate MBIM Session 0;
7. parse `MBIM_CID_IP_CONFIGURATION`;
8. construct and parse NTB16 / NDP16 frames;
9. prove data-plane traffic with a raw DNS query;
10. bridge macOS utun packets to/from MBIM/NCM;
11. publish temporary IPv4 + DNS state to SystemConfiguration;
12. fix shutdown, stale PID, menu UI deadlocks and auto-fallback lifecycle;
13. add live carrier/signal telemetry and APN learning;
14. test real IPv6 capability instead of guessing.

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

Experienced macOS, DriverKit, NetworkExtension, IOKit, USB, MBIM/NCM and cellular developers are explicitly invited to help answer questions such as:

- Can this be exposed as a first-class macOS network interface without the current user-space routing approach?
- Is there a maintainable DriverKit or system-extension architecture for MBIM/NCM WWAN devices?
- Can carrier/APN/SIM state be integrated cleanly with macOS networking UI?
- Can native IPv6 RA/DHCPv6 handling be implemented when the carrier provides it?
- Can the hardware support be generalized safely beyond EM7455/DW5811e?

Please read [Docs/DEVELOPER-CALL.md](Docs/DEVELOPER-CALL.md), [CONTRIBUTING.md](CONTRIBUTING.md), and join the discussion in [Issue #1](https://github.com/StefanAlMare/macOS-Cellular-EM7455/issues/1).

## Known limitations

- hardware-specific USB interface numbers/endpoints are currently compiled into the EM7455 engine;
- production engine is currently Intel/x86_64;
- current validated data path is IPv4;
- current carrier tests did not provide Cellular IPv6;
- no SMS/voice/eSIM integration;
- no native macOS WWAN preference pane / CoreTelephony integration;
- manual mode can intentionally produce split-stack behaviour if IPv4 is routed over Cellular while another interface still has IPv6;
- other Sierra, Quectel or Fibocom modems require explicit porting and validation.

## Security and privacy

No IMEI, IMSI, ICCID, phone number, SIM secret or personal subscriber identifier belongs in this repository or in bug reports. Redact them before posting logs.

## Credits

- **StefanAlMare** — hardware owner, firmware modification/reflash, hardware validation, reverse-engineering test execution and project direction.
- **Sierra Wireless / Semtech** — EM7455 hardware, firmware and AT-command documentation.
- **USB-IF** — USB NCM and MBIM specifications.
- **Linux kernel / libmbim / ModemManager developers** — invaluable public reference implementations and behavioural reference points for MBIM/NCM devices.
- **Apple** — macOS networking APIs, SystemConfiguration and utun control interface used by the current implementation.

See [CREDITS.md](CREDITS.md).

## License

Original code in this repository is released under the **MIT License** unless a file states otherwise. Third-party specifications, firmware and vendor binaries are not included and retain their respective licenses.

This is an independent community project. It is not affiliated with Apple, Dell, Sierra Wireless/Semtech, Telekom, Vodafone or any other mobile operator.
