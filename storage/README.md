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
| Filesystems | HFS read/write, HFS+/HFSX read | APFS is reported |
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

## Not yet wired in

The desktop's double-click install (`src/install.c`, `session_sit.c`)
still calls `unar` directly. Switching it to `st_unpack_file` +
`st_vol_install` is the L06 follow-up.
