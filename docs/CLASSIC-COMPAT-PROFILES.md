# Classic compatibility profiles and classification

Status: increment A (audit, baseline) and B (classification) of the classic-compat work, 11 Nov 2026. Recognition is
not compatibility: nothing here says an application works. `app_classify` (include/app_classify.h) reads bytes and
reports what a program appears to need. A pass needs a real run, recorded separately.

## Five things that must not be conflated

| Concern | Where it lives today | Independent? |
| --- | --- | --- |
| Machine RAM capacity | `guest_memory.h` (`--ram-mb`), `mac_cpu_create_mode` | yes, capacity only |
| Addressing behaviour | the CPU bus mask in `mac_cpu.c`, `rt->addr32` | partly (see audit) |
| Application partition | `mac_partition.c`, SIZE resource, `OS999_GUEST_HEAP_MB` | yes |
| CPU / executable format | `app_classify`, `mac_runtime.c` (68K only) | n/a |
| Toolbox services | `mac_*.c` trap services | independent of the above, but they read guest pointers |

## Profiles

| | A legacy 68K | B 32-bit-clean 68K | C classic PowerPC | D unsupported |
| --- | --- | --- | --- | --- |
| Formats | CODE resources | CODE resources | PEF (`Joy!peff`, `pwpc`) with a main symbol; `cfrg` | everything else |
| Evidence | SIZE present, is32BitCompatible clear (high), or no SIZE (low) | SIZE is32BitCompatible set (medium: a declaration) | PEF loader section, imports | see reason |
| Addressing | 24-bit semantics wanted | works in 24 or 32-bit; prefers 32 for memory | 32-bit | n/a |
| Runs in this build | yes (68K runtime) | yes | no: no interpreter or PEF loader | no |
| Missing | none | none | PowerPC interpreter, PEF/CFM loader; CarbonLib for Carbon imports | stated per case |

B tolerates both addressing modes, A tolerates only 24-bit. A session-wide 24-bit machine therefore satisfies A and B;
only C and large-partition B need 32-bit.

Not supported, and recognised only to say why: Mac OS X Mach-O (any architecture, no x86/Rosetta/Cocoa), CFM-68K,
PowerPC shared libraries, programs with CODE but no usable CODE 0. Mixed 68K/PowerPC programs take a path the host can
run: with no PowerPC available the 68K path is chosen and PowerPC is reported as `alternate`; with PowerPC available the
PowerPC path is chosen only if every import is a known classic library (unlisted imports or Carbon keep the 68K path).

## Output

`app_classify()` fills `AppClassification`: `format`, `arch` bits, `profile`, `alternate`, `confidence`, `runnable`,
`is_application`, the evidence (CODE count, CODE 0 layout, SIZE flags and sizes, cfrg members, PEF imports), a 64-bit
`identity` (order-independent hash of CODE/cfrg resources and a PEF data fork; independent of name, path and dates;
change detection only, not authentication), and three texts: `reason`, `uncertainty`, `missing`.
`app_classification_json()` renders one JSON object; `demonstrated` is always false. `build/os999-classify` prints it for
a pair of forks: `os999-classify --rsrc A.rsrc [--data A] [--type APPL] [--ppc]`.

Rules: Finder file type, when known, must be APPL; names and extensions are never read; a missing SIZE resource is not
evidence of 32-bit cleanliness; damaged structure gives `malformed` and profile D, never a guess. Tests:
`tests/test_app_classify.c` (every prefix of valid inputs plus 4000 seeded corruptions, also run under ASan/UBSan).

## Audit of the 16 MB / 24-bit fallback (verified against the code and a headless probe)

* The CPU bus mask is real. `mac_cpu.c` masks every Musashi bus read/write, instruction fetch and PC to 24 bits when
  the machine is 24-bit, and 24-bit-dirty pointers work in guest code. It is not just "less RAM".
* It stops at the CPU. About 500 `mac_cpu_read/write` call sites and 64 files that use `mac_cpu_ram()` interpret guest
  pointers without masking. Probe, default machine: `GetHandleSize` on a handle with bit 31 set returns -109;
  `BlockMove` with a flagged source halts with "range exceeds guest RAM". On a real 24-bit Mac the Memory Manager
  ignores the flag byte, so a program that passes a flagged pointer to a trap diverges here. Only `StripAddress` and
  PC/trap-return masking are 24-bit aware.
* Master pointers carry no flag bits: lock/purge/resource state is host-side (`MacHandle.state`, heap bridge). A program
  that tests master-pointer bits directly instead of using `HGetState` sees zeros.
* RAM is 16 MB for 24-bit, so the 24-bit machine cannot give a program its SIZE preferred partition above 12 MB
  (extended partitions need 32-bit).
* One runtime process per launched app (`ospi-runtime` is forked per app, with its own CPU), so addressing modes can
  coexist across apps. Correction to the first audit: RAM size and addressing reach the child from the saved Memory-panel
  keys through `native_settings_apply` (inside the forked child), not from `cdev_memory.c`; that file only `setenv`s the
  partition size and heap guard in the desktop process. So the machine is a session default, and a per-app choice is
  an argv override at the fork seam (`--ram-mb/--addressing` beat the environment), never a flip of live state.

## Baseline (before memory/addressing changes), headless, 20 s of guest time, this branch (guest-RAM tip)

Configs: default (16 MB, 24-bit) and `OS999_GUEST_RAM_MB=256 OS999_GUEST_ADDRESSING=32`. Identical results in both.
"startup" means the probe ran to its time budget without a halt; no frames were captured, so this proves neither
graphics, input, sound nor gameplay. Specimens S1-S10 are Ian's own copies, run from a scratch directory, not committed and deliberately not named here.

| Specimen | Classified | Headless result (both configs) |
| --- | --- | --- |
| S1 adventure game | B, medium (SIZE 1.2 MB) | startup to idle loop, no halt; gameplay unverified |
| S2 adventure game | B, medium (SIZE 2.0 MB) | same |
| S3 puzzle game | B, medium (SIZE 1.2 MB) | same |
| S4 3D modeller (68K) | B, medium (SIZE 2.5 MB) | same |
| S5 game | B, medium (SIZE 0.75 MB) | halts: unhandled A-line trap A800 (D0 000c0008) |
| S6 archive utility (68K) | B, medium (SIZE 0.5 MB) | halts: AliasDispatch selector unsupported |
| S7 archive utility (fat) | B, alternate C (cfrg, PEF, 5 imports) | 68K part halts: AliasDispatch selector unsupported |
| S8 3D renderer (68K) | B, medium (SIZE 4.4 MB preferred) | halts: unhandled A-line trap A81C at 33 instructions |
| S9 web browser (Carbon PEF) | C, high; needs CarbonLib | not runnable here (no PowerPC); runtime refuses: no CODE 0 |
| S10 3D modeller (PEF) | C, medium (3 unlisted imports) | not runnable here; runtime refuses: no CODE 0 |

Every 68K specimen declares is32BitCompatible, so the specimen set contains no profile A program: the 24-bit
fallback is unexercised by real software. S9 asks for 160 MB (minimum 96 MB) and needs the 32-bit extended
partition if it is ever run.

## Launch policy (increment C)

`native_open` classifies the real forks first (`native_policy.c`), asks `launch_policy_decide` (`launch_profile.c`) and
adds the profile's arguments to the `ospi-runtime` argv. The selected profile, source and reason go to the private
runtime log (the first line, before the runtime's own output).

| Profile | Extra argv | Why |
| --- | --- | --- |
| A, session default | none | the default machine is 24-bit |
| A, session set to 32-bit | `--addressing 24` | this launch only; the session setting is not changed |
| B, partition up to 12 MB | none (unchanged) | 32-bit-clean programs run on either machine |
| B, SIZE above 12 MB | `--addressing 32 --ram-mb N` | N = 16 + preferred MB + 16, at least 256, at most 1024; omitted if the session already provides it |
| C, when a PowerPC runtime exists | `--addressing 32 --ram-mb N` | refused today: no interpreter or PEF loader |
| D, or not runnable | refused | the alert carries the classifier's reason and what is missing |

Nothing falls back: a refused program is never handed to Systemless or to another profile, and a program that crashed
is never relaunched under a different profile (there is no retry path in the code). The default 16 MB/24-bit launch of
every program that worked is byte-identical to before.

Overrides live in `<volume>/.os999/launch-profiles` (an older `<volume>/.os999-launch-profiles` is read once, copied there and removed; text, atomic replace, symlinks refused, damaged lines dropped). An
entry is found by executable identity, so renaming or moving the application keeps it. A hash of the application's path
is stored beside it only to notice that the executable at that place changed: the entry is then deleted, the launch uses
the classifier, and the log line says why. There is no UI to create an override yet (`launch_override_set` is the API).

Launch entry points: (1) Finder open of an application item `finder_open.c` -> `native_open`: policy. (2) Finder open of a
file `finder_open.c` -> `classic_open` -> `native_open`: policy first; only when it reports "not an executable" (1) does
`classic_open` continue to the sibling-archive / Systemless path, which is unchanged. (3) Command-line open `main.c` ->
`classic_open`: same as 2. (4) `session_open_sit` and archive launches: archive code, not behind policy. (5) Developer tools
(`ospi-runtime`, `ospi-runtime-probe`, `ospi-bridge-probe`): direct, no policy.

## 24-bit pointer masking (increment D, first slice)

`mac_memory_service` strips the high byte of the pointer registers of NewPtr-family consumers on a 24-bit machine:
GetPtrSize, DisposePtr, GetHandleSize, SetHandleSize, DisposeHandle, HLock/HUnlock/HPurge/HNoPurge, HGetState/HSetState,
RecoverHandle, MoveHHi, and A0/A1 of BlockMove/BlockMoveData. Code addresses (SetGrowZone) and StripAddress are not
touched; 32-bit machines strip nothing. Not done: the other ~500 pointer-reading call sites in Toolbox services, master-
pointer flag bytes, and window/control/dialog record pointers.

## Gaps and next steps

1. Toolbox services other than the Memory Manager still read flagged pointers unmasked.
2. No UI for per-app overrides or for viewing the selected profile; the log line is the only surface.
3. A profile A specimen (System 6-era 68K program) is needed before the legacy path can be called exercised.
4. PowerPC availability is a constant in `native_policy.c` (`ppc_available = 0`).
