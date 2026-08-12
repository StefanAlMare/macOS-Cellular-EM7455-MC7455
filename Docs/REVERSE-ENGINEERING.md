# Reverse-engineering notes

This project was developed experimentally on real hardware rather than beginning from a working macOS WWAN driver.

## Initial condition

Modern macOS could enumerate the EM7455 USB device, but there was no usable native mobile-broadband network service for the hardware.

The project therefore investigated the USB device directly.

## Key discoveries

### USB layout

The working Sierra Generic identity was `1199:9071`.

The useful functions were recovered as:

```text
AT       interface 3
MBIM     interface 12
NCM data interface 13
```

### Linux-like NCM initialization mattered

A stable MBIM open required the NCM data alternate-setting sequence used by Linux-style drivers:

```text
ALT 1 -> ALT 0 -> GET_NTB_PARAMETERS -> ALT 1
```

Attempts to over-optimize this initialization later caused a real regression, confirming that the ordering/timing is not cosmetic.

### MBIM control path

The project then implemented:

- `MBIM_OPEN`
- `OPEN_DONE`
- Subscriber Ready query
- Register State query
- Packet Service query
- Signal State query
- CONNECT Session 0
- IP Configuration query
- CONNECT deactivation
- `MBIM_CLOSE`

### Raw data-plane proof

Before creating a macOS network interface, the implementation built a raw IPv4/UDP/DNS packet, encapsulated it in an NCM/MBIM NTB16 frame and sent it through USB.

Receiving a DNS response proved that:

- MBIM activation was real;
- IP configuration was usable;
- the NTB/NDP layout was correct enough for bidirectional traffic.

### macOS utun bridge

The next step connected the raw MBIM data path to macOS:

```text
utun -> MBIM/NCM -> modem
modem -> MBIM/NCM -> utun
```

The engine also publishes temporary SystemConfiguration state and routes.

### Application lifecycle problems solved

Development exposed several non-radio issues:

- stale PID file causing Disconnect to signal the wrong PID;
- SIGINT not stopping the bridge reliably;
- UI deadlock caused by synchronously waiting for `ps` before draining its pipe;
- automatic fallback racing manual connect/disconnect;
- APN profile data belonging to a previous SIM;
- misleading `Connected` state before utun/IP/routes were actually ready;
- startup delays caused by repeated APN discovery.

The current architecture separates:

- privileged engine/helper work;
- tiny state/command files;
- non-blocking menu-bar UI.

## Timing result

On the validated setup, a representative stable startup measured roughly:

```text
+0 ms      engine start
+3067 ms   MBIM OPEN complete
+3323 ms   preflight complete
+3451 ms   bearer activated (cached broadband APN)
+3515 ms   first IP configuration complete
+4070 ms   utun + routes ready
~5.0 s     helper reports Connected
```

The ~3-second MBIM OPEN dominates the startup time; later stages are comparatively fast.

## Historical source

The project was developed through many one-off diagnostic C programs and terminal experiments before consolidation into `Sources/mbim_lte.c`. The reproducible probes retained in `Tools/` are the cleaned-up diagnostic lineage intended for public use.
