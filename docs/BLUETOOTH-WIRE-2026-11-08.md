# BNEP wire and macOS transport recovery

Follow-up to backend checkpoint `7d5d21a`. The old control format was mutually
understood only by the in-tree peers. This change uses seven-byte NAP/PANU setup
requests and four-byte status responses, with 16-, 32-, and Bluetooth-base
128-bit UUID validation. Unsupported destination/source roles get the relevant
status; invalid sizes and truncated packets never establish a link. Payload
frames are rejected before successful setup. Unknown commands get the standard
not-understood response.

The standard filter command numbers no longer act as private disconnect
commands. Optional filtering returns an explicit unsupported response. The
public filter request uses range framing. Transport close is reported by the
L2CAP close callback; host-owned channels expose `bt_l2cap_pipe_closed` rather
than emitting raw signalling through the host pipe. Pending callback contexts
and listeners detach safely when a BNEP owner is freed.

Reference: [Bluetooth SIG BNEP test suite](https://files.bluetooth.com/wp-content/uploads/2024/10/BNEP.TS_.p09.pdf),
setup, unknown-command and filter tests. Independent fixed vectors exercise both
wire output and receive behavior, including failure/truncation and 32/128-bit
UUIDs. This is not a full Bluetooth qualification claim: compressed Ethernet,
extension headers, filters and negotiation retry timers remain unsupported.
Extensions/compressed packets are rejected safely, not interpreted as payload.
General Ethernet frames and explicit host channel closure are supported.

## macOS accelerator

The IOBluetooth wrapper now reports controller power availability and a local
address query, adds discovery completion with cancellation suppression, limits
write lengths to the outgoing MTU, and reports synchronous open only after its
pipe is initialized. Remote close clears stale channel state; explicit close
suppresses callbacks. The source compiles against the installed SDK. Eight
contract checks use only invalid inputs and empty lifecycle objects: no inquiry,
controller query, pairing or device connection occurs in those tests.

The subsequent [desktop driver increment](BLUETOOTH-DESKTOP-DRIVER-2026-11-08.md)
links the macOS production backend. Shared host/menu hooks and remaining protocol
capabilities are still separate acceptance gates.
The active Control Panels producer and candidate checkout were not modified.
No physical peer or real classic-program networking acceptance is claimed.

## Validation

Bluetooth suite and 39 independent wire checks pass. The existing stack and new
wire tests pass ASan/UBSan. The macOS contract test passes eight checks. Full
`BUILDFLEET_LOCAL=1 make -j2 test all` uses the private genuine SDL2 pin.
Final gate results and ignored logs live in `build-logs/bnep-*.log`.
