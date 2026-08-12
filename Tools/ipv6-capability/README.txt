CELLULAR IPv6 CAPABILITY PROBE v1.0

Purpose:
- temporarily request MBIM IPv4v6 on Session 0
- keep APN broadband
- query MBIM_CID_IP_CONFIGURATION several times
- inspect the MBIM/NCM data endpoint for IPv6 and Router Advertisements
- deactivate the temporary bearer
- do NOT create utun
- do NOT change macOS routes, DNS, Wi-Fi, or Ethernet

Run only while installed Cellular data engine is disconnected:
  ./run-probe.sh

The installed Cellular.app is not modified.
