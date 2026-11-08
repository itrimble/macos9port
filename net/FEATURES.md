# OS999 guest networking: feature specification and status

The Ralph gate's completion contract for checklist N01-N08, phase 1
(host-side service, no runtime edits). `scripts/ralph-net-loop.sh`
fails until every **Required** item is checked, and an item is checked
only with evidence (a test name or a recorded command).

Phase 2 (runtime hook) is listed separately, because it waits for the
L04 worktree to land in `mac_runtime.h` and `mac_files.c`.

## Required

- [x] N01 Socket broker: nonblocking TCP connect, listen and accept, and
  UDP send/receive over IPv4 and IPv6. One poll-driven pump, never
  blocks the guest. Evidence: `test_gn_broker` (IPv4 echo of 200 KB,
  listen/accept, UDP loopback; the IPv6 echo runs when the host has
  an IPv6 loopback).
- [x] N02 Bounded resources and failure handling: per-connection send
  and receive caps with back-pressure, a connection limit, connect
  timeouts, refusal, peer close, abort, invalid ids. The resolver runs
  in the background with cancellable lookups and rejects bad names.
  Evidence: `test_gn_broker` (failures, resolver).
- [x] N03 MacTCP stream model: stream pointers, receive areas, state
  codes, Status windows, and ipctlGetAddr. Evidence: `test_gn_mactcp`.
- [x] N04 MacTCP TCP calls: TCPCreate, Passive and Active Open, Send
  (WDS gather), Rcv, NoCopyRcv/RcvBfrReturn (RDS lending), Close,
  Abort, Status and Release, sync and async, with the public error
  codes. Evidence: `test_gn_mactcp` (test_tcp, test_passive).
- [x] N05 Async completion and ASR delivery as guest-call records
  (ioCompletion with the PB; TCPDataArrival, TCPClosing, TCPTerminate),
  plus UDP Create/Read/BfrReturn/Write/MTU/Release and DNR StrToAddr
  (dotted, resolver, cache). Evidence: `test_gn_mactcp` (ASR, abort
  completes pending, UDP, DNR).
- [x] N06 Policy: deny by default; allow/deny rules by direction,
  protocol, host (name, `*.suffix`, IP, CIDR), ports and app; first
  match wins; loopback and private ranges are off unless `local on`;
  listeners use loopback unless `lan-listen on`. Evidence:
  `test_gn_policy`, `test_gn_broker` (policy gate, loopback listen).
- [x] N07 Plaintext disclosure: guest traffic needs the `legacy`
  profile per program, and every flow reports `plaintext = 1`.
  Evidence: `test_gn_policy` (test_defaults), `test_gn_broker`.
- [x] N08 Diagnostics carry metadata only and pass through the
  redactor (PASS, Authorization, Cookie, password=/token=, URL
  userinfo). Evidence: `test_gn_policy` (test_redact).
- [x] N09 Hardening: ASan/UBSan clean; deterministic fuzzing of the
  policy text, redactor and arbitrary MacTCP PBs, including
  out-of-range pointers and live loopback connections. Evidence:
  `make -f net/net.mk net-asan` (`fuzz_gn`).
- [x] N10 Personal area network: the guest IP stack runs over a link
  that carries Ethernet frames, so guest TCP crosses a Bluetooth PAN
  with the guest's own address. ARP answers for our address, IPv4 and
  TCP carry their Internet checksums, the TCP endpoint does the
  handshake, moves data both ways and closes, and the broker routes a
  connection to an address on the PAN subnet over that link instead of
  a host socket. Evidence: `test_gn_pan` (test_subnet, test_arp,
  test_handshake, test_data, test_close, test_broker_endpoint) and
  `test_bt_pan_guest` in `bluetooth/`.

## Phase 2 (runtime hook, after L04 lands)

- [ ] Runtime `_Open ".IPP"` returns the MacTCP refNum; `_Control`
  (0xA004) is sent to `gn_mactcp_control` with the guest RAM view.
- [ ] Guest calls run on the 68k CPU between instructions (ioCompletion
  in A0; ASR with stream, event, userDataPtr on the stack).
- [ ] DNR: synthesize the `dnrp` resource entry points (OpenResolver,
  StrToAddr, AddrToName, CloseResolver) over `gn_mactcp_str_to_addr`.
- [ ] Desktop policy UI: per-program legacy toggle with a "plaintext"
  warning; the policy file lives in the user's OS999 settings folder.
- [ ] A real program (for example Fetch or NCSA Telnet) reaches a
  loopback FTP/telnet server under the gate.

## Host validation

- [x] Linux x86_64 (cloud VM): gate PASS (see build/ralph-net).
- [ ] macOS (Apple Silicon): `sh net/scripts/ralph-net-loop.sh`. This
  also runs the IPv6 echo and the SO_NOSIGPIPE paths.
