# Firmware and USB composition

## Important warning

Firmware flashing and USB-composition changes are **not normal installation steps** for Cellular.app. They are hardware-preparation operations and can make a modem temporarily inaccessible.

Keep a Linux or Windows recovery environment available before changing the modem. Do not copy composition numbers blindly between unrelated modem families or firmware branches.

## Validated Generic target

The project is now validated with both the Dell DW5811e / Sierra EM7455B and a Sierra MC7455 using the Sierra Generic firmware family:

- firmware: **SWI9X30C_02.39.00.00**
- Generic PRI: **002.085_000**
- firmware file: `SWI9X30C_02.39.00.00.cwe`
- configuration file: `SWI9X30C_02.39.00.00_GENERIC_002.085_000.nvu`

The repository does **not** redistribute these vendor firmware files. Obtain them from the official Sierra Wireless / Semtech EM/MC74xx firmware page.

Official firmware page:

- https://source.sierrawireless.com/resources/airprime/minicard/74xx/em_mc74xx-approved-fw-packages/

As of the 2026 Sierra/Semtech package list, Generic `SWI9X30C_02.39.00.00 / 002.085_000` is published for the EM/MC7455 family.

## Development card histories

### Dell DW5811e / Sierra EM7455B

The original M.2 card used to develop this project was manually reflashed/modified by **StefanAlMare** while moving away from an older Dell/OEM carrier-oriented setup toward a Sierra Generic configuration suitable for direct MBIM work.

Historical observations included:

- Dell OEM identity: `413c:81b6`
- older firmware observed: `SWI9X30C_02.24.03.00`
- final project target: Sierra Generic `SWI9X30C_02.39.00.00 / 002.085_000`

### Sierra MC7455 Mini PCIe

A second validation was completed on a full-size Mini PCIe **Sierra MC7455** installed in a Dell Precision M6800.

The tested card was received in its shipped/default **USB composition 7**. It was prepared as follows:

1. flash/select Sierra Generic firmware **SWI9X30C_02.39.00.00**;
2. activate Generic PRI **002.085_000**;
3. verify Sierra Generic USB identity `1199:9071`;
4. change the modem from the shipped/default composition **7** to **composition 8**;
5. reset/re-enumerate the modem and verify the final MBIM interfaces/endpoints;
6. validate real data connectivity in Ubuntu and Windows;
7. boot macOS Tahoe and install the existing Cellular.app — it worked immediately with **no MC7455-specific code change**.

## Why composition 8 matters

The final MC7455 cross-platform configuration is:

```text
8 = DM / NMEA / AT / MBIM
```

That composition exposes exactly the interface layout already used by the production macOS engine:

```text
VID:PID    1199:9071
AT         interface 3   OUT 0x03 / IN 0x84
MBIM CTRL  interface 12  INT IN 0x87
MBIM DATA  interface 13  OUT 0x04 / IN 0x86
```

This is why the existing EM7455-oriented Cellular engine also worked unchanged with the MC7455 once the card was prepared correctly.

During investigation, Sierra firmware also exposed other composition choices, including QMI-oriented and MBIM-only layouts. The important point is not the number alone: the production application requires the validated **AT + MBIM** function layout above.

## Linux verification and composition change

On the tested MC7455, Linux/libqmi was used to inspect and select the Sierra USB composition. A typical read-only check is:

```bash
sudo qmicli -p -d /dev/cdc-wdm1 --dms-swi-get-usb-composition
```

For this specifically validated MC7455, the final change to composition 8 was performed with:

```bash
sudo qmicli -p -d /dev/cdc-wdm1 --dms-swi-set-usb-composition=8
sudo qmicli -p -d /dev/cdc-wdm1 --dms-set-operating-mode=reset
```

After reset, the device re-enumerated as `1199:9071` with `cdc_mbim`, retaining DM/NMEA/AT functions and exposing the MBIM control/data interfaces required by Cellular.app.

Device paths such as `/dev/cdc-wdm1` are examples from the validated machine and may differ after a reset or on another computer.

## Dell Precision M6800 Mini PCIe electrical compatibility

The M6800 required two additional **platform-specific hardware** changes before the MC7455 behaved normally:

- pins **23, 25, 31 and 33** were insulated to prevent the M6800/MC7455 USB 3.0 SuperSpeed conflict; the card then runs as USB 2.0 High-Speed;
- pin **20** was insulated because the M6800 asserted the Mini PCIe `W_DISABLE` signal, keeping the radio in low-power/off state. After isolation the modem reported `W_DISABLE: 0` and `ONLINE`.

These changes are documented because they were necessary on the validated M6800, but they are **not generic firmware requirements and must not be applied blindly to other systems**.

## Cross-platform validation of the final MC7455 state

With Generic `02.39.00.00 / 002.085_000` and composition 8:

- **Ubuntu:** MBIM via `cdc_mbim`, LTE registration/attach and real Internet traffic passed;
- **Windows:** the modem was recognized automatically as Mobile Broadband, displayed **Vodafone RO LTE**, connected immediately, and passed real Internet testing;
- **macOS Tahoe:** the existing Cellular.app package worked immediately after installation, without source changes.

This is the currently validated MC7455 deployment state and should be preserved unless there is a specific reason to change firmware or USB composition.

## Read-only checks before changing anything

Useful checks include:

```text
ATI
AT!IMPREF?
AT!USBCOMP?
AT+COPS?
AT+CGDCONT?
```

On some firmware builds, Sierra composition management is more reliably exposed through QMI DMS than through the AT `!USBCOMP` command. Always verify the actual interfaces after re-enumeration rather than trusting a composition label alone.

## Why firmware matters here

The user-space engine does not need a macOS kernel WWAN driver, but it does require the device to enumerate in a predictable MBIM/NCM layout that libusb can claim. The validated Generic configuration now proves the same production layout on both **EM7455/DW5811e M.2** and **MC7455 Mini PCIe** hardware.

If another card enumerates differently, port or extend the code rather than forcing firmware changes without understanding the module.
