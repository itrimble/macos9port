# OS999 storage stack

Clean-room C11 code for checklist items L02-L04: classic Mac containers
and archives, disk images, HFS/HFS+, and writable guest volumes. No SDL,
no global state beyond lazily built CRC tables; zlib is required, libbz2
is used when present. `FEATURES.md` is the completion contract.

| Area | Formats | Notes |
| --- | --- | --- |
| Encodings | MacBinary I/II/III, BinHex 4.0, AppleSingle/Double v1/v2 | decode + encode |
| Archives | ZIP (+`__MACOSX`), StuffIt 1.x-4.x, StuffIt 5, Compact Pro | StuffIt methods 0-3 native; 13, 15 and .sitx through `unar` |
| Images | Disk Copy 4.2, NDIF, UDIF, raw; APM, GPT, HFS wrapper | UDIF LZFSE/LZMA and NDIF KenCode are reported |
| Filesystems | HFS read/write, HFS+/HFSX read, ISO9660 primary tree | APFS is reported; ISO interleaving and multi-extent files are rejected |
| Volumes | host folder + `<name>.rsrc` AppleDouble sidecars | openat containment, quotas, journaled saves, install manifests |

## Use

```sh
make -f storage/storage.mk test-storage      # headless tests
make -f storage/storage.mk storage-asan      # ASan/UBSan + fuzzing
sh storage/scripts/ralph-storage-loop.sh     # the Ralph gate
build/os999-unpack -l Game.sit.hqx           # list
build/os999-unpack -x Game.sit.hqx "$HOME/Library/Application Support/OS Pi/Volume" Applications
```

`OS999_STORAGE_FIXTURES=<dir>` enables real-file checks (Aaru StuffIt and
Compact Pro corpora, an HFS floppy, a UDIF dmg). Keep those files
outside Git. `OS999_STORAGE_FUZZ_ITERS` sets fuzzing depth.

## API

- `os999_storage.h`: decoders, encoders, `st_hfs_read`, `st_hfs_write`.
- `os999_unpack.h`: `st_detect`, `st_unpack`, `st_unpack_file`,
  `st_import_tree`.
- `os999_mount.h`: disposable image snapshots, read-only by default, explicit scratch directory and cleanup; source images never change.
- `os999_volume.h`: `st_vol_open`, `st_vol_write`, `st_vol_read`,
  `st_vol_install`, and friends.

All functions return an `StStatus` and fill an `StErr` message. Paths
are always guest-relative; ".." and symlinks never resolve.

## Clean-room notes

Formats were implemented from public descriptions and from comparing
real archives with their stored copies (Compact Pro LZH block rules,
StuffIt 5 entry layout). The NDIF `bcem` layout was cross-checked with
the BSD-licensed ndif2raw tool. No Apple code, ROMs or GPL code is used.
StuffIt method 13's built-in tables and Arsenic need constants we cannot
reproduce clean-room, so they go to `unar`.

## Desktop and mount integration

Local `.sit`, `.bin`, `.hqx`, `.zip`, `.cpt`, `.img`, `.dmg`, and `.iso`
files use `install_archive` and the existing transactional volume installer.
Installs exceeding the 4096-entry recovery plan fail before writing.
Decoded contents open as a folder; no installer executes during extraction.
`install_sit` remains a compatibility wrapper. Unsupported StuffIt compression
still needs the separately installed `unar`/`lsar` tools.

`st_mount_open(image, scratch, options, limits, &mount, &err)` fully decodes an
image before creating a private snapshot under the caller's scratch directory.
Use `st_mount_volume` to access it and `st_mount_close` to remove it. A writable
snapshot changes only that copy. This is a userspace volume, not a host mount.
The API is implemented and tested; Finder does not yet expose mount/eject UI.
HFS hybrid ISO images preserve Mac forks. Plain ISO9660 primary-tree files have
only their data fork; Joliet/Rock Ridge alternate names and metadata are not
imported, and no missing Mac resources are synthesized.

`application_probe.h` provides exact-path selection and rejects ambiguous
archives. Native launch uses the same APPL/resource signature checks, accepts
CODE 0, and reports unsupported CFM/PEF instead of misrouting those files.
These checks identify structure and architecture; they are not cryptographic
publisher authentication or proof that an application will run successfully.

Synthetic lifecycle fixtures live in `tests/test_storage_lifecycle.c` and run
with `make test-storage`. Set `TMPDIR` to a private directory under a worktree
when all generated test files must remain there.
