# Integration gate: candidate-owned pins

The gate (`scripts/integration-gate.sh`) builds a candidate worktree. Every external
version it needs is read from that candidate; the watcher's launch-agent environment
supplies locations only.

| Input | Pinned in the candidate | Bytes live in |
| --- | --- | --- |
| CEF version and archive SHA-1 | `browser/cef-pin.txt` (`CEF_VERSION`, `CEF_SHA1_*`) | `$OS999_DEPS_STORE/cef/<version>/` |
| Quake engine commit | `config/quake-pin.txt` (`QUAKE_COMMIT`, optional `QUAKE_ROOT_COMMITS`) | repository at `OS999_QUAKE_REPOSITORY` |
| SDL2 version and SHA-256 | `scripts/prepare-sdl2.sh` | archive cache `$OS999_DEPS_STORE/archives/` |
| Musashi revision and SHA-256 | `scripts/prepare-musashi.sh` | candidate `build/deps/` |

`$OS999_DEPS_STORE` defaults to `~/Library/Application Support/OS999/Integration/deps`.
A legacy flat directory `cef-<version>` in the store is accepted (recorded as
`legacy-unverified` because it has no `.archive-sha1` marker). The gate never looks in
any checkout's `build/deps` for CEF.

## Preflight

The first gate stage, `pins` (`scripts/gate_pins.py preflight`), runs in seconds and
reports every problem at once before SDL or browser builds start:

- CEF entry missing from the store, wrong `.version`, no `include/`, or an
  `.archive-sha1` marker that disagrees with the candidate pin.
- Quake commit not 40-hex, absent from the repository, repository identity
  (`QUAKE_ROOT_COMMITS`) mismatch, or `include/os999_quake_bridge.h` differing from
  `code/darwin/os999_bridge.h` at that commit (read with `git show`, no checkout).
- `OS999_QUAKE_REF` in the environment that differs from the pin: **fails**, naming both.
  A matching value is accepted. `OS999_CEF_ROOT` in the environment is **ignored with a
  logged warning** (it is a path, no longer an input).

A missing CEF is not fetched by default. Fetch it with
`sh browser/scripts/fetch-cef.sh macosarm64 --store "$OS999_DEPS_STORE"` (verifies the
pinned SHA-1, extracts atomically, writes `.version` and `.archive-sha1`), or set
`OS999_DEPS_FETCH=1` so the preflight runs that command itself.

The preflight writes `pins.json` (store, CEF version, archive SHA-1 and how it was
checked, resolved path, Quake repository and commit, SDL2 and Musashi pins) and
`pins.env` (exports sourced by the gate). `pins.json` is embedded in `receipt.json`
under `inputs.pins`.

Test: `make test-gate-pins` (`tests/test_gate_pins.py`, temp trees and a tiny git repo).
