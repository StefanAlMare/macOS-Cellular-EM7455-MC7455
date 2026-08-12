# Changelog

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
