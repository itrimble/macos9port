# Bluetooth recovery checkpoint

Recovered from clean producer `feature/bluetooth-stack` at `537e274` in a
separate `recovery/bluetooth-pan` checkout. Original producer and running
sessions were left untouched. The desktop PAN plan was documentation only.

## Implemented backend

`desktop_guestnet` owns a broker and MacTCP service; attaching a caller-owned
PAN publishes its address. Detach clears both address fields and fails/releases
the old PAN TCP endpoints before their link can be freed. The permissive legacy
policy is the approved design's explicit policy; this backend is not wired into
the running desktop or guest runtime and is not a general security boundary.

`desktop_bluetooth` supplies the bounded discovery list and driver callback seam.
It rejects overlapping scans/connections, cancels pending connects, ignores late
completion after cancellation, and detaches the broker before driver destruction.
Drivers must deliver callbacks on the caller thread and quiesce callbacks on
free. The no-radio backend reports unavailable. There is no real-radio driver
connected to this manager yet.

## Original protocol finding and remaining integration

At the first recovery checkpoint the BNEP stack was a simulator-compatible subset, not a demonstrated
standard PAN implementation. Its control sender wrote a five-byte setup request
without service UUIDs; its receiver expected at least five bytes with data at
byte five. The Bluetooth SIG test suite specifies a seven-byte setup request
with UUID length and destination/source service UUIDs, and a four-byte success
response. See [Bluetooth SIG BNEP test suite, tables 4.6 and 4.7](https://files.bluetooth.com/wp-content/uploads/2024/10/BNEP.TS_.p09.pdf).

The follow-up protocol commit corrects setup/control framing, removes the private
disconnect opcodes, adds independent wire vectors and safe host-channel adopt,
closure and callback detachment. See [protocol follow-up](BLUETOOTH-WIRE-2026-11-08.md).
Physical interoperability remains unverified; no radio inquiry, pairing or
user-device disconnect was attempted.

The menu, panel facade, and host lifecycle wiring overlap the active Control
Panels producer and remain unmodified. Runtime hooks for real classic programs
also remain separate. The original desktop plan is not marked complete.

## Validation

Focused tests: service 23 checks, manager 41 checks, and 31 synthetic guest TCP
checks crossing the existing BNEP/L2CAP/controller simulator. These establish
backend ownership and simulated transport only, not hardware interoperability.
`BUILDFLEET_LOCAL=1 make -j2 test all` passed with the private genuine SDL2
2.32.10 pkg-config path. Desktop and runtime binaries built. The final incremental
whole-suite pass includes `test-desktop-pan`, which executes all three new tests.
The existing Bluetooth ASan/UBSan suite and all three new backend tests also
passed under AddressSanitizer/UndefinedBehaviorSanitizer. Existing linker
common-section alignment warnings remain. Ignored local logs are in
`build-logs/recovery-validation.log` and `build-logs/recovery-asan.log`.
