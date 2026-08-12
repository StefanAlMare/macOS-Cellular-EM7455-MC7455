# Cellular IPv6

## Current result

Native Cellular IPv6 was actively tested and was **not available on the tested mobile bearer**.

The test matrix was:

| APN | Requested type | CONNECT | Returned bearer | IPv6 flags | IPv6 packets |
|---|---|---:|---|---:|---:|
| `broadband` | IPv4v6 | yes | IPv4 | 0 | 0 |
| NULL/default | IPv4v6 | yes | IPv4 | 0 | 0 |
| `broadband` | IPv6-only | no | — | — | — |
| NULL/default | IPv6-only | no | — | — | — |

Both IPv4v6 requests were accepted, but the modem/network returned `IP type = 1` (IPv4). Both IPv6-only requests failed.

A second SIM tested independently on an iPad also reported no IPv6 connectivity.

## What this means

The current limitation is the tested carrier/SIM/bearer, not macOS as a whole.

When Ethernet or Wi-Fi provides IPv6, macOS can continue to use that IPv6 normally. Cellular does not disable IPv6 on those interfaces.

In manual mode, a machine can therefore become split-stack:

- IPv4 through Cellular
- IPv6 through Ethernet/Wi-Fi

That is expected with the current routing design.

## Future native IPv6 work

When a carrier finally supplies IPv6, the engine must support both possible MBIM behaviours:

1. IPv6 parameters in `MBIM_CID_IP_CONFIGURATION`;
2. Router Advertisement / DHCPv6 packets forwarded over the MBIM data plane.

USB/MBIM implementation guidance expects RA and DHCPv6 packets to be passed to the host when the network uses those mechanisms.

Official reference:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/network/mobile-broadband-implementation-guidelines-for-usb-devices

## Repository readiness

The current UI/helper already detect and display a global IPv6 address if a future engine configures one on the Cellular utun.

The `Tools/ipv6-capability` and `Tools/ipv6-matrix` probes are included so future SIMs/operators can be tested before changing production code.
