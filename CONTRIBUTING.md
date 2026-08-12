# Contributing

Contributions are welcome, especially from developers with experience in:

- macOS DriverKit
- NetworkExtension
- IOKit / USB
- USB CDC-NCM / MBIM
- libmbim / ModemManager
- WWAN/cellular networking
- IPv6 RA/DHCPv6
- Swift/AppKit system utilities

## Before submitting hardware support

Include:

- laptop/model
- modem exact model
- VID:PID
- firmware + PRI version
- macOS version/build
- interface/endpoint map
- whether MBIM OPEN works
- whether Session 0 activates
- IPv4/IPv6 result
- logs with IMEI/IMSI/ICCID/phone number redacted

## Do not

- upload proprietary modem firmware to the repository;
- publish subscriber identifiers;
- claim a modem is supported because it is physically M.2-compatible;
- replace the stable MBIM initialization sequence without hardware testing;
- mark IPv6 complete without an end-to-end carrier test.

## Code style

Prefer small, reviewable changes. Keep protocol constants documented and keep diagnostics reproducible.
