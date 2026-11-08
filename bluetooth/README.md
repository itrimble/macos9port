# bluetooth/: the OS999 Bluetooth stack

A Bluetooth stack for OS999: HCI, L2CAP, RFCOMM and BNEP, written in
C11 with no SDL and no windowing. It is the Bluetooth counterpart of
`net/`, which carries guest TCP and UDP traffic over MacTCP.

- `bt_hci` — the controller engine: packet codec plus the command and
  event state machine.
- `bt_l2cap` — logical channels over a baseband link: signalling, MTU
  negotiation, CID allocation, segmentation and reassembly.
- `bt_rfcomm` — serial ports over L2CAP: DLCI addressing, framing and
  credit based flow control.
- `bt_bnep` — Ethernet encapsulation, so a peer becomes a network link.
- `bt_pan` — a link-layer socket over BNEP for the guest network stack.
- `bt_pair` — the pairing machine: PIN entry, authentication, encryption
  and bonding.
- `bt_device` — the device database that discovery fills.
- `bt_stack` — the top level object that routes between all of them.

## Layers

```
        bt_hci            command, event and ACL packets
           |
        bt_l2cap          channels, signalling, MTU, segmentation
           |
     +-----+------+
     |            |
 bt_rfcomm     bt_bnep -> bt_pan
 DLCI, credit   Ethernet frames, route by MAC
```

The layers do not know about each other. RFCOMM and BNEP only need an
open L2CAP channel, so they run unchanged over a channel that the
operating system opened for us (see below).

## Connecting a device

```c
BtStack *stack = bt_stack_new(transport, log, ctx);

bt_stack_discover(stack, 8, 5);      /* standard inquiry */
bt_stack_pump(stack, 0);             /* read what the controller said */

BtAddress peer;
bt_addr_parse("00:11:22:33:44:55", &peer);
bt_stack_connect(stack, &peer, 0xCC18);
bt_stack_pump(stack, 0);

uint16_t handle;
bt_stack_handle_for(stack, &peer, &handle);

/* Pair, then open a channel. */
bt_pair_begin(bt_stack_pair(stack), &peer, handle);
bt_stack_pump(stack, 0);
bt_pair_set_pin(bt_stack_pair(stack), &peer, pin, 4);

int cid;
bt_l2cap_connect(bt_stack_l2cap(stack), handle, &peer, BT_PSM_RFCOMM,
                 2048, NULL, NULL, &cid);
```

## The socket shape

`bt_pan` mirrors the broker in `net/`, so the guest network stack can
adopt a Bluetooth personal area network the same way it adopts a TCP
connection:

```c
int id;
bt_pan_open(pan, handle, &peer, peer_mac, &id);
bt_pan_send(pan, id, frame, len);
long n = bt_pan_recv(pan, id, frame, sizeof frame);
bt_pan_info(pan, id, &info);
bt_pan_close(pan, id);
```

## The macOS accelerator

`bt_darwin.m` uses IOBluetooth for device inquiry and for opening an
L2CAP channel. IOBluetooth does not expose raw HCI packets, so the
system cannot replace the controller engine; instead the channel it
opens is handed to the stack through `bt_l2cap_adopt()`, and RFCOMM and
BNEP run on top of it unchanged.

Build it with `make bt-darwin`. It is Objective-C against IOBluetooth
and is kept out of the C build so the rest of the stack stays plain C.

## Build and test

    make test-bluetooth        # from the repository root
    make -f bluetooth/bluetooth.mk test-bluetooth
    make bt-asan               # AddressSanitizer and UndefinedBehaviorSanitizer

Every test is its own binary built from one `bluetooth/tests/test_bt_*.c`
and uses the `CHECK` macro, so it runs headless with no display.

The end to end test drives a controller simulator (`bt_hci_sim.c`): the
simulator answers HCI commands the way a controller does and carries
the ACL link to a peer stack, so discovery, pairing, RFCOMM and BNEP
are all exercised end to end with no radio.

## Bounds

Nothing in the stack grows without a limit. The device database holds
64 entries and evicts the oldest, an L2CAP SDU is cut off at 4 KiB, a
reassembled payload is dropped rather than allocated over the cap, and
a frame that does not fit the receive queue is dropped instead of
queued. A peer cannot make the desktop allocate by announcing a large
payload.
