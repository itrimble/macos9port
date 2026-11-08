# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# OS999 Stylized

Clean-room desktop that looks like a classic Mac, for arm64 and x86_64.
It is a userspace program, not an OS kernel. Extracted CODE-resource apps
run in our own independent runtime (`ospi-runtime`). Older archive launches
retain a Systemless fallback; that fallback does not satisfy the development
contract. `AGENTS.md` carries the coding-style and PR conventions; read it too.

## Development contract (Ian, 4 November 2026)

Read and maintain `docs/DEVELOPMENT-CHECKLIST.md` at every work session.
Ian requires our own clean-room classic-app runtime, playable target games,
measured hardware acceleration, modern browsing, secure networking, and
isolated legacy protocol compatibility. Systemless launches do not satisfy it.
Required demonstrations include browsing Thurrott.com, YouTube playback with
verified hardware video decoding and GPU composition, and opening classic
QuickTime to play a video in our runtime. QuickTime is separately supplied guest
software, never implementation source or bundled Apple code/assets. Track these
as distinct acceptance gates in the checklist; none currently has a runtime pass.
Use the REA skill for artifact/behavioral investigation without deriving our
implementation from Apple code or proprietary assets. Keep downloaded guest
software outside Git. Run direct local builds/tests for this project; Ian
explicitly overrides the shared build-orchestrator requirement here
(`BUILDFLEET_LOCAL=1`, say so when you build).

The checklist supersedes older scope exclusions for planned networking, media,
and execution work. Older handoff observations remain historical evidence, not
current acceptance or prohibitions on the newly requested capabilities.

## Do not add

- Apple ROMs, System files, or a Toolbox built by decompiling Apple code
- Apple's Chicago, Charcoal, Geneva, Monaco, or the Apple logo
- SheepShaver, Basilisk II, Mini vMac, or any wrapper that needs a ROM
- Copied text from Inside Macintosh
- The Systemless source tree. It is GPL-3. Some legacy archive launches may
  call an installed `systemless` binary, but that fallback is not accepted as
  OS999 runtime compatibility. Do not vendor or link its code.
- Chicago Kare, ChiKareGo, or JohnDDuncanIII/macfonts. ChicagoFLF
  (Robin Casady, public domain, in `fonts/`) is the bundled face.

The on-screen product name is OS999 Stylized. The menu bar uses OS999's
original Granny Smith fruit mark. Keep Apple-owned logos, source code, and
system assets out of the project. Musashi (MIT-style) is the only vendored
third-party code and it supplies CPU instruction execution only; it is
downloaded hash-checked into ignored `build/deps/` by `scripts/prepare-musashi.sh`.

## Commands

Needs a C11 compiler, `pkg-config`, SDL2 and FreeType (`brew install sdl2 freetype`;
`libsdl2-dev libfreetype6-dev` on Debian). Without FreeType the build falls back
to the 8x8 bitmap font and `-DOSPI_FREETYPE` is not set.

```bash
BUILDFLEET_LOCAL=1 make test        # build + run every headless check (no window needed)
make                                # build/ospi (desktop) and build/ospi-runtime (guest helper)
make runtime                        # only build/ospi-runtime
make app                            # package + ad-hoc sign "build/OS999 Stylized.app" (macOS)
make run                            # make app && open it; on Linux runs ./build/ospi --volume ./volume
make app APP_BUNDLE="build/OS999 Test.app"   # parallel bundle; never rebuild a live bundle in place
make test-wallpapers                # converts assets/wallpapers with sips, then checks them
make clean                          # rm -rf build (also drops the Musashi download)
```

For a complete standalone macOS artifact, use `scripts/integration-gate.sh`
through the configured local watcher; `make app` is base packaging only. The
complete gate builds exact browser/engine sources, bundles dependencies and
checks relocated startup. It pins genuine SDL2 privately because SDL2-compat
2.32.74 crashes on synthetic keyboard text injection. Keep that compatibility
pin and its linkage checks until a replacement passes the same tests. See
`docs/OS999-MASTER-TRACKER.md` for current artifacts and acceptance boundaries.

Run a single test: every test is its own binary built from one `tests/test_*.c`.
Build the target and run it, e.g.

```bash
make build/test_mac_quickdraw && ./build/test_mac_quickdraw
./build/test_desktop_assets assets/icons/ospi-icons.icl8     # some tests take an asset path
./build/test_desktop_sound assets/sounds/classic9
```

Runtime probes against a guest application (`Name.rsrc` sidecar or real
resource fork, kept outside Git):

```bash
./build/ospi-runtime --inspect '/path/Game.rsrc'
./build/ospi-runtime --window --indexed --steps 100000000 '/path/Game.rsrc'
./build/ospi-runtime --window --indexed --interactive '/path/Game.rsrc'   # not with --steps
python3 scripts/runtime-checkpoint.py --indexed '/path/Game.rsrc'          # bounded headless evidence record
```

Headless Systemless probe (fallback path only, one process at a time; its PNG
lands at `$TMPDIR/systemless_headless_9999.png` and is overwritten by the next run):

```bash
systemless --headless --max-ticks 50 "/path/Archive.toast"
```

`make app` deletes and recreates the bundle: quit the running app first. The
live data volume is `~/Library/Application Support/OS Pi/Volume`; never commit
it, Garden downloads, `build/`, or third-party program screenshots.
CI (`.github/workflows/test.yml`) runs only `make test` on macOS with FreeType.

## Adding a test

The Makefile has no wildcard rule. Add a `build/test_<name>:` recipe (link
`$(CORE)` + `src/os999_startup.c` for desktop tests, or `$(RUNTIME_OBJ)
$(CPUOBJ)` with `$(CPUFLAGS)` for runtime tests), append it to a `test:`
prerequisite line, and add the `./build/test_<name>` invocation to the `test`
recipe. Tests use a local `CHECK(cond)` macro that counts failures and return
nonzero; they must run without SDL or a display. `tests/test_ospi.c` can
write PPM frames for inspection; keep new tests headless the same way.
The `$(PHASE_STANDALONE_TESTS)` group (quicktime, browser IPC, network policy,
PEF/CFM, media gateway, release gate) compiles each `tests/%.c` alone with no
`src/` objects: they pin contracts for work that is not implemented yet.

## Architecture

Two executables share one source tree and talk over shared memory:

- **`ospi`** (`src/main.c`): the desktop. One `Session` (`include/ospi.h`)
  holds windows, selection, menus, dialogs and the volume path. Everything
  is drawn into a 1024x768 `uint32_t` framebuffer by `session_paint`
  (`src/paint.c`, `src/draw.c`, `src/font.c`); SDL only blits that buffer.
  Input arrives through `session_down/up/move/wheel/key` (`src/input.c`) and
  menu commands through `session_command` with `CMD_*` ids. `desktop_host.c`
  owns SDL, sounds and the clipboard; `session_int.h` exposes session internals
  (`open_item`, `open_folder`, dialogs) to the split-out `src/session_*.c`,
  `desktop_*.c`, `folder_*.c` and `menu_*.c` files.
- **`ospi-runtime`** (`src/runtime_main.c`): the guest helper. `mac_cpu.c`
  wraps Musashi; `mac_runtime.c` loads `CODE` segments from a `MacResourceFork`
  (`mac_resource*.c`), builds A5 world, jump table and handles in 16 MB of
  guest RAM, and installs an A-line trap callback. `mac_services.c` is the
  dispatcher: each `mac_<area>_service(rt, opcode)` returns 0 for "not mine",
  >0 handled, <0 halt, and they are tried in a fixed order (time, sound, SANE,
  memory, resource, event, QuickDraw, menu, toolbox, raster, system, geometry,
  then files, cursor, dialog, color, picture, palette, regions, windows,
  pixmaps and VBL). Add a new service to that chain. Unsupported traps halt
  with a diagnostic; they never fall back to Systemless. `runtime_video.c`,
  `runtime_audio.c`, `runtime_keys.c`, `runtime_perf.c` are the SDL/host side.
- **The bridge** (`src/runtime_bridge.c`): a private shared-memory region
  created by the desktop and passed to the child on `--bridge`. The guest
  publishes frames and state (STARTING/READY/CLOSED/FAILED); the desktop reads
  them, draws them into a guest window (`desktop_guest.c`) and pushes mouse
  and key input back. `native_launch.c` resolves the helper beside `ospi`,
  forks it with `--window --indexed --interactive`, and exposes
  `native_surface_*` to the desktop loop.

Opening an item (`open_item` in `session.c` -> `classic_open` in `classic.c`):
text opens in a text window; an extracted application with a resource fork
goes to `native_open`; otherwise the classic path picks the best sibling
archive (`.sit` first, then zip/hqx/bin/sea/cpt, then disk images) and launches
Systemless if installed. `install.c` handles File > Install from Garden
(`garden_*` helpers, host `curl`, `unar -k visible` so resource forks become
`Name.rsrc` sidecars that listings hide). `volume.c` owns the real on-disk
volume (`volume_seed` creates the starting items when missing); `archive_fs.c`
reads archives, `install.c` writes downloads into it, `classic.c` walks it for
sibling archives, and `session_state.c` persists per-volume settings.

Themes, wallpapers, icons and sounds are data tables (`desktop_theme.c`,
`desktop_wallpaper_catalog.h`, `desktop_icon_map.c`, `assets/`) selected per
volume and saved by `session_state.c`. `assets/icons/c/ospi_icon_set.c` loads
the icon pack from `assets/icons/ospi-icons.icl8` at runtime; the Python under
`assets/tools/` renders icons, wallpapers, the fruit mark and sounds offline,
and `make app` converts HD wallpaper masters with `sips` at package time.

## Repo layout notes

- `browser/`: separate CEF/Chromium app (`sh browser/scripts/build.sh`), the
  B01/B02 checklist track. Its GPU frames are embedded in the desktop; the
  complete gate builds and bundles the helper from the candidate source.
- `keyboard/`: on-screen keyboards for foldables; own `keyboard.mk`
  (`make -f keyboard.mk test`), links `../src/font.c`.
- `terminal/`, `videosupport/`, `ios_android/`, `quake/`, `build resources
  from ian/`: untracked. The first three are full forks of this tree from other
  work streams, `quake/` is the Quake III GPL drop, the last holds Ian's
  mockups (the `.png` files are JPEGs; treat as visual references, not assets
  to ship). The root Makefile builds none of them; do not edit them when
  working on the main project.
- `docs/RUNTIME-LOOP-*.md` and `handover.md` are dated evidence records, not
  instructions. `handover.md` holds the latest integration checkpoint and live
  app/volume/save paths; read it before touching a running bundle or saves.

## Working rules

- Keep every file under 500 lines (`src/typeface.c` is at 499). Split rather
  than cross it.
- Do not commit or push unless Ian asks. Author stays `Ian <ian@example.com>`.
  No `Co-Authored-By`. No force-push.
- Never claim a GUI behaviour you did not see in a real window; report
  headless results, windowed results and device results separately. A launch
  or splash is not gameplay acceptance.
- Automated single clicks reach the app as `clicks == 1`; past sessions never
  opened a folder by scripted double-click. Do not treat that as proof a human
  double-click fails, and do not describe scripted opens as verified.
- Systemless is resumable: a save under
  `~/Library/Application Support/Systemless/saves/<sha256>` makes relaunches
  ignore the executable hint, and there is no `--no-resume`. Do not delete
  existing saves.
