# Developer call: help bring real WWAN integration back to modern macOS

This repository is intentionally public because the current implementation solves the immediate connectivity problem but should not be the final architectural destination.

## What has already been proven

On real **Dell DW5811e / Sierra EM7455B (M.2)** and **Sierra MC7455 (Mini PCIe)** hardware, modern macOS can:

- control the modem through MBIM using libusb;
- activate and deactivate a mobile data bearer;
- retrieve IP/DNS/MTU parameters;
- transmit and receive MBIM/NCM IP traffic;
- bridge that traffic through a macOS utun interface;
- operate reliably enough for normal web/video traffic;
- monitor carrier and signal strength;
- perform automatic fallback against Ethernet/Wi-Fi.

Both validated cards use the same Sierra Generic `1199:9071` production layout. The MC7455 worked with the existing application unchanged after preparation with Generic firmware/PRI and USB composition 8 (`DM / NMEA / AT / MBIM`).

So the radio/modem problem is substantially solved for this validated Sierra 9X30 layout.

## What is missing

The remaining challenge is **macOS-native integration**.

Experienced developers are invited to investigate:

### DriverKit / system extension

Can a modern DriverKit networking architecture expose an MBIM/NCM WWAN modem cleanly without relying on a root user-space engine plus utun?

### NetworkExtension

Is there an appropriate NetworkExtension architecture that can own the packet path while respecting Apple's deployment/security model?

### WWAN UI integration

Can SIM/carrier/signal/APN state be surfaced in macOS in a first-class way rather than only through a custom menu-bar app?

### Generic MBIM device discovery

The current project expects the validated Sierra Generic layout. A robust implementation should discover descriptors, interfaces and endpoints instead of assuming `IF12/IF13` and fixed endpoints.

### IPv6

When a carrier supplies IPv6, support must handle both MBIM IP_CONFIGURATION data and IPv6 RA/DHCPv6 forwarded over the data plane.

### Power management

Sleep/wake, USB re-enumeration, radio state and resume should be made deterministic across supported machines.

## Contribution philosophy

Please keep hardware-validated claims separate from hypotheses.

A useful contribution should include:

- exact modem model and USB VID/PID;
- relevant firmware/PRI version (but no proprietary firmware upload);
- macOS version/build;
- USB interface/endpoints;
- reproducible logs with personal identifiers redacted;
- whether the result is hardware-tested or code-review-only.

The objective is to evolve this from a working hardware-specific recovery into a maintainable modern macOS cellular stack.
