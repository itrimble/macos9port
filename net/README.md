# net/: OS999 guest networking

A host-side service that lets classic programs use the network through
MacTCP without ever touching host sockets themselves.

- `gn_policy.c`: the policy file grammar and decisions (deny by default).
- `gn_broker.c`: nonblocking sockets, bounded buffers, a single `gn_pump`.
- `gn_broker_pan.c`: the broker's personal area network endpoints.
- `gn_panip.c`, `gn_pantcp.c`: IPv4, ARP and TCP over a link that carries
  Ethernet frames.
- `gn_dns.c`: a background resolver with cancellable lookups.
- `gn_mactcp*.c`: the MacTCP `.IPP` driver interface over guest RAM.
- `gn_redact.c`: credential masking for anything that is logged.

Policy example (`net-policy.txt`):

    legacy on app=Fetch
    allow out tcp ftp.example.org 20,21 app=Fetch
    allow out udp * 53

MacTCP has no TLS, so guest traffic is plaintext. It stays off unless a
program gets the `legacy` profile, and every flow reports itself as
unencrypted so the desktop can show that.

## A personal area network

`net/` also owns the guest IP stack for a link that carries Ethernet
frames, which is what a Bluetooth personal area network is. Attach one
to the broker and a guest connection to an address on that network
travels over the link, with the address the guest was given:

```c
gn_broker_set_pan(broker, pan);   /* pan carries Ethernet frames */
```

The broker picks the link by subnet: an address on the PAN goes over
the PAN, and everything else still uses a host socket. `net/` never
mentions Bluetooth; `bluetooth/` supplies the frames through
`bt_panif`.

Build and test: `make test-net`, or run the gate with
`sh net/scripts/ralph-net-loop.sh`. Status: `FEATURES.md`.
