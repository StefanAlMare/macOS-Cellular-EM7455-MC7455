# Architecture

## Design goal

Provide usable mobile broadband on modern macOS without depending on a removed/unsupported native WWAN stack for this modem.

## Layers

### 1. Cellular.app

Swift/AppKit menu-bar application.

Responsibilities:

- display carrier name
- display four signal bars
- show dBm
- show APN state
- show IPv4 and Cellular IPv6 state
- enable/disable automatic fallback
- manual connect/disconnect commands
- open logs/support folder

The UI does not directly perform privileged USB/network work.

### 2. CellularLTEHelper

Root LaunchDaemon written in Swift.

Responsibilities:

- monitor Ethernet and Wi-Fi availability through `NWPathMonitor`
- enforce automatic fallback policy
- start/stop the MBIM engine
- maintain per-carrier APN cache
- query active APN and modem telemetry
- publish a small JSON state file consumed by the menu app

### 3. `mbim_lte`

C/libusb engine.

Responsibilities:

- claim the EM7455 MBIM interfaces
- perform Sierra/NCM initialization
- open MBIM control channel
- query modem state
- activate Session 0
- parse IP configuration
- create/configure macOS utun
- translate between utun AF_INET packets and MBIM/NCM NTB16 frames
- publish temporary IPv4/DNS state through SystemConfiguration
- install/remove temporary routes
- shut down Session 0 cleanly

## MBIM initialization

The working sequence reproduced Linux-style NCM preparation:

```text
claim IF12 + IF13
IF13 alt 1
IF13 alt 0
GET_NTB_PARAMETERS
IF13 alt 1
MBIM_OPEN
```

Skipping/reordering parts of this sequence caused real regressions during optimization experiments, so the stable branch intentionally keeps the proven sequence.

## Data plane

Transmit:

```text
utun read
  -> check AF_INET
  -> create NTH16/NDP16
  -> MBIM IPS Session 0
  -> USB bulk OUT 0x04
```

Receive:

```text
USB bulk IN 0x86
  -> parse NTH16/NDP16
  -> extract IPv4 datagrams for Session 0
  -> prepend utun AF_INET header
  -> write utun
```

## Routing

The current IPv4 design installs:

```text
0.0.0.0/1
128.0.0.0/1
```

through the Cellular utun. This avoids modifying the Ethernet/Wi-Fi network-service configuration while making Cellular the effective IPv4 path when active.

SystemConfiguration state is temporary and is removed during cleanup.

## Shutdown

SIGUSR1 is the normal helper-to-engine shutdown signal. The engine removes routes/state, stops the bridge, deactivates MBIM Session 0 and releases USB interfaces.
