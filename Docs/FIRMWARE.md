# Firmware and USB composition

## Important warning

Firmware flashing and USB-composition changes are **not normal installation steps** for Cellular.app. They are hardware-preparation operations and can make a modem temporarily inaccessible.

Keep a Linux or Windows recovery environment available before changing the modem.

## Development card history

The Dell DW5811e used in this project was manually reflashed/modified by **StefanAlMare** while moving away from an older Dell/OEM carrier-oriented setup toward a Sierra Generic configuration suitable for direct MBIM work.

Historical observations during the project included:

- Dell OEM identity: `413c:81b6`
- older firmware observed: `SWI9X30C_02.24.03.00`
- the project later targeted Sierra Generic firmware/PRI:
  - `SWI9X30C_02.39.00.00`
  - `002.085_000`

The official Sierra/Semtech firmware package contains the corresponding CWE and NVU files:

```text
SWI9X30C_02.39.00.00.cwe
SWI9X30C_02.39.00.00_GENERIC_002.085_000.nvu
```

The repository does **not** redistribute these vendor firmware files.

Official firmware page:

- https://source.sierrawireless.com/resources/airprime/minicard/74xx/em_mc74xx-approved-fw-packages/

As of the 2026 Sierra/Semtech package list, Generic `SWI9X30C_02.39.00.00 / 002.085_000` is published for the EM/MC7455 family.

## USB composition observations

During Linux-side investigation, the card exposed Sierra composition choices including:

```text
6 = DM / NMEA / AT / QMI
8 = DM / NMEA / AT / MBIM
9 = MBIM
```

The working macOS project depends on the layout that exposes the AT and MBIM functions used by the code. The validated runtime layout is documented in `HARDWARE.md`.

### Verify before changing

Read-only checks should come first. Typical Sierra AT queries include:

```text
ATI
AT!IMPREF?
AT!USBCOMP?
AT+COPS?
AT+CGDCONT?
```

Exact write/unlock/flash commands are intentionally not presented as a universal recipe here because Dell OEM variants, firmware generations and recovery paths can differ. A wrong composition can remove the AT or MBIM function you need for recovery.

## Why firmware matters here

The user-space engine does not need a macOS kernel driver for the modem, but it does require the device to enumerate in a predictable MBIM/NCM layout that libusb can claim. The current production code is built around:

```text
VID:PID    1199:9071
MBIM CTRL  interface 12
MBIM DATA  interface 13
AT         interface 3
```

If your card enumerates differently, port the code rather than forcing firmware changes without understanding the module.
