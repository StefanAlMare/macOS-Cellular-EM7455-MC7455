CELLULAR ACTIVE CONTEXT PROBE

Read-only AT diagnostic for Sierra Wireless EM7455.

Can be run while Cellular is connected.

Queries only:
- AT+COPS?
- AT+CGATT?
- AT+CGACT?
- AT+CGDCONT?
- AT+CGCONTRDP
- AT+CGPADDR
- AT!GSTATUS?

It does not change:
- APN profiles
- MBIM session
- Wi-Fi/Ethernet
- routes
- IPv4/IPv6

Run:
  ./run-probe.sh
