# Troubleshooting

## Cellular says Connecting and then disconnects

Check:

```bash
sudo tail -200 "/Library/Application Support/CellularLTE/engine.log"
sudo tail -200 "/Library/Application Support/CellularLTE/helper.log"
```

Confirm the modem enumerates as the expected hardware and that no other process owns its MBIM interfaces.

## Wrong APN appears

Do not assume saved modem contexts belong to the current SIM.

Run the active-context probe in `Tools/active-context/` while Cellular is connected and inspect `AT+CGCONTRDP`.

## Menu app shows stale state

The helper state file should update continuously:

```bash
cat "/Library/Application Support/CellularLTE/state.json"
```

## Signal does not change bars

Very strong values such as approximately -54 to -61 dBm remain in the top four-bar bucket even though the dBm value changes. This is expected.

## Internet works on Ethernet/Wi-Fi but not Cellular

Inspect route selection:

```bash
route -n get 1.1.1.1
```

When Cellular is active, the route should point to the Cellular utun.

## IPv6 says Carrier unavailable

This means the Cellular bearer did not supply usable IPv6. It does not mean macOS Ethernet/Wi-Fi IPv6 was disabled.

Use `Tools/ipv6-matrix/` to test a new SIM/operator before modifying the production engine.

## Do not post personal modem identifiers

Redact IMEI, IMSI, ICCID, phone number and SIM authentication data from logs before opening an issue.
