# Hardware

## Validated platforms

The same Cellular engine is now hardware-validated with both Sierra 9X30 form factors used in this project:

| Platform | WWAN card | Form factor | macOS | Result |
|---|---|---|---|---|
| Dell Precision 7720 | Dell DW5811e / Sierra Wireless EM7455B | internal M.2 WWAN | Tahoe | ✅ Validated |
| Dell Precision M6800 | Sierra Wireless MC7455 | internal full-size Mini PCIe WWAN | Tahoe | ✅ Validated |

The MC7455 validation is significant because **the existing Cellular.app and MBIM engine worked unchanged** once the modem itself had been prepared with the Generic firmware and the same MBIM USB composition/layout. No MC7455-specific macOS code change was required.

This should be read as validation of the **Sierra EM7455/MC7455 9X30 family using the documented USB layout**, not as a claim that every M.2 or Mini PCIe modem is automatically compatible.

## USB identities observed during the project

| Identity | Meaning in this project |
|---|---|
| `413c:81b6` | Dell OEM DW5811e identity observed before/around firmware work |
| `1199:9071` | Sierra Generic identity used by the validated MBIM data engine on both EM7455 and MC7455 |

The telemetry helper has fallback support for both IDs, while the current production MBIM engine opens `1199:9071` directly.

## Validated USB layout

Both the EM7455/DW5811e M.2 card and the MC7455 Mini PCIe card were validated with the same production layout:

| Function | Interface / endpoint |
|---|---|
| AT control | interface 3 |
| AT OUT | `0x03` |
| AT IN | `0x84` |
| MBIM control | interface 12 |
| MBIM interrupt | `0x87` |
| MBIM/NCM data | interface 13 |
| data OUT | `0x04` |
| data IN | `0x86` |

For the MC7455, the final working USB composition is **composition 8: DM / NMEA / AT / MBIM**. This matches the layout already expected by the existing macOS engine.

## MC7455 / Dell Precision M6800 preparation

The tested MC7455 was received in its shipped/default **USB composition 7**. For the final cross-platform configuration it was moved to **composition 8**, after flashing the Sierra Generic firmware/PRI documented in `FIRMWARE.md`.

The Dell Precision M6800 also required platform-specific Mini PCIe compatibility work:

- Mini PCIe pins **23, 25, 31 and 33** were insulated on the tested card to avoid the M6800 conflict with the MC7455 USB 3.0/SuperSpeed signals. The modem then operates over USB 2.0 High-Speed, which is fully adequate for this LTE/MBIM setup.
- Mini PCIe pin **20** was insulated on this M6800 because the platform asserted `W_DISABLE`, leaving the modem in low-power/radio-disabled state. After isolating pin 20, the modem reported `W_DISABLE: 0`, `ONLINE`, and normal radio operation.

These pin modifications are **specific to the tested Dell Precision M6800/MC7455 combination**. Do not treat them as a universal MC7455 installation recipe for other laptops.

### Antenna note from the M6800 validation

The M6800 had three available antenna leads. On the tested machine, moving the lead that had been connected to GPS onto the MC7455 AUX connector dramatically improved LTE diversity reception: in the validation snapshot MAIN and AUX were effectively matched (approximately `RSRP -109/-108 dBm`) instead of AUX being roughly 25 dB weaker in the earlier arrangement. Antenna construction varies by laptop, so validate MAIN/AUX performance on the actual hardware rather than relying only on cable labels.

## Cross-platform result for MC7455 composition 8

The same final MC7455 configuration was validated across all three operating systems used during bring-up:

- **Ubuntu:** ModemManager/NetworkManager over `cdc_mbim`, LTE attach and real Internet traffic validated.
- **Windows:** automatically recognized as Mobile Broadband / Vodafone RO LTE; connection and real Internet traffic validated.
- **macOS Tahoe:** existing Cellular.app installed and worked immediately, with no source-code modification required.

This makes composition 8 the validated common denominator for this MC7455 deployment.

## EM7455 / MC7455 capabilities

The EM7455 and MC7455 are LTE-Advanced / UMTS modems from the Sierra 9X30 family. The project name `Cellular` is technology-neutral at the UI level, but these devices are **not 5G modems**.

## Porting to other hardware

A different modem family still needs, at minimum:

1. USB VID/PID identification;
2. MBIM control/data interface discovery;
3. endpoint discovery;
4. validation of NCM framing and session ID;
5. AT interface discovery if telemetry is desired;
6. APN/context validation;
7. sleep/wake and re-enumeration testing.

The new MC7455 result shows that **M.2 versus Mini PCIe is not itself the limiting factor** when the modem exposes the same validated Sierra Generic MBIM layout. Compatibility should nevertheless be claimed by modem/layout, not merely by connector form factor.
