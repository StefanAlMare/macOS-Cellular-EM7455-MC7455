CELLULAR IPv6 MATRIX PROBE v1.0

Temporary capability test for EM7455 / MBIM.

Scenarios:
A. broadband + IPv4v6
B. NULL APN + IPv4v6
C. broadband + IPv6-only
D. NULL APN + IPv6-only

It does NOT modify the installed Cellular.app.
It does NOT create utun or macOS routes/DNS.
It does NOT modify Wi-Fi or Ethernet.

Run only while the installed Cellular data engine is disconnected:
  ./run-probe.sh
