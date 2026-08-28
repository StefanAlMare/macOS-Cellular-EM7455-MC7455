# Changelog

## Unreleased — MC7455 Mini PCIe validation

- validated the existing Cellular.app unchanged with Sierra Wireless MC7455 in Dell Precision M6800
- validated Sierra Generic `SWI9X30C_02.39.00.00 / PRI 002.085_000`
- moved the tested MC7455 from shipped/default USB composition 7 to composition 8 (`DM / NMEA / AT / MBIM`)
- confirmed the same `1199:9071`, IF3/IF12/IF13 and endpoint layout already used by the EM7455 engine
- validated real LTE data in Ubuntu, Windows and macOS Tahoe
- documented M6800-specific Mini PCIe pin isolation and antenna findings
- renamed repository to `macOS-Cellular-EM7455-MC7455`

## 2.6.2 — package-ready / IPv6-aware UI

- package builder for self-contained target installation
- bundled libusb path
- global LaunchAgent/LaunchDaemon layout
- Cellular IPv6 status line prepared for future engine support
- current carrier shows `Carrier unavailable` when no global Cellular IPv6 exists
- production MBIM sequence remains based on 2.6.1 stable timed engine

## 2.6.1 — stable timed baseline

- restored the complete working MBIM initialization path after an over-aggressive optimization regression
- measured startup milestones
- reduced deliberate automatic-fallback delay while keeping protocol sequence intact

## 2.6 — adaptive APN cache

- per-carrier APN cache
- TELEKOM.RO validated with active `Broadband` context
- `Connected` only after utun + IPv4 are ready
- removed misleading long `APN: checking…` state

## 2.5 — APN auto experiments

- network-default APN first with fallback
- active APN read through `CGCONTRDP`

## 2.4.x — package/UI/telemetry evolution

- application renamed to `Cellular`
- app icon and dynamic menu-bar signal bars
- operator normalization
- rollback after stale Vodafone APN exposed limitations of stored modem contexts

## 2.3 and earlier

- stable menu-bar application
- privileged helper
- automatic Ethernet/Wi-Fi fallback
- fixed PID/signal shutdown handling
- fixed menu UI deadlock
- validated Internet/video traffic over the EM7455 user-space bridge
