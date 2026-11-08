# net/: OS999 guest networking

A host-side service that lets classic programs use the network through
MacTCP without ever touching host sockets themselves.

- `gn_policy.c`: the policy file grammar and decisions (deny by default).
- `gn_broker.c`: nonblocking sockets, bounded buffers, a single `gn_pump`.
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

Build and test: `make test-net`, or run the gate with
`sh net/scripts/ralph-net-loop.sh`. Status: `FEATURES.md`.
