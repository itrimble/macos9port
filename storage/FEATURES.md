# OS999 storage: feature specification and status

This file is the Ralph gate's completion contract for checklist items
L02 (fork-preserving containers), L03 (disk images and HFS) and L04
(writable guest volumes). `scripts/ralph-storage-loop.sh` fails until
every **Required** item is checked. Check an item only with evidence:
a test name, a real-fixture result, or a recorded command.

Host validation is tracked separately (bottom). A pass on Linux does
not cover macOS or a Raspberry Pi.

## Required

- [x] SF01 MacBinary I/II/III decode; MacBinary III encode with CRC.
  Evidence: `test_storage_codecs` (round trip, CRC, truncation, limits).
- [x] SF02 BinHex 4.0 decode and encode: 6-bit text, RLE90, three CRCs.
  Evidence: `test_storage_codecs`; fuzzed (`test_storage_fuzz`).
- [x] SF03 AppleSingle/AppleDouble v1 and v2 decode and encode: forks,
  name, dates, Finder info (macOS extended-attribute tail tolerated).
  Evidence: `test_storage_codecs`, `test_storage_archives` (ZIP companions).
- [x] SF04 ZIP: stored, deflate, ZIP64; `__MACOSX/._name` and `._name`
  companions restore forks and Finder info; traversal, encrypted
  entries, CRC errors and entry limits are rejected; symlinks skipped.
  Evidence: `test_storage_archives`.
- [x] SF05 StuffIt 1.x-4.x container: folders, Finder info, dates, both
  forks, header and fork CRCs; methods 0 (store), 1 (RLE90), 2 (LZW),
  3 (Huffman). Evidence: Aaru StuffIt 1.5 corpus decodes identical to
  its STORE copy (31 entries); FSSETTER: 320 entries, 71 resource forks,
  10-level nesting (`test_storage_real`); synthetic archives in
  `test_storage_archives`.
- [x] SF06 StuffIt 5 container: folder tree by parent offset, Finder
  info, both forks, CRCs; method 0. Evidence: Aaru 5.0 NONE corpus
  equals the 1.5 corpus (`test_storage_real`); `test_storage_archives`.
- [x] SF07 StuffIt methods we do not decode natively (13 LZ+Huffman,
  15 Arsenic, StuffIt X) are named in an ST_EUNSUPPORTED error and never
  mis-decoded; `st_unpack_file(..., allow_external=1)` and
  `os999-unpack -x` hand them to `unar -k visible` (no shell, private
  temporary folder) and import its AppleDouble `.rsrc` output.
  `lsar -j` restores type, creator and Finder flags that `-k visible`
  drops for data-only files. Evidence (`test_storage_real`, unar 1.10.1
  on Linux): StuffIt 3.0 FSSETTER (method 13) 299/299 and 5.0 FSSETTER
  (methods 13/15) 233/233 shared files identical to our native decode of
  the 1.5 archive, forks and type/creator included; 3.0 and 5.0 MAXIMUM
  are reported unsupported natively.
- [x] SF08 Compact Pro: container, nested folders (descendant counts),
  both forks, CRC-32, RLE (0x81 escape) and LZH (8K ring, three
  nibble-packed tables, 0x1FFF0-unit blocks, 16-bit word lookahead).
  Coding rules were derived by matching every block of the corpus.
  Evidence: Aaru CORPUS DEFAULTS == STORE (31 entries, 5.3 MB, CRC-32
  verified); FSSETTER 321 entries, 71 resource forks, 319/320 identical
  to the StuffIt 1.5 copy (`test_storage_real`); synthetic LZH crossing a
  block boundary (`test_storage_archives`); fuzzed.
- [x] SF09 Disk Copy 4.2: decode with data and tag checksums; encode.
  Evidence: `test_storage_images`; fuzzed.
- [x] SF10 NDIF (Disk Copy 6) images: `bcem` table (blocks @68, backing
  offset @76, CRC @80, chunks @124), raw, zero and ADC (0x83) chunks,
  image checksum; KenCode (0x80) reported unsupported. Layout and type
  codes cross-checked against the BSD-licensed ndif2raw tool, which is
  used on real images (this caught swapped ADC/KenCode codes).
  Evidence: `test_storage_runtime` (raw/zero/ADC NDIF, checksum, and
  MacBinary > NDIF > HFS). No real NDIF image was available here.
- [x] SF11 UDIF (.dmg): koly trailer, XML `blkx` tables; raw, zero,
  ignore, ADC, zlib, bzip2 (when libbz2 is present) chunks; LZFSE and
  LZMA reported. Evidence: synthetic UDIF with zlib, ADC, raw chunks
  (`test_storage_images`); real StatBar 1.4.0 dmg (`test_storage_real`).
- [x] SF12 Volume location: bare HFS/HFS+, Apple partition map (512 and
  2048 strides), GPT, HFS wrappers around HFS+; APFS reported.
  Evidence: `test_storage_images` (APM, GPT); real dmg (GPT > HFS+).
- [x] SF13 HFS read: MDB, catalog and extents B-trees (leaf chains),
  folders, Finder info, dates, both forks, extents-overflow records,
  HFS wrappers. Evidence: real Crystal Quest floppy matches machfs (12
  files, both forks); a fork split across three catalog extents plus an
  overflow record is reassembled, and machfs reads the same image
  (`test_storage_images`); HFS wrapper around the real StatBar HFS+
  volume (`test_storage_real`).
- [x] SF14 HFS+ / HFSX read: UTF-16 names, eight-extent forks, overflow
  records, private hard-link folders and symlinks not materialized.
  Evidence: real StatBar dmg (`test_storage_real`).
- [x] SF15 HFS write: fresh 400K-2G volumes with nested folders, index
  levels, both forks and Finder info. Evidence: `test_storage_images`
  round trip (120 files); the independent machfs reader lists all 121
  files of a written volume with correct forks (recorded in docs).
- [x] SF16 Guest volume containment: openat + O_NOFOLLOW per component;
  "..", symlinked folders and leaves, reserved names, control
  characters and empty paths are refused. Evidence: `test_storage_volume`.
- [x] SF17 Quotas on bytes (data + sidecars) and file count, checked
  before anything is written. Evidence: `test_storage_volume`.
- [x] SF18 Atomic saves: temporaries, fsync, journal, renames; opening a
  volume replays a committed save or rolls back an uncommitted one and
  deletes stray temporaries. Evidence: crash at each of 4 durable steps
  then reopen (`test_storage_volume`).
- [x] SF19 All-or-nothing installs with quota precheck, no silent
  overwrite, in-process rollback and crash recovery from the install
  manifest. Evidence: `test_storage_volume`, `test_storage_runtime`.
- [x] SF20 Runtime compatibility: `<name>.rsrc` AppleDouble sidecars are
  read by the desktop's `mac_resource_open_container`.
  Evidence: `test_storage_runtime` links `src/mac_resource.c`.
- [x] SF21 Content-based detection and recursive unwrapping
  (e.g. BinHex > MacBinary > UDIF > HFS) with depth limits.
  Evidence: `test_storage_images`.
- [x] SF22 `os999-unpack` command: list, install into a volume, quota,
  optional external fallback. Evidence: manual run on the real floppy.
- [x] SF23 Every parser survives deterministic mutation fuzzing under
  ASan/UBSan with a per-format watchdog. Evidence: `test_storage_fuzz`
  (12 formats incl. StuffIt, StuffIt 5, Compact Pro, UDIF, HFS and the
  lsar JSON reader; 300,000 mutants at 25,000 per format, 0 findings,
  6 Nov 2026). Fuzzing found and fixed one hang in the JSON reader.

## Host validation (separate from feature completion)

- [x] Linux x86_64 (cloud container, gcc 13, zlib + libbz2, unar 1.10.1).
- [x] Linux arm64 device VM on Ian's Mac (gcc 11, no libbz2): gate passes.
- [ ] macOS arm64 build and tests on Ian's Mac.
- [ ] Raspberry Pi hardware.

## Not in scope here

- StuffIt method 13 static tables and Arsenic need constants we cannot
  reproduce clean-room; the unar route covers them.
- Desktop integration (double-click installs through this library,
  checklist L06) is a follow-up on top of this module.
