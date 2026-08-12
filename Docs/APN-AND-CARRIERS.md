# APN and carrier handling

## Why naive APN auto-detection failed

The test EM7455 contained stale carrier state from earlier use. With a TELEKOM.RO SIM inserted, modem profile queries still exposed:

```text
internet.vodafone.ro
```

Using that stored profile caused the MBIM connection to fail repeatedly.

The current network registration reported:

```text
MCC/MNC: 22603
Operator: TELEKOM.RO
```

while the stale Vodafone APN remained in saved contexts.

## Active-context result

With the known-working connection active, read-only AT queries showed:

```text
+CGACT: 1,0
+CGACT: 2,1
```

CID 2 was active.

`AT+CGCONTRDP` returned the active bearer as:

```text
CID 2 ... Broadband ... IPv4 ... DNS ...
```

That result became the basis for the current APN strategy.

## Current strategy

1. normalize carrier name;
2. look up a locally validated APN for that carrier;
3. try cached APN first;
4. on a new carrier, attempt network-default / NULL AccessString;
5. use `broadband` fallback where appropriate;
6. after a successful connection, query the active context through `CGCONTRDP`;
7. store the actually used APN in the local cache.

Cache file:

```text
/Library/Application Support/CellularLTE/apn-cache.tsv
```

## Important principle

Saved `CGDCONT` or MBIM provisioned contexts are evidence, not unquestionable truth. They can belong to a previous SIM/operator.

For new carriers, validate the active bearer rather than shipping a giant hard-coded APN database.
