# Runtime loop record — 7 November 2026 (memory, Toolbox gaps, real-app probing)

Dated evidence, not acceptance. Nothing here is a gameplay, windowed or device pass; every
result is headless on one host. Read with [the checklist](DEVELOPMENT-CHECKLIST.md) items R03,
R04 and G02. Items stay unchecked.

## Scope and revision

- Branch `feature/memory-integration`, code head `6923cd5`, cut from `main` `a14d464`. `main` is
  untouched and nothing is pushed.
- Host: Apple silicon (arm64), macOS 26.6.2 (Darwin 25.6.0), Apple clang 21.0.0.
- Builds and tests ran locally with `BUILDFLEET_LOCAL=1` per the project override.
- Guest apps were downloaded from Macintosh Garden (`old.mac.gdn`) or taken from the existing
  volume, copied into a scratch directory outside Git, and run from copies. Glider 4.10,
  Glider Edit and Room Editor came from the Glider 4.10 floppy image (`Glider_4_10.dsk`, HFS
  extracted with `hfsutils`, forks via `unar -k visible`); Glider PRO 1.1.2 from its `.toast`.
  Monkey Island, Monkey Island 2 and Castle of Dr. Brain were already installed.

## What changed, in commit order

| Commit | Change | Main tests |
| --- | --- | --- |
| `d1ff826` | Protected heap `os999_heap`: per-guest region committed on demand, bookkeeping outside guest memory, generation-tagged handles, incremental compaction, zero on allocate and free, fault tears down one space | `test_os999_heap` (plain and ASan/UBSan) |
| `f1aa8b9` | Guest NewPtr/NewHandle/size/lock/state/FreeMem/CompactMem served from the heap over guest RAM `[0x400000,0x700000)`; low bump area keeps `[0x100000,0x400000)` | `test_mac_heap_bridge` (plain and ASan) |
| `82d824f` | Heap handles and pointers shadowed in `rt->handles[]` so menu/resource/pixel code can see them; resource bodies moved into runtime-owned heap blocks; MoveHHi on them accepted; MoreMasters and SetGrowZone; `OSPI_HEAP_STATS`; ASan probe target | bridge tests |
| `5e3f173` | SetResInfo (rename at the same ID) | `test_mac_resource_services` |
| `f87ac3d` | PaletteDispatch `$AAA2`: HasDepth `$0A14`, SetDepth `$0A13` (identified from the public MPW interface text) | `test_mac_palette_manager` |
| `6f99cdd` | Resource edit layer, WriteResource, persistence through the save overlay as a hidden file bound to a hash of the fork; Mac II / System 7.0 profile; Gestalt `qdrw` | `test_mac_resource_edits`, `test_mac_system` |
| `260c84c` | Colour cursors GetCCursor/SetCCursor/AllocCursor/DisposeCCursor | `test_mac_ccursor` |
| `25bdfdc` | Pack7 NumToString/StringToNum | `test_mac_numconv` |
| `9be4086` | Real 8-bit gray mode for the main device; HasDepth/SetDepth report it | palette manager tests |
| `6b7b6db` | FSSpec calls via `$AA52` (7 selectors); Gestalt `fs  ` bit 1 only | `test_mac_fsspec`, `test_mac_fsspec_write` |
| `837c66f` | ClearMenuBar, GetNewMBar, SetMenuBar, data-level submenus; heap-safe list resize | `test_mac_menubar` |
| `3fbcd10` | `$A830` identified as the Help Manager dispatch; reports "switched off" with the documented codes | `test_mac_help` |
| `507c06b` | Window colour tables (`wctb`) for GetNewCWindow | `test_mac_window_colors` |
| `00d65f1` | SetOrigin `$A878` (translates bounds or pmBounds, portRect and visRgn, not the clip); fixes LocalToGlobal/GlobalToLocal, which read the wrong port offsets with the wrong signs | `test_mac_setorigin`, `test_mac_quickdraw` |
| `6923cd5` | GetNewPalette `$AA92` from a `pltt` resource; NewPalette's allocator shared | `test_mac_getnewpalette` |

## Compatibility profile decision

Ian authorised, on 7 November 2026, presenting the guest as a Mac II-class machine running
System 7.0 (SysEnvirons `envMacII`, Gestalt `mach` = `gestaltMacII`, version `0x0700`). It is
an identity for programs that gate on it, not a claim that every System 7 service exists:
unsupported traps and Gestalt selectors still halt or return the undefined-selector error.
Consequences seen: apps now ask for the Help Manager, so it answers honestly that balloons are
off and no Help menu exists; Gestalt `help`, `stdf`, `evnt` remain unanswered.

## Evidence at `507c06b`

- `BUILDFLEET_LOCAL=1 make test-memory` exit 0; `BUILDFLEET_LOCAL=1 make test` exit 0 with 127
  test binaries and no failure line. No source or test file reaches 500 lines.
- Every new test file was mutation-checked (deliberate breakage, rebuild with a forced object
  refresh, confirm a test fails). Weak cases found that way were fixed: the pixel-depth and
  trailing-bytes cases, a source-span off-by-one in Pack7, and one redundant read-only check
  in FSSpec (equivalent mutant, documented by its author).
- Regression: Monkey Island, Monkey Island 2 and Castle of Dr. Brain give byte-identical
  `build/ospi-runtime-probe --seconds 8` output between consecutive verified revisions, and
  zero AddressSanitizer or UBSan reports in `build/ospi-runtime-probe-asan`. Against the first
  baseline Monkey Island differs by 2 instructions in 21.8 million after the System 7.0
  profile; the other two are identical.
- A real run of Glider 4.10 with `--saves` wrote a 94-byte hidden `.resedits-<fork hash>`
  file, a second run reported loading one edit, and the original `.rsrc` kept its SHA-256.

## Real applications (headless, `build/ospi-runtime --indexed --steps 20000000`)

None of these has reached its main loop. "Traps" is the count before the halt.

| App | Traps | Stops at | What it is |
| --- | --- | --- | --- |
| Glider 4.10 | 508 | `SetPalette` | an existing deliberate halt: SetPalette on a visible window needs Palette Manager activation (which colours the screen shows) |
| Room Editor 1.06 | 148 | GetNewCWindow | custom window definition (WDEF 10, "Tools" palette) and `noGrowDocProc` |
| Glider PRO 1.1.2 | 88 | `A816` | Apple Event Manager; the app installs four handlers and sets interaction |
| Glider Edit 1.1.1 | 36 | Gestalt `stdf` | Standard File dialogs; `GetNewMBar` was exercised only with `stdf` faked locally, not committed |

The window-colour change cleared Glider 4.10's `wctb 128` (content, frame and text black;
parts 3 and 4 are documented as reserved). A pixel dump of the screen at the halt is entirely
palette index 255, including outside the 512x342 window, so the game itself paints the whole
screen black and the real run cannot show the colour work. The unit tests carry that evidence.

## Corrections and findings during the loop

- **LocalToGlobal / GlobalToLocal were wrong** (found while doing SetOrigin). They read `port+12/+14`
  (bounds bottom and right; the file's own `bitmap()` helper and the raster code put bounds at
  `port+8`) and applied the opposite signs. Their test had copied the same offsets. Both are
  corrected, colour ports now find their bounds through the PixMap, and the test says why. The
  three games' output did not change, so none of them depended on the old behaviour.
- **SetOrigin and the clip region:** two public sources agree it does not move the clipRgn.
- **`pltt` layout** verified against all three real palettes in the test apps (exactly
  `16 + 16*count` bytes); their header and private slots hold junk, which is deliberately not copied.
- **A mutation harness blind spot:** a mutant that crashes prints no failure line. Mutation runs now
  also look at the exit status; the unbounded-palette mutant was caught by a stack-smash abort.

## Limitations and things not verified

- Everything is headless. No windowed or device run, no screenshots, no audio listening.
- The protected heap's compaction never ran in any real app (none frees a movable block ahead
  of another); it is covered by unit tests only. Guest loads and stores still reach RAM
  directly, so fault containment applies to the translate calls, not to every guest access.
- Handles beyond the 512-entry table work for the memory traps but are invisible to other
  managers. SetGrowZone stores the procedure but never calls it. The bump area for runtime
  records is 3 MB (0.34 to 1.38 MB used by the installed games, measured before resource
  bodies moved into the heap, so current use is lower).
- Colour cursors install the 1-bit image only; colour pixels are validated and kept, not drawn.
  The `crsr` layout was checked against 25 real cursors in two apps, not against a fetched spec.
- Pack7 parsing rules for malformed input are an implementation choice, not verified on a ROM.
- FSSpec: FSpOpenRF, FSpDirCreate, locks, CatMove and ExchangeFiles are not implemented;
  Glider Edit never reached an FSp call, so they are tested only with hand-built frames.
- SetResInfo cannot change an ID. Resource edits persist only for the application fork.
- GetNewCWindow does not yet create and attach the palette that the Window Manager is documented to
  build from a `pltt` with the window's ID. SetPalette on a visible window halts (activation is not
  implemented), so no palette ever changes the screen's colours. Complex (non-rectangular) regions
  are unsupported, so SetOrigin halts on one.
- Subsystems that are entirely absent: custom WDEFs, SetOrigin, Apple Events, Standard File,
  pop-up and hierarchical menu drawing, balloon help, GWorlds backed by handles.

## Next reproducible steps

1. Palette Manager activation (SetPalette/ActivatePalette on a visible window) for Glider 4.10: a design
   for how palettes map onto the device's colour table, then a windowed run and screenshot once it
   reaches its loop.
2. Apple Event Manager, record-only subset first (install, remove, get handler, interaction
   level), with a test that every selector's word count matches the public declaration.
3. Room Editor: `noGrowDocProc`, then the custom WDEF mechanism (guest callbacks from the
   Window Manager) as its own design.
4. Standard File for Glider Edit: a UI contract, design before code.
5. Exercise compaction with a real fragmenting program, or a purpose-built guest.
