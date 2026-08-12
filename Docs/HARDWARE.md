# Hardware

## Validated platform

- Dell Precision 7720
- internal M.2 WWAN slot
- Dell DW5811e / Sierra Wireless EM7455B
- tested on macOS Tahoe 26.6.1 (25G76)

## USB identities observed during the project

| Identity | Meaning in this project |
|---|---|
| `413c:81b6` | Dell OEM DW5811e identity observed before/around firmware work |
| `1199:9071` | Sierra Generic identity used by the validated MBIM data engine |

The telemetry helper has fallback support for both IDs, but the current production MBIM engine opens `1199:9071` directly.

## Validated USB layout

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

The engine intentionally treats these values as hardware-specific. Do not assume another WWAN modem uses the same layout.

## EM7455 capabilities

The EM7455 is an LTE-Advanced / UMTS modem. The project name `Cellular` is technology-neutral at the UI level, but this hardware is **not a 5G modem**.

## Porting to other hardware

A different modem needs, at minimum:

1. USB VID/PID identification;
2. MBIM control/data interface discovery;
3. endpoint discovery;
4. validation of NCM framing and session ID;
5. AT interface discovery if telemetry is desired;
6. APN/context validation;
7. sleep/wake and re-enumeration testing.

Do not submit a claim of compatibility based only on the presence of an M.2 WWAN slot.
