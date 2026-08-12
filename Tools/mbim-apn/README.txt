CELLULAR MBIM APN PROBE v1.1

Read-only.
Uses the exact MBIM OPEN initialization path and 4096-byte response
buffer from the validated Cellular engine.

Queries:
- CID 6  HOME_PROVIDER
- CID 9  REGISTER_STATE
- CID 13 PROVISIONED_CONTEXTS

No SET. No CONNECT. No profile changes.

Run only while Cellular data engine is disconnected:
  ./run-probe.sh
