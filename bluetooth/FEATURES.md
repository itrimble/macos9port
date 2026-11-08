# OS999 Bluetooth stack: feature specification and status

The completion contract for the Bluetooth stack. Every **Required**
item is checked only with evidence: a test name or a recorded command.

## Required

- [x] B01 HCI packet codec: command, event and ACL packets with the
  packet type indicator, the handle and packet boundary flags, and a
  decoder that refuses a truncated or mistyped packet. Evidence:
  `test_bt_hci` (test_command_roundtrip, test_acl_roundtrip,
  test_event_roundtrip, test_decode_rejects_garbage).
- [x] B02 Controller engine: commands are queued with the reply they
  expect, the controller's Command Complete and Command Status are
  matched, and events and inbound ACL leave through a sink. Evidence:
  `test_bt_hci` (test_engine_sends_commands, test_engine_dispatch,
  test_engine_wait_status).
- [x] B03 Device discovery and the device database: a standard inquiry
  is answered with the addresses, class of device and signal strength
  of the devices that replied; the database keeps them, updates them
  without losing a name and is bounded. Evidence: `test_bt_device`
  (test_database, test_database_is_bounded), `test_bt_stack`
  (test_discovery_finds_devices).
- [x] B04 Pairing: the controller asks for a PIN, the user supplies it,
  the link is authenticated, encrypted and bonded, and the link key is
  kept for the next connection. A failed authentication ends in a
  failed state instead of hanging. Evidence: `test_bt_pair`
  (test_pin_flow_reaches_paired, test_authentication_failure,
  test_link_key_notification_stores_key), `test_bt_stack`
  (test_connect_and_pair).
- [x] B05 L2CAP channels: a connection request opens a channel on both
  sides, the MTU is negotiated, the configuration exchange finishes,
  and a channel that the peer refuses is closed instead of left open.
  Evidence: `test_bt_l2cap` (test_connect_opens_both_sides,
  test_unsupported_psm_is_refused).
- [x] B06 L2CAP data: payloads travel in both directions, a payload
  larger than the MTU is segmented and reassembled, and a channel
  close is reported to the owner. Evidence: `test_bt_l2cap`
  (test_data_flows, test_large_payload_is_reassembled,
  test_disconnect_closes_channel, test_link_down_closes_channels).
- [x] B07 RFCOMM: the multiplexer channel is brought up with SABM and
  UA, a data channel owns the DLCI of twice its server channel, and
  user data travels in INFO frames. Evidence: `test_bt_rfcomm`
  (test_mux_and_data_channel, test_data_flows_over_the_channel).
- [x] B08 RFCOMM flow control: the receiver grants credit, the sender
  may not send more than the credit, and reading hands the credit
  back. Evidence: `test_bt_rfcomm` (test_credit_limits_the_sender,
  test_explicit_credit_grant).
- [x] B09 BNEP: the connection exchange completes, Ethernet frames
  travel in both directions with the header intact, and the multicast
  filter is carried as a control message. Evidence: `test_bt_bnep`
  (test_bnep_connects_and_carries_frames, test_multicast_filter).
- [x] B10 PAN socket: a socket opens to a peer, frames are routed by
  the source MAC of the peer, the socket reports its state and
  counters, and a frame from an unknown peer is not routed. Frames keep
  their boundaries, so two frames that arrive together are handed over
  one by one. Evidence: `test_bt_bnep` (test_pan_socket_routes_frames,
  test_pan_keeps_frame_boundaries, test_pan_rejects_bad_input).
- [x] B11 Bounds: no buffer grows without a limit. The device database
  is capped, an L2CAP SDU is cut off, a payload that does not fit is
  dropped, and a frame over the cap is refused. Evidence:
  `test_bt_device` (test_database_is_bounded), `test_bt_l2cap`
  (test_large_payload_is_reassembled).
- [x] B12 Hardening: AddressSanitizer and UndefinedBehaviorSanitizer
  are clean, and the macOS `leaks` tool reports no leak in any test
  binary. Evidence: `make -f bluetooth/bluetooth.mk bt-asan`, and
  `leaks --atExit -- build/bluetooth/test_bt_*`.
- [x] B13 End to end: discovery, connection, pairing, an RFCOMM
  channel carrying data and a BNEP link carrying frames all work
  through one simulated controller. Evidence: `test_bt_stack`.
- [x] B14 macOS accelerator: device inquiry and L2CAP channels through
  IOBluetooth, with the channel handed to the stack so RFCOMM and BNEP
  run on it unchanged. Evidence: `make bt-darwin` (compiles against
  the macOS SDK).
- [x] B15 The guest sees the PAN: `bt_panif` binds a PAN socket to the
  guest IP stack in `net/`, and a guest MacTCP stream opened to a peer
  on that network crosses BNEP, L2CAP and the controller and comes back
  the other way. Evidence: `test_bt_pan_guest`
  (test_guest_tcp_over_the_pan).

## Not yet done

- [ ] A real radio. Everything above runs against the controller
  simulator. The Darwin accelerator is compile checked against the
  SDK, but no device has been opened with it, so the IOBluetooth path
  is unverified at runtime.
- [ ] SDP. The stack has no service discovery, so RFCOMM server
  channels are chosen by number rather than looked up.
- [ ] Secure Simple Pairing. The pairing machine does the legacy PIN
  flow only.
- [ ] Low Energy. Discovery is the classic inquiry; there is no LE
  advertising scan.
- [ ] The desktop opens a PAN. `bt_panif` binds a personal area network
  to the guest IP stack and the guest reaches a peer over it, but the
  desktop does not yet offer the user a way to pick the peer or to bring
  the link up.

## Host validation

- [x] macOS (Apple Silicon): `make test-bluetooth`, `make bt-asan` and
  `leaks --atExit` over every test binary.
