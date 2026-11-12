# PowerPC runtime

Status on 10 November 2026, branch `feature/ppc-runtime`. No real PowerPC
application runs to a window. Two slices exist. The first is a tested vertical
path: a synthetic big-endian PowerPC PEF fragment is parsed, placed in guest
RAM, relocated, bound to host C functions and run in our own interpreter. The
second puts a PowerPC application on the same machine as a 68K application
(one `MacRuntime`, one guest RAM, two CPUs) and serves its InterfaceLib imports
from the existing trap services through a table-driven bridge. The one real
application tried so far gets through 443 Toolbox calls of its startup and
stops at its first window; the trace is at the end of this file.

Everything here is clean-room: written from public knowledge of the 32-bit
PowerPC user-mode instruction set, the publicly documented PEF container
layout and the public classic Mac OS programming interface (function names,
parameter lists, trap numbers, low-memory global addresses). No emulator
source was read or adapted, nothing is vendored, and no Apple code, ROM,
binary, header or documentation text is in the tree. Every test fixture is
assembled by hand inside the test.

## What exists and who owns it

| Piece | Files | What it does |
| --- | --- | --- |
| Interpreter core | `include/ppc_cpu.h`, `src/ppc_int.h`, `src/ppc_cpu.c` | Register state, bounds-checked big-endian guest memory, fetch/dispatch loop, halt diagnostics, `ppc_cpu_call` |
| Integer and logical | `src/ppc_exec_int.c` | Immediate and opcode-31 arithmetic with carry/overflow, rotates, logical ops, shifts, compares, CR and SPR moves |
| Branches | `src/ppc_exec_branch.c` | `b`, `bc`, `bclr`, `bcctr` with LK/AA and CTR, CR logical ops, `sc` |
| Loads and stores | `src/ppc_exec_mem.c` | D-form and indexed integer loads/stores, update forms, byte-reversed forms, `lmw`/`stmw`, `dcbz` |
| Floating point | `src/ppc_exec_fp.c` | FP loads/stores, single and double arithmetic, moves, compares, integer conversion, FPSCR subset |
| PEF parser | `include/mac_pef.h`, `src/mac_pef.c` | Container and section headers, loader section tables, exports, `cfrg` members |
| Pattern data | `src/mac_pef_unpack.c` | Section unpacking, pattern-initialized data expansion |
| Fragment loader | `include/mac_cfm.h`, `src/mac_cfm.c` | Section placement, import binding, host call gate, entry vectors, exports |
| Relocations | `src/mac_cfm_reloc.c` | The PEF relocation instruction interpreter |
| Host imports | `src/ppc_host.c` | Imports that need no machine (`strlen`, `memcpy`, `memset`, `abs`, `sqrt`) |
| Probe | `include/mac_ppc_probe.h`, `src/mac_ppc_probe.c` | `cfrg` lookup, data-fork read, `--inspect` report |
| Machine start path | `src/mac_runtime.c` (`mac_runtime_init_machine`) | The 68K runtime's machine without `CODE` 0 |
| PowerPC start path and scheduler | `include/mac_ppc.h`, `src/mac_ppc_runtime.c` | Loads the fragment into a `MacRuntime`, builds the import table, runs the two CPUs in turn |
| Import bridge | `src/mac_ppc_bridge.c` | Marshals PowerPC arguments into the 68K trap convention and back |
| Signature tables | `src/mac_ppc_sigs_memory.c`, `_quickdraw.c`, `_toolbox.c`, `_managers.c` | 492 rows: symbol, trap word, parameter description |
| Mixed mode | `src/mac_ppc_mixed.c` | Routine descriptors, `$AAFE`, `CallUniversalProc` |
| Native imports | `src/mac_ppc_native.c`, `src/mac_ppc_files.c`, `src/mac_ppc_math.c` | Calls with no trap: low-memory accessors, string and A5 helpers, File Manager glue, MathLib |
| Wiring | `src/runtime_main.c` | `--inspect` report; launches a PowerPC-only application through `mac_ppc_runtime_open` |
| Build | `config/ppc-tests.mk` | Source lists and test recipes; included by the `Makefile` |

The interpreter and loader do not depend on Musashi, SDL or `MacRuntime`.
Guest memory is a flat big-endian buffer borrowed from the caller and the
loader allocates through a callback, so both sit on the 68K runtime's guest
RAM without a copy. Everything from "Machine start path" down does depend on
the whole 68K runtime and is linked into `ospi-runtime` and the runtime tests.

### Instructions implemented

- Arithmetic: `addi addis addic addic. subfic mulli add addc adde addze addme
  subf subfc subfe subfze subfme neg mullw mulhw mulhwu divw divwu`, with the
  `o` (XER OV/SO) and `.` (CR0) forms.
- Logical and shifts: `andi. andis. ori oris xori xoris and andc or orc xor
  nand nor eqv extsb extsh cntlzw slw srw sraw srawi`, with record forms.
- Rotates: `rlwinm rlwimi rlwnm`, including wrapped masks.
- Compares and CR: `cmp cmpi cmpl cmpli mcrf crand cror crxor crnand crnor
  creqv crandc crorc mfcr mtcrf mcrxr`.
- SPR: `mfspr`/`mtspr` for XER, LR and CTR only.
- Branches: `b bc bclr bcctr` with AA and LK, CTR decrement and test.
- Loads/stores: `lbz lhz lha lwz stb sth stw` in plain, update, indexed and
  indexed-update forms; `lhbrx lwbrx sthbrx stwbrx`; `lmw stmw`; `dcbz`.
  `dcbt dcbtst dcbf dcbst icbi sync isync` are accepted as no-ops.
- Floating point: `lfs lfd stfs stfd` in plain, update, indexed and
  indexed-update forms; `stfiwx`; `fadd fsub fmul fdiv fmadd fmsub fnmadd
  fnmsub` in double and single; `fmr fneg fabs fnabs frsp fcmpu fcmpo fctiw
  fctiwz mffs mtfsf mtfsfi mtfsb0 mtfsb1`.

Any other encoding halts with the PC, the instruction word and its primary
and extended opcode. A guest access outside RAM halts with the address and
PC. PC stays on the faulting instruction.

### PEF relocation instructions implemented

By section D with skip; runs by section C, by section D, 12-byte and 8-byte
transition vectors, 8-byte vtable entries and import runs; small by import,
set section C, set section D and by section; increment position; small
repeat; set position; large by import; large repeat; large by section, set
section C and set section D. Unknown instructions, positions or indexes out
of range, nested repeats and truncated two-word forms reject the fragment.

### Host call gate

Each imported symbol gets a 16-byte record in guest RAM: an 8-byte
transition vector whose code address points at the record's own `sc; blr`.
Compiler glue loads the vector and jumps; the interpreter hands `sc` to the
loader, which maps the address back to the import and calls the bound C
function (`MacCfmHostFn`) with integer arguments in r3..r10, floating point
in f1.. and results in r3 or f1; the `blr` returns to the guest. An import
nobody implements is still bound to a gate and halts when called, naming the
library and symbol, the way an unsupported 68K trap halts. An `sc` anywhere
else halts. One host function can serve many symbols: each table row carries
a `data` pointer and the fragment records which import is being called.

Weak imports are the exception. An import whose symbol has the weak bit, or
whose library has the weak option, and that nobody implements resolves to
address 0 and is not counted as missing. That is what the system's loader
does when a weak library is not installed, so the fragment's own "is it
there" test sees it as absent. QuickDraw 3D is not faked.

## One machine, two CPUs

`mac_ppc_runtime_init` calls `mac_runtime_init_machine`, which is
`mac_runtime_init` without the `CODE` 0 steps: the same 16 MB of guest RAM,
partition and heap split, low-memory globals, System volume, trap table and
the application's resource fork opened as the application file. The A5 world
has 32 KB below A5 (room for the QuickDraw globals pointer) and no jump table.
`rt->ppc` then holds the PowerPC CPU, whose memory is `mac_cpu_ram(rt->cpu)`,
and the loaded fragment. `mac_runtime_init` itself is unchanged in behaviour;
the two paths share `machine_begin` and `machine_finish`.

Where things live for a PowerPC application with the default 8 MB partition:

| Range | Use |
| --- | --- |
| `0x100000`..heap split | Bump heap (runtime records, handles' master pointers) |
| heap split..partition top | Protected heap: `NewPtr`, `NewHandle`, resources |
| below `0x7F8000` | 68K stack, used only while a trap service runs |
| `0x800000` | A5 |
| `0x810000`..`0xE00000` | PowerPC arena: 256 KB stack, fragment sections, call gates |
| `0xE00000` | Screen |

The arena is 5.9 MB, so a fragment larger than that does not load, and with
the 12 MB partition setting it is only 1.9 MB. The application heap is the
same size a 68K application would get; a PowerPC application that expects
more than about 3 MB of heap will run out. Both limits come from the 16 MB,
24-bit machine and were not changed here.

`mac_runtime_run` and `mac_runtime_run_cycles` run the Time Manager and VBL
polls as before and then call `mac_ppc_run` instead of the 68K CPU when
`rt->ppc` is set, so the headless loop, the window loop and the desktop bridge
need no PowerPC knowledge. A halt on either CPU halts both, and the hosts keep
reading `mac_cpu_halted` and `mac_cpu_error`.

## The import bridge

An InterfaceLib import is bound to one of two things.

**A signature row** (492 of them). `MacPpcSig` is a symbol name, an A-line
trap word and a parameter string. All rows share one host function,
`mac_ppc_bridge_call`. It copies r3..r10 and six words of the caller's
parameter area, lays them out the way the trap expects, writes the trap word
into a scratch slot in guest RAM, points the 68K PC just after it and calls
`mac_runtime_trap`, the function an A-line instruction reaches. Patched traps,
the dispatch chain and every service therefore behave exactly as they do for a
68K caller. The result goes back to r3.

Signature grammar:

- Pascal stack-based Toolbox trap: `"<result>:<arguments>[/<selector>]"`.
  Result is `-` (none), `b` (Boolean: a byte in the high half of a word slot),
  `w` (16 bits, sign-extended into r3) or `l` (32 bits). Arguments are one
  character each in declaration order: `b`, `w` or `l`. A pointer, a handle
  and a 4-byte structure passed by value (a `Point`, which PowerPC passes in
  one register) are all `l`. The selector is `/w<hex>` (a 16-bit selector
  pushed after the arguments), `/l<hex>` (32-bit) or `/d<hex>` (loaded into
  D0), for the package and dispatcher traps.
  `{"PtInRect", 0xa8ad, "b:ll"}`, `{"NewGWorld", 0xab1d, "w:lwllll/d00160000"}`.
- Register-based OS trap: `"@<in>[,<in>...]><out>[/w<hex>]"`. Each input token
  takes the next PowerPC parameter: a register name (`D0 D1 D2 A0 A1`)
  receives the value; `*<reg>` is a pointer parameter that receives the
  register after the call (`*<reg>w` for 16 bits); `&<reg>` loads the register
  from the pointed-to long before and stores it back after; `-` skips a
  parameter; `<reg>=<hex>` sets a constant and uses no parameter. Output is
  empty, `D0`, `D0w`, `D0b` or `A0`. `{"NewPtr", 0xa11e, "@D0>A0"}`,
  `{"Gestalt", 0xa1ad, "@D0,*A0>D0w"}`,
  `{"PBGetCatInfoSync", 0xa260, "@A0,D0=9>D0w"}`.

The Pascal frames are checked on the way out. The service pops what it
believes its arguments are; if the 68K stack pointer is not where the row
says it should be, the bridge halts with `Stack imbalance of N bytes after
PowerPC import X`. A row that disagrees with a service cannot quietly corrupt
the stack. It does not catch a wrong trap number whose service happens to
take the same number of bytes.

**A native function** (61 for InterfaceLib, 23 for MathLib), where the classic
call has no trap: low-memory accessors (`LMGet...`, `LMSet...`, `MemError`,
`GetMBarHeight`, `GetGrayRgn`, read and written at the same low-memory
addresses the 68K services use), `c2pstr`/`p2cstr`, `SetA5`/`SetCurrentA5`,
`NGetTrapAddress`, `FlushEvents`, `HLockHi`, `GetOSEvent`, `GetIndString`,
`GetDateTime`, `Debugger`/`DebugStr` (which halt with the message), the
routine-descriptor calls, the high-level File Manager calls (`FSRead`,
`FSWrite`, `FSClose`, `GetEOF`, `SetFPos`, `GetFPos`, `HOpen`, `HOpenDF`,
`HOpenRF`, `GetVol`, `SetVol`: each builds a parameter block on the 68K stack
and makes the PB trap through the bridge, as the original glue did) and the
MathLib functions the host `libm` computes.

A row is a description of a call, not a promise that the service behind it
exists. If no service handles the trap the bridge halts with `Toolbox trap
$XXXX behind PowerPC import Name is not implemented.` An import with neither a
row nor a native function halts with `Unsupported PowerPC import Lib::Name`.
Nothing returns invented success.

Of the real application's 893 imports, 330 are weak and resolve to null,
473 are bound to a row or a native function and 90 have neither
(InterfaceLib 64, mostly printing, the Edition Manager, more Apple Event
calls and File Manager glue; QuickTimeLib 17; ObjectSupportLib 6; MathLib 3).
`ospi-runtime --inspect` lists each one as `binding=missing`.
Only 38 distinct imports have been exercised by that application; the other
rows are unexercised and each may hide a wrong trap number or layout.

## Mixed mode

What exists:

- **Routine descriptors.** `NewRoutineDescriptor` allocates the 32-byte record
  (`$AAFE`, version 7, procInfo, instruction set, procedure pointer) and
  `DisposeRoutineDescriptor` frees it. Descriptors a compiler placed in the
  application's own data work the same way. Only the first routine record is
  read.
- **68K side to PowerPC.** When code running on the 68K side jumps to a
  descriptor it executes `$AAFE`, which `mac_runtime_trap` hands to
  `mac_ppc_mixed_trap`. The parameters are taken from the 68K stack or
  registers according to the procInfo (Pascal stack, C stack and register
  conventions; up to eight parameters), the PowerPC routine runs through its
  transition vector, and the result goes back where the convention says. This
  is how a trap service's `mac_guest_call` reaches a PowerPC callback, with no
  change to the services.
- **PowerPC to PowerPC.** `CallUniversalProc` on a PowerPC descriptor is a
  tail call (up to six parameters).
- **PowerPC to 68K code.** `CallUniversalProc` on a 68K routine, or on a
  descriptor for one, builds the frame the procInfo describes and runs the 68K
  CPU until the routine returns.
- **Blocking traps.** A service that asks to be run again later leaves the
  PowerPC caller parked; the host gets its time slice and the trap is retried
  on the next one. `Delay` is the tested case. Modal dialogs and
  Standard File block the same way in the services but have not been run from
  PowerPC code. `PopUpMenuSelect` with a custom menu definition has been (see
  below).
- **Menu definition procedures.** A menu whose record carries a menuProc
  handle (`GetMenu` fills it from the MENU resource's MDEF id) runs that
  resource's 68K code from the Menu Manager services (`src/mac_mdef.c`): the
  size message in `GetMenu`, `GetNewMBar`, `CalcMenuSize` and before every
  `MenuSelect` and `PopUpMenuSelect`; pop-up, draw and one choose message per
  time slice while the button is down. Each message is a `mac_guest_call`
  whose completion carries on, so a PowerPC caller sees an ordinary blocking
  trap. `DrawMenuBar` and `MenuKey` need no message (titles and command keys
  are read from the record). Messages 4, 5 and 7 are never sent. The
  definition runs in a screen-sized colour port once colour QuickDraw is in
  use, else in the plain Window Manager port. Tested headless with a
  hand-assembled MDEF from the 68K side (`build/test_mac_mdef`) and from a
  PowerPC fragment (`build/test_mac_ppc_mdef`).

How it is scheduled: `mac_ppc_run` keeps a stack of switch frames (16 deep).
Exactly one CPU runs at a time and neither runs inside the other's callback.
With a TRAP or CALL68K frame on top, the 68K CPU is stepped until it returns
to the frame's scratch slot; with no frame or a CALLBACK frame on top,
PowerPC code runs. One exception: when a poller owns the 68K CPU (VBL tasks,
Time Manager tasks, sound callbacks), a PowerPC routine reached through a
descriptor runs to completion inside the trap, with a 50 million instruction
budget, and may only make Toolbox calls that complete at once.

What does not exist: `$AAFE` descriptors with more than one routine record or
with selectors; procInfo conventions other than Pascal stack, C stack and
register (the D0/D1/stack-dispatched and THINK C forms halt); more than eight
parameters to a PowerPC callback or six through `CallUniversalProc`;
floating-point parameters or results across a switch; 68K code calling a
PowerPC routine by any route other than a descriptor; `CallOSTrapUniversalProc`;
the Code Fragment Manager calls (`GetSharedLibrary`, `FindSymbol`); accelerated
resources; a blocking Toolbox call from a PowerPC routine that a poller
called. The existing limit of one `mac_guest_call` at a time also applies: a
PowerPC callback that makes a Toolbox call which itself needs a guest callback
halts with the runtime's "nested guest calls" message.

## How it was verified

All headless, on arm64 macOS, built locally (`BUILDFLEET_LOCAL=1`).

| Test | Checks | Covers |
| --- | --- | --- |
| `build/test_ppc_cpu` | 118 | Carry, overflow, record forms, multiply/divide edges, logical ops, shifts, rotate masks, CR fields and CR logic, SPR moves, big-endian loads/stores, update and byte-reversed forms, bounds and decode faults, branch conditions, CTR loops, `ppc_cpu_call`, the `sc` hook |
| `build/test_ppc_fp` | 60 | Bit-exact `lfd`/`stfd`, single conversion, arithmetic and single rounding, FPRF classes, divide by zero, compares with quiet and signalling NaNs, `fctiw`/`fctiwz` saturation and rounding modes, FPSCR moves |
| `build/test_mac_pef` | 97 | Valid container, every loader table, pattern opcodes, 35 single-field corruptions, a truncation sweep and a single-byte corruption sweep, `cfrg` members |
| `build/test_mac_cfm_reloc` | 53 | Each relocation form, default addresses, repeats, rejected streams |
| `build/test_mac_cfm` | 53 | The end-to-end fragment: pattern data, relocation, glue, two host calls, an FP host call, an unimplemented import, budget exhaustion, load failures |
| `build/test_mac_ppc_probe` | 36 | `cfrg` to data fork to PEF, the `--inspect` report, the probe summary |
| `build/test_mac_ppc_bridge` | 40 | A hand-built fragment on a real `MacRuntime`: `NewPtr`, `NewHandle`, `GetHandleSize`, `DisposePtr`, `MemError` (register traps and a low-memory accessor); `SetRect` (stack trap, pointer and values); `PtInRect` and `AddPt` (a `Point` by value); `Gestalt` (result through a pointer); `NewRoutineDescriptor` and `CallUniversalProc` PowerPC to PowerPC; a VBL task that is a PowerPC routine descriptor; PowerPC to 68K code to a PowerPC descriptor and back, with a Toolbox call inside the callback; `Delay` blocking across host slices; a weak import reading as null; `ExitToShell`; `PortSize` and `MovePortTo`; a stack-imbalance refusal, an unimplemented-trap refusal, and the 68K start path still refusing a fork without `CODE` 0 |
| `build/test_mac_ppc_mdef` | 16 | A PowerPC fragment calls `InitMenus`, `GetMenu`, `CalcMenuSize`, `InsertMenu` and `PopUpMenuSelect` on menus that name a hand-assembled 68K MDEF: size, pop-up, draw and choose messages reach the 68K code from PowerPC, the pop-up result is the item the MDEF answered, PowerPC code carries on afterwards |
| `build/test_mac_mdef` | see the test | The same MDEF driven from the 68K side with real guest calls: sizes, `GetNewMBar`, `AppendMenu` on a custom menu, the full `MenuSelect` sequence (size, draw rectangle, choose with pointer and item, restore of the pixels), sliding between a custom and a standard menu, pop-up placement and screen fitting, and the refusals (missing MDEF, stale menuProc, negative size) |

The first six tests were built once with `-fsanitize=address,undefined` during
the first slice and passed; that was not repeated for the bridge slice. `tests/test_mac_pef_cfm.c`, the older contract test, is untouched
and still passes. Expected values in the tests were worked out by hand from
the instruction and format definitions, not captured from the implementation.

Not verified: anything against real hardware or a reference CPU. The only
real binary is the one application described at the end of this file. Nothing
here was run in a window.

## What `ospi-runtime` does now

- `--inspect` on a resource fork with a `cfrg` 0 naming a PowerPC fragment
  in the data fork prints the member, the PEF sections, entry points,
  imported libraries and every import with `binding=host` (a signature row or
  a native function), `binding=null` (an absent weak import) or
  `binding=missing`, then one summary line. Nothing is executed. The
  `OS999_PPC_TRIAL` scratch run of the first slice is gone; the launch below
  replaces it.
- Launching an application that has a PowerPC `cfrg` member and no `CODE` 0
  creates the machine, loads the fragment and runs it: `ospi-runtime --indexed
  --steps N App.rsrc` headless, or the same window and bridge paths a 68K
  application uses (those were not tried). At exit it prints the halt reason
  and a `powerpc pc=... last-import=...` line. `OS999_PPC_TRACE=1` (or
  `--trace`) prints every import call to stderr in order.
- Fat applications that also carry `CODE` 0 keep taking the 68K path.
- A fragment with an initialisation routine is refused at load.
- The data fork is found by stripping `.rsrc` or `/..namedfork/rsrc` from
  the resource fork path. AppleSingle/AppleDouble containers are not handled.
- The INIT rejections in `src/mac_init.c` and `src/ext_scan.c` are unchanged:
  PowerPC INIT code still cannot run, so their messages still hold.

## What does not work yet

- A real application reaching its first window. See the trace below.
- Most of the Toolbox for PowerPC callers in practice. The bridge reaches only
  what the 68K runtime implements, and that runtime was grown for specific 68K
  games: `GetNewWindow`/`NewWindow` (old-style ports) are not implemented,
  windows are limited to a few definition procedures, eight windows and a
  640x480 indexed display. Each such gap halts with the trap number and the
  import name.
- Libraries other than InterfaceLib and MathLib. QuickTimeLib and
  ObjectSupportLib imports are bound to gates that halt when called; a real
  system would refuse to launch an application whose non-weak library is not
  installed, and we do not model that refusal. StdCLib has only the five
  functions of the first slice. MathLib lacks `num2dec`, `dec2num` and
  `str2dec`.
- Data imports (for example a library's exported globals) have no host storage
  and are parked at an unmapped address so any access faults. The real
  application imports none.
- Mixed mode beyond the list in "Mixed mode" above.
- Multiple fragments. One fragment per machine; no import library loading, no
  binding between fragments, no version checks. Initialisation and termination
  entries are located but never called, and a fragment with an initialisation
  routine is refused.
- Memory. The fragment is placed above A5, outside the application heap, in at
  most 5.9 MB. The heap is what a 68K application of the same `SIZE` gets.
- Exceptions and traps. `tw`/`twi`, alignment, program and floating-point
  exceptions are not delivered; `sc` is only the host gate.
- AltiVec, 64-bit instructions, `lwarx`/`stwcx.`, string instructions
  (`lswi`, `lswx`, `stswi`, `stswx`), `mftb`, `fsel`, `fsqrt`, `fres`,
  `frsqrte`, privileged instructions and all other SPRs.
- Floating-point fidelity. Arithmetic always rounds to nearest (only `fctiw`
  honours the FPSCR rounding mode); only ZX, VXSNAN, VXVC, VXCVI, FX, VX and
  FPRF are maintained; single-precision results are computed in double and
  rounded once more, which can differ in the last bit for `fmadds` and for
  operands that are not themselves singles; `lfs` of a signalling NaN goes
  through a host conversion.
- Performance. A plain switch interpreter, not measured.
- Guest memory from a failed or unloaded fragment is not returned to the
  allocator.
- Floating-point arguments through the bridge. Signature rows carry integer
  and pointer parameters only; the MathLib natives read f1 and f2 directly.

## Points to check independently

These come from memory of the public formats and were not checked against a
real file or reference implementation:

- Large repeat (`101100`) is taken to store its repeat count as is, while
  small repeat stores count minus one; both are read as "run the preceding
  block this many more times".
- Sections C and D are assumed to start as sections 0 and 1.
- The `cfrg` member layout used here (usage at +22, locator at +23, offset
  at +24, length at +28, member size at +40, name at +42) differs from the
  placeholder layout in `tests/test_mac_pef_cfm.c`.
- Exported symbols are found by a linear scan using the name length from the
  key table. The export hash function and hash-slot layout are not
  implemented or checked.
- Pattern opcodes 3 and 4 are taken to read custom size then repeat count,
  and to emit the common part before each custom part and once more at the
  end.
- Library option bit `0x40` is printed as "weak" by `--inspect`.
- `divw`/`divwu` leave 0 in the target on divide by zero or overflow (the
  architecture leaves it undefined); `fctiw`/`fctiwz` set the upper word of
  the result to `0xfff80000`; `mtxer` keeps only SO, OV, CA and the 7-bit
  byte count.

### From the bridge slice

Written from memory of the public programming interface and not checked
against a reference. A wrong row shows up as a stack-imbalance halt only when
the byte counts differ.

- Every trap word in the four signature tables. The 38 imports the real
  application called are the only ones exercised against services, and the
  services check byte counts, not meaning.
- Selectors: QuickDraw offscreen (`$AB1D`: NewGWorld `00160000`, LockPixels
  `00040001`, UnlockPixels `00040002`, UpdateGWorld `00160003`, DisposeGWorld
  `00040004`, GetGWorld `00080005`, SetGWorld `00080006`, GetPixBaseAddr
  `0004000F`, GetGWorldPixMap `00040017`); TextEdit dispatch (`$A83D`, 0 to
  13); Apple Event Manager (`$A816`: `091F`, `021B`, `061C`, `011E`, `0E11`,
  `0812`, `0E15`, `0407`, `0204`, `0825`); high-level File Manager (`$AA52`, 1
  to 15); HFS dispatch (`$A260`: 1, 2, 5, 6, 7, 8, 9, A, 1A); Standard File
  (`$A9EA`, 1, 2, 5, 6); Script Manager (`$A8B5`: `84020008`, `8404000C`,
  `82060010`); `IUMagString` (`$A9ED`, `000A`); `FindFolder` and
  `ResolveAliasFile` (`$A823`, 0 and `000C`); `SetFrontProcess` and
  `GetCurrentProcess` (`$A88F`, `003B` and `0037`); `HMGetBalloons` (`$A830`,
  `0003`).
- Parameter lists with many arguments or Booleans: `NewWindow`/`NewCWindow`
  (`l:lllbwlbl`), `NewDialog` (`l:lllbwlbll`), `NewControl` (`l:lllbwwwwl`),
  `DragGrayRgn` (`l:llllwl`), `CalcMask` (`-:llwwww`), `StdText` (`-:wlll`),
  `TESetStyle`, `TEUseStyleScrap`, `TEContinuousStyle`, `SFGetFile`,
  `FSpCreate`, `FindFolder`.
- Register conventions: `MaxMem` (grow size back in A0), `PurgeSpace` (D0 and
  A0 out), `Delay` (ticks in A0, final ticks in D0), `SecondsToDate` and
  `DateToSeconds` as `$A9C6`/`$A9C7`, `PtrToHand`, `HandToHand`, `PtrToXHand`,
  `SetToolTrapAddress` as `$A647`, `NGetTrapAddress` as `$A346`/`$A746`,
  `GetOSEvent`/`OSEventAvail` returning true when D0 is 0, `FlushEvents`
  packing the stop mask in the high word, `DrvrInstall` as `$A03D`.
- A character parameter is passed as a 16-bit value (`w`), a Boolean as a byte
  in the high half of its word (`b`).
- Low-memory addresses in `src/mac_ppc_native.c`: MemErr `$220`, MBarHeight
  `$BAA`, SysFontFam `$BA6`, SysFontSize `$BA8`, HiliteMode `$938`,
  TEScrpHandle `$AB4`, TEScrpLength `$AB0`, LastSPExtra `$B4C`, ROM85 `$28E`,
  Time `$20C`, CurActivate `$A64`, CurDeactive `$A68`, SEvtEnb `$15C`,
  GZRootHnd as `$328` for `GZSaveHnd`, DoubleTime `$2F0`, CaretTime `$2F4`,
  GrayRgn `$9EE`, EventQueue `$14A`, CurApName `$910`, CurrentA5 `$904`. A
  global the 68K runtime never writes reads as zero, exactly as it does for a
  68K application.
- The routine descriptor layout and the procInfo bit fields in
  `src/mac_ppc_mixed.c`, including the register codes (D0-D3 = 0-3, A0-A3 =
  4-7, D4-D7 = 8-11, A4-A6 = 12-14) and the rule that C stack parameters are
  each one long.
- The PowerPC parameter area starting at 24(r1), and a 224-byte red zone below
  r1 that a callback's frame must skip.
- `__fpclassifyd` class numbers (0 signalling NaN, 1 quiet NaN, 2 infinite, 3
  zero, 4 normal, 5 subnormal); `roundtol` rounding halfway cases away from
  zero.
- Parameter-block offsets in `src/mac_ppc_files.c`. None of that file's
  functions has been called by anything but the compiler.

## Real-file evidence

The dated runs of the one real application tried so far (first run, bridge slice, second run, third run) are in
[POWERPC-RUNTIME-TRACES.md](POWERPC-RUNTIME-TRACES.md). Latest, 11 November 2026: custom menu definitions run as 68K code from PowerPC
callers (`MenuSelect`, `PopUpMenuSelect`, `GetMenu`, `CalcMenuSize`). 12 November 2026: offscreen graphics worlds (`src/mac_gworld.c`), `CopyBits`
between 1, 8, 16 and 32 bits per pixel, and a `GetItemCmd` fix that lets the application load its own submenus; the application reaches 2,554
import calls and stops at `InterfaceLib::PrOpen` (the Printing Manager). Its first window is not drawn.
