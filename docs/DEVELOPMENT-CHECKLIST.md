# OS999 Stylized Development Checklist

## Success means working software

Build our own clean-room classic Macintosh runtime. Run the original target
applications and games with correct graphics, sound, input, persistence, and
measured hardware acceleration. The product-facing name is OS999 Stylized; the
classic desktop remains recognizable while branding and startup evolve. Add
modern browsing and secure networking, with explicitly isolated legacy protocol
compatibility. A desktop, splash screen, successful build, or external
Systemless launch does not pass this goal.

Required end-to-end demonstrations: browse `https://thurrott.com`, play YouTube
video with verified hardware acceleration, and open classic QuickTime inside
our runtime to play a video. These are required outcomes, not optional demos.

This checklist includes [Ian’s 7 November compatibility additions](CLASSIC-COMPATIBILITY-CHECKLIST.md).
Unchecked items are requirements, not claims of existing functionality.

## Historical evidence carried forward — 7 November 2026

The [6 November cross-worktree audit](PROJECT-STATUS-2026-11-06.md) and
[inventory](WORKTREE-INVENTORY-2026-11-06.md) preserve the earlier branch results,
including Ian's observed Monkey Island input/audio and older FPS measurements.
They are dated evidence, not acceptance of this candidate. See the
[master tracker](OS999-MASTER-TRACKER.md) for subsequent integration. Passing
specification-only tests does not establish an implemented feature.

## How we use this checklist

- Keep stable IDs. Mark an item complete only with a linked evidence record.
- Each record names the source revision (or exact dirty-tree snapshot), host,
  architecture, tool versions, artifact SHA-256, command or interaction, expected result, observed result, limitations, and date.
- Track implementation, automated checks, windowed/device checks, performance,
  and Ian's acceptance separately. A pass on one host does not cover another.
- At each work session: read this file and `CLAUDE.md`, select the earliest
  dependency-ready item, preserve unrelated work, and record the result.
- At each checkpoint: update affected items and target rows; retain failures
  and the next reproducible step. Never turn unknowns into passes.
- Run direct local builds/tests for this project, as Ian requested. Do not use
  the build orchestrator. Check tool currency; preserve active jobs and saves.

## Historical baseline

See [the preserved baseline](DEVELOPMENT-BASELINE.md). Newer acceptance evidence follows below.

## 0. Clean-room boundary and target identity

- [ ] C01 Record provenance and compatible licenses for every shipped source,
  font, icon, codec, dependency, and generated test fixture. Ship no Apple ROM, System file, proprietary font, logo, game asset, or copied implementation.
- [ ] C02 Use REA for selected application artifacts and behavioral evidence.
  Keep observations, inferred contracts, implementation, and unknowns separate. Do not decompile Apple code to implement our runtime. Record unsupported
  formats/providers and retain actual Evidence IDs when REA returns them.
- [ ] C03 Identify exact editions, languages, architectures, and hashes for all
  target applications. Keep user-supplied binaries and saves outside Git.
- [ ] C04 Resolve the MacSurf name and correct URL; the supplied URL repeats
  Castle of Dr. Brain. Do not substitute a different browser silently.
- [x] C05 QuickTime scope resolved by Ian on 4 November 2026: open classic
  QuickTime and play a video. Run separately supplied QuickTime as guest software on our independent runtime. Its code/assets stay outside our implementation
  and distribution; do not derive runtime code from Apple's implementation. This records a scope decision only; playback remains unimplemented/unverified.

## 0.5 OS999 Stylized branding and startup

- [ ] S01 Use “OS999 Stylized” as the display/app name, “OS999” in startup,
  “OS999 HD” for the default disk, and the original Granny Smith fruit mark in the menu bar. Remove visible OS Pi labels while preserving internal symbols, bundle/data
  compatibility, and the existing support-volume path. Verify the packaged app.
- [ ] S02 Ship the optional startup introduction enabled by default, with a
  persistent end-user on/off setting and a visible preview/replay action.
- [ ] S03 Use a replaceable, original OS999 emblem; do not copy Apple logos,
  recordings, or other proprietary assets. The supplied startup prompt describes a spinning Apple-shaped icon; adapt that concept to an original silhouette.
  7 Nov (branch `feature/boot-startup-sequence`): the boot screen now draws an
  original pixel-art Granny Smith fruit authored as indexed rows in
  `src/os999_startup_sprite.c`, using only colours already present in the
  project 256-colour icon cube. The supplied `apple_logo.png` and
  `boot logo.png` mockups were used as layout references only; no Apple
  artwork and no bitmap from that folder is vendored or shipped.
- [ ] S04 Drive the initialize → settle → reveal → desktop/failure state machine
  from actual startup work. Keep status truthful, prevent duplicate reveals, provide recovery on failure, and do not invent progress or spin indefinitely.
  7 Nov: `os999_startup_progress()` now reports boot progress and only
  reaches 1.0 once the desktop is genuinely ready; while work is pending it
  climbs with elapsed time and stops at 0.92. A failure freezes the bar at its
  last value and keeps the retry/close dialog.
  8 Nov: the bar is a single continuous zebra whose bands crawl to the right
  at about 40 pixels a second, and the lit part advances to the right with the
  boot. The empty track carries a dim zebra so the bar reads as alive from the
  first frame, and a bright cap marks the head of the fill. The bands keep
  crawling until the desktop takes over the screen, but a failure freezes them
  so the bar never invents progress.
  8 Nov: the bar no longer snaps to full when the desktop session appears.
  Progress completes when the intro leaves the spin phase, which only happens
  once the desktop is genuinely ready, so the fill crosses the track during
  the boot instead of jumping straight to 100%.
- [ ] S05 Keep the wordmark and “Made for the fun of it.” readable and still;
  keep any supporting tagline (“For enthusiasts. By enthusiasts.”) secondary. Spin only while work is pending, land upright smoothly, and avoid long waits.
  Provide a development preview/replay control for repeatable review.
  7 Nov: the wordmark and tagline are drawn in the project's 8x8 pixel font,
  the status line under the progress bar reads exactly `Starting Up...`, and
  the fruit turns only while work is pending, landing upright before the
  reveal. Reduced motion keeps the mark static.
- [ ] S06 Respect reduced motion with a static mark and short dissolve. Any
  startup sound is one optional original sound, respects sound preferences, and never uses a copied Apple chime or delays startup.
- [ ] S07 Reveal the existing functional desktop, menus, windows, icons, and
  application launching without resizing or animating the host window. Pause hidden-view animation and release startup resources after completion.
- [ ] S08 Render a small, crisp Granny Smith fruit icon at the left edge of the
  simulated menu bar. Keep the OS999 menu hit area and dropdown behavior intact; use original generated artwork rather than Apple-owned logo assets.
- [ ] S09 Make the Granny Smith menu open the OS999 menu with “About This Mac”
  first. Credit author Ian Trimble, include remnantsecurity.com as the public home for his creations, plus accurate version/runtime details.
- [ ] S10 Use supplied desktop and boot mockups as layout references. Preserve
  the existing functional desktop; treat the Chromium image as a future target, not implemented browser evidence. Verify wallpaper, windows, menu, About, and
  app launching separately before marking visual acceptance complete.
  7 Nov: the boot screen follows the supplied boot mockup layout — fruit,
  `OS999` wordmark, tagline, striped progress bar — and drops the mockup's
  decorative HD/document/`OS9 ->` icons and the old soft shadow halo. The
  1024x768 frames were reviewed as rendered images; hands-on acceptance in a
  real window is still open.
- [ ] S11 Keep About This Mac compact and centered, fit the author/site/system
  information without an oversized window, and never show temporary host paths or the private volume location.

## 1. Containers, resources, and guest storage

- [x] L01 Implement bounds-checked resource-fork enumeration and type/ID lookup;
  test truncated headers, offsets, names, counts, and oversized payloads. Raw forks/AppleSingle/AppleDouble parser checks plus 20,000 deterministic
  mutations pass. [Evidence and format limits](RUNTIME-LOOP-2026-11-04.md).
- [ ] L02 Preserve data/resource forks, Finder metadata, names, and IDs through
  MacBinary, BinHex, AppleDouble, ZIP, and required StuffIt variants. 6 November 2026: implemented in `storage/` on branch `os999-storage`
  (commit 5099e09). MacBinary, BinHex, AppleSingle/Double, ZIP, StuffIt 1.x-5 (methods 0-3 native; 13, 15 and StuffIt X through `unar` with
  `lsar` metadata) and Compact Pro keep both forks, type/creator, flags and dates. Real Aaru corpora decode identical to their stored copies;
  see `storage/FEATURES.md` SF01-SF08. The Ralph gate (`storage/scripts/ralph-storage-loop.sh`) passes on Linux x86_64 and arm64. A macOS run and Ian's acceptance are still open.
- [ ] L03 Decode required Disk Copy containers before parsing HFS. QuickTime's
  page describes a compressed disk image; `.img` does not imply raw HFS.
  6 November 2026: Disk Copy 4.2, NDIF (raw/zero/ADC; KenCode reported),
  UDIF (zlib/ADC/raw/zero, bzip2 when available), APM, GPT and HFS
  wrappers locate the volume; HFS is read and written, HFS+ is read.
  A real HFS floppy matches the independent machfs reader; a real UDIF
  dmg reads through GPT to HFS+ (`storage/FEATURES.md` SF09-SF15). No
  real NDIF image was available; QuickTime's own image is not yet tried.
- [ ] L04 Implement guest volumes and writable files with path containment,
  symlink/traversal defense, quotas, atomic save updates, and restart recovery.
  6 November 2026: `storage/include/os999_volume.h` resolves every path
  with openat + O_NOFOLLOW, enforces byte and file quotas before writing,
  saves through temporaries + journal + renames, recovers after a crash at
  each durable step, and undoes unfinished installs. Sidecars are read by
  the runtime's own `mac_resource_open_container` (SF16-SF20). The
  desktop does not use it yet; switching installs over is L06 work.
- [ ] L05 Select the exact application deterministically. Report unsupported
  formats and missing resources instead of launching a neighboring executable.
- [ ] L06 Support both a user double-click on a local `.sit` archive and the
  normal download/install route. Preserve the archive, extracted data/resource
  forks and `.rsrc` sidecars, names, and metadata; launch installed apps through
  our runtime. Show extraction/copy failures instead of reporting false success.
  Test traversal/symlink rejection, quotas, partial extraction cleanup, and
  restart behavior with legally obtained user-supplied archives outside Git.
  The current working tree now has an isolated implementation that expands a
  local `.sit` beside its archive and reports Garden extraction/copy failures;
  its synthetic fixture checks passed in the agent worktree. The shared source
  has not yet been rebuilt. `unar` remains required, and temporary expanded
  files are not quota-limited before safe-copy checks.

## 2. Our execution and Macintosh API runtime

  6 November 2026 (branch `os999-l06-install`, from `integration/all-features`):
  `install_sit` (double-click) and `install_garden` (download) now expand
  through the storage stack: content detection, forks and Finder info in
  AppleDouble `.rsrc` sidecars, quota-checked all-or-nothing installs into
  the volume, and `unar` only as the fallback for StuffIt 13/15/X or formats
  we do not decode. Unrecognized files with no unar fail with no folder left
  behind; deep trees no longer stop at depth 8. `test_archive_install`
  passes (native MacBinary install with unar absent, stubbed unar paths,
  Garden route); 69 of the desktop tests that build on the Linux VM pass. A
  macOS app run with a real Garden `.sit` and Ian's acceptance are open.
- [ ] R01 Implement the 68k execution core, memory model, exceptions, instruction
  accounting, and reproducible traces. Validate with independently authored
  instruction vectors, including flags, alignment, branches, and fault cases.
- [ ] R02 Load CODE segments, establish application state and stack, implement
  trap dispatch, and report unsupported instructions/traps with guest addresses.
- [ ] R03 Implement the needed Memory, Resource, File, Event, Window, Menu,
  Dialog, Control, TextEdit, and timing behavior with focused regression probes.
  6 Nov: real-time runs starved VBL/Time Manager tasks (service budget below
  the pollers' minimum); fixed in `mac_runtime_run_cycles`, regression in
  `test_mac_vbl`. MI1 cutscene pace now matches the deterministic probe. See
  [the 6 November loop record](RUNTIME-LOOP-2026-11-06.md).
- [ ] R04 Implement color QuickDraw, palettes, clipping, regions, CopyBits,
  offscreen buffers, fonts, and direct framebuffer writes used by target games.
- [ ] R05 Implement Sound Manager behavior, mixing, timing, callbacks, and
  independent media decoding. Test synchronization and resource cleanup.
  For the MI1 SCUMM Bar opening, record a fresh note trace and correlate it
  with listening: Ian reports audible music that sounded doubled for about 20
  seconds, then mostly alright before returning to normal. An older trace
  logged 26 note events over its first 20 guest seconds and no same-pitch
  reissue within 250 ms, but it did not capture PCM or the latest session. A
  startup gap and callback underruns correlate but do not prove cause. Zero
  clipped samples or a healthy host queue alone do not pass.
  Finder-launched games now set `OSPI_PERF_TRACE=1` automatically and log
  presentation FPS plus p50/p95/p99 and maximum frame intervals once per
  second. Set the flag for standalone runtime launches too;
  `OSPI_AUDIO_TRACE=1` additionally enables audio telemetry. These are
  presenter timings, not GPU execution, input latency, or
  proof of hardware rendering. The live Finder-launched session has no FPS
  trace, so its current frame rate is unknown. `build/ospi-runtime-capture` can
  make an opt-in, up-to-120-second WAV via
  `OSPI_AUDIO_WAV=/tmp/mi1.wav`; capture the same opening with audio and note
  traces after the live run is closed. This records producer PCM before host
  callback underruns, so retain callback telemetry and listening as separate
  evidence. Ian's later live MI1 report: in SCUMM Bar and Sword Master scenes,
  he heard persistent ticking instead of music. It took about a minute for
  normal music to resume; another scene sounded good for about 20 seconds
  before ticking returned, then grew louder. The game continued advancing.
  This session has no WAV, note trace, or callback trace, so the cause is
  unknown. Ian supplied a [YouTube comparison reference](https://www.youtube.com/watch?v=DoVi1__jNsI&t=2242s)
  whose Mac-version segment starts at 37:23. It is a
  listening reference only; no synchronized sample comparison has been captured.
- [ ] R06 Add PowerPC/PEF/CFM and Mixed Mode only against selected targets and
  separate tests; a 68k pass does not establish PPC compatibility.
- [ ] R07 Make the independent runtime the application launch path. Remove
  Systemless from required execution and packaging after replacement is proven.
- [ ] R08 Present guest application windows as OS999 desktop content. Clip
  them to the desktop, route input through the desktop, and prevent guest windows
  from escaping the OS999 frame. The source has a shared-memory guest
  framebuffer/input bridge and draws a bounded, draggable guest window. An
  isolated MI1 probe published a READY 640x480 frame through that bridge
  (sequence 398); bridge and launcher tests pass. Finder dispatch routes
  ITEM_APPLICATION to native_open and reports missing CODE resources. Ian's
  later user-driven launch succeeded: screenshots show MI1 composed inside the
  desktop and advancing through story scenes; he says gameplay otherwise works.
  An earlier report showed the game window could be dragged beyond the desktop;
  newer screenshots establish in-desktop composition but do not verify bounds.
  Earlier GUI automation failed to click Applications (both attempted clicks
  landed at 0,0); that was an automation-only failure. Manually verify that
  guest dragging stays clipped to desktop bounds.
- [ ] R09 Match the supplied Mac OS 9 references with original clean-room folder,
  disk, document, application, and trash artwork. Do not copy Apple artwork.
  Guest application icons may be read at runtime from the installed,
  user-supplied application's resource fork; never bundle that data. The 8-bit
  icon renderer now maps the Macintosh system color cube and four color ramps;
  its table is covered by focused regression tests. A fresh desktop capture
  shows the current built-in icon set; its folder, disk, document, and Trash
  drawings still need comparison and refinement against Ian's Mac OS 9
  references. The installed MI1 application icon was verified in an off-screen
  render, but was not opened in this fresh GUI check. Keep this item open.

## 2.5 Delivered appearance assets and integration loop

### Font Book 999 and international text

- [x] F01 Inventory every repository font candidate before implementation and
  separate canonical licensed assets from generated, dependency, and sibling-
  worktree copies. The 7 November audit found ChicagoFLF, two Meslo terminal
  faces, and four IBM Plex dashboard weights; no canonical suitcase or OTF.
- [x] F02 Add a bounded catalog and integrity parser for TTF, OTF, TTC, Type 1,
  raw resource forks, and AppleSingle/AppleDouble font resources. Invalid named
  fonts remain visible as Problems and cannot install or activate.
- [x] F03 Add atomic simulated-volume installation and a persistent activation
  registry with symlink rejection, collision refusal, last-face protection,
  and transactional desktop typeface reload.
- [x] F04 Add the Font Book 999 utility under Applications/Utilities and the
  OS999 Control Panels submenu, with Collections, family list, and live preview
  panes in a standard classic window. Focused off-screen tests pass; live visual
  and hands-on interaction acceptance remain open.
- [x] F05 Add a hash-pinned, OFL-licensed Noto coverage pack for Latin/Greek/
  Cyrillic, Arabic, Hebrew, major Indic scripts, Thai, symbols, monochrome
  emoji, and ten CJK faces. Metadata scans pass. Bundle cost is 24.29 MiB.
- [x] F06 Shape complex scripts with HarfBuzz, apply Unicode bidirectional
  ordering, use script-aware line-break opportunities, and resolve missing
  glyphs through ordered per-glyph fallback. Test mixed scripts, combining
  marks, Arabic joining, Indic clusters, Thai, Hebrew, CJK face indices, emoji
  variation/ZWJ behavior, malformed UTF-8, and bounded allocation. The first
  integrated pass uses HarfBuzz 15.1.0, FriBidi 1.0.18 and libunibreak 8.0,
  with explicit degraded builds when a library is absent. Full-library tests
  cover shaping, fallback, bidi, line breaks, and framebuffer drawing. FriBidi
  uses Unicode 14 data; a future permissive Unicode-17 bidi replacement remains
  a release/packaging decision. Human-language visual review remains F07.
- [ ] F07 Capture real one-window screenshots of Font Book and multilingual
  sample documents, with italic captions and capture dates. Obtain human review
  from readers of represented languages; metadata and pixels alone are not
  language-quality acceptance.

- [x] V01 Inventory the local delivery without overwriting existing work.
  On 5 November, all 1,499 local asset files matched bundle tip `1c1b1b9`
  byte-for-byte: 59 wallpapers in five sizes, 82 icons, seven cursors,
  HiDPI variants, and 64 original synthesized sounds. This is file evidence,
  not rendering or listening acceptance. The default merge script refused
  because assets were already copied but untracked; nothing was overwritten.
- [ ] V02 Integrate the supplied sixteen-theme engine, Appearance menu,
  checked selection, Cmd-T, volume themes and persisted selection. The local
  handoff describes commit `f876cdb`, but the delivered bundle ends at
  `1c1b1b9`. Theme code, integration patch and theme-aware script are missing.
  A new local palette-theme implementation was added independently, using
  the requested 16 names. This is not the missing delivered engine: it changes
  desktop colors only, with no shape skins, custom-file loader or automatic
  wallpaper/sound/font collections. Production checks cover all 16 selections,
  restart persistence, Cmd-T and unchanged guest-icon pixels. Both the isolated
  and combined-checkout full suites pass; a fresh preview shows Titanium Night
  chrome. Ian’s theme selection/visual acceptance remains pending.
- [ ] V03 Use the delivered icon pack in the desktop and package required
  resources. Preserve guest-provided icons. Distinguish logical 32px rendering
  from the still-required native HiDPI backing-store path and cursor scaling.
  The icon pass maps recognized application names, folder roles and document
  types to the existing pack while keeping guest-provided app icons. Control
  Panels now has an Appearance icon and contextual actions. The isolated full
  suite, 62 panel checks and shared focused integration checks pass. A fresh
  5 November window capture confirms the Applications/Documents folder icons;
  Ian confirmed the Control Panels contextual-menu fix works. App-specific
  artwork and native HiDPI rendering still await acceptance.
- [ ] V04 Connect wallpaper selection and persistence to the actual desktop;
  validate aspect ratios and any macOS bitmap conversion on a disposable volume.
  The wallpaper integration adds all 59 delivered pictures to a paged Appearance
  menu, persists the choice per volume, and preserves the plain default. Bitmap
  decoding, aspect-preserving cover fit, missing-file fallback and menu behavior
  pass automated checks. A 5 November macOS window capture shows Aqua Bokeh,
  the Appearance menu title and readable icon labels. Restart persistence
  remains separately checked by automated tests;
  this does not implement the separately missing 16-theme engine.
  Ian approved the wallpaper appearance and supplied screenshots showing
  Spiral Galaxy selected in the open menu on 5 November 2026.
- [ ] V07 Provide OS999 → Control Panels → Appearance navigation with Desktop
  and Themes tabs, plus Control-click/right-click contextual menus for desktop
  and folder items. Preserve guest input and separate absent themes from working
  desktop pictures. Implemented in the Controls Preview: isolated full tests,
  38 panel checks and real-session navigation/persistence checks pass. A live
  right-click opened the desktop menu on 5 November; automated left-click
  activation was blocked by the GUI tool, so panel interaction and Control-click
  were initially unverified. Ian subsequently confirmed the panel contextual
  interaction works. The other session’s HD-icon test now passes 639 checks;
  its private theme fixtures do not exercise production theme implementation.
- [ ] V09 Complete the final Mac OS 9-style desktop/menu goal. R1–R7 add
  Finder file actions, aliases, recent items, native printing, saved layouts,
  pop-up windows and list columns. R8 combines these with release features and
  newer main runtime fixes in an isolated worktree. Full combined tests/package
  pass. R9 adds overlay stacking/input checks, including sanitizer coverage;
  R10 adds player registration/hiding and background stacking with tests.
  R12 adds browser registration/lifecycle and Finder stacking tests;
  R13 adds scoped menu dispatch and browser editing; full application stacking
  and integrated GUI acceptance remain pending. R14 fixes modal Quit and adds
  explicit browser IME cancellation on overlay/app interruption. R15 adds
  retained Appearance/guest foreground composition and input routing. R16
  scopes browser menu availability to existing windows and editor state. R17
  routes guest Close/Quit through atomic runtime shortcut delivery. R18 removes
  the host Minimize conflict with Finder Make Alias (Cmd-M). R19 adds idle
  folder refresh with selection/layout retention; full suite/package pass.
  R20 hardens listing/icon reads. R21 expands ordinary folders and bulk Undo;
  137-file focused/sanitizer checks and full suite/package pass. R22 expands
  search/clipboard storage; 215 focused/sanitizer checks and full suite/package pass.
  R23 adds all-app cycling and graceful guest close-box dispatch; full suite/package pass.
  R24 shares app stacking and modeless focus; focused checks and full suite/package pass.
  R25 adds a floating switcher and drag outlines; full suite/package and sanitizer pass.
  R26 persists switcher preferences; GUI restore is verified. R27 fixes release
  endpoints; full suite/package pass. R28 adds single-media-file switcher drops;
  R29 Arrange and R30 spring folders pass full suite/package. R31 labels pass full validation. R32 adds saved spring-folder preferences;
  full suite/package pass. R33 corrects alias drops; R34 desktop files pass full checks; R35 placement passes focused checks; R36 overflow/spatial keys pass full checks; R37 system icons in validation; GUI open.
  See [desktop loop](DESKTOP-LOOP-2026-11-06.md) and [integration](DESKTOP-INTEGRATION-2026-11-06.md).
- [ ] V08 Complete useful File/Edit/View/Special/Help menu actions. Current
  menu pass adds Get Info, Close All, Select All, Copy Names/Text, per-window
  Icon/List and Name/Kind sorting, keyboard menu navigation and Help topics.
  Cut/Paste/Undo remain disabled. On 5 November, isolated and combined
  `make -j2 test all` passed; the separate Menus Preview bundle passed strict
  signature verification and launched on a copied volume. Runtime clipboard
  acceptance, list interactions and menu keyboard behavior need Ian’s hands-on
  review: automated foreground input could not verify its exact target.
  The next pass adds folder keyboard navigation with selection scrolling,
  clickable list headings, row-sized list scroll buttons, context shortcuts and
  correct New Folder targeting for selected desktop volumes. Startup preview
  shortcuts respect overlays. Commit 880fbb4 adds explicit blank-desktop focus:
  background clicks clear folder selections and block stale item commands.
  Its isolated full suite and combined desktop integration checks passed;
  Focus Preview launched on a copied volume. Hands-on acceptance is pending.
  Scrolling follow-up adds text arrow/page/Home/End controls and folder page
  selection, and prevents background wheel events from scrolling old windows.
  Full isolated build/tests and combined desktop integration passed on
  6 November; focused navigation passed 188 sanitizer checks. Live acceptance
  remains pending.
  On 6 November, isolated and combined build/test suites passed, including 91
  focused navigation checks. The signed Navigation Preview launched on a copied
  volume; a fresh window capture confirms rendering. Automated keyboard input
  was blocked by exact-window focus, so hands-on navigation acceptance is pending.
- [ ] V05 Connect original host startup/interface sounds, mute preference and
  bounded nonblocking playback. Judge them by listening; synthesized waveforms
  and a successful audio-device open do not establish sound quality.
- [ ] V06 At major validated checkpoints, commit scoped source/assets locally
  and launch a separate test bundle for Ian. Preserve existing runs, saves,
  and unrelated dirty work. Never replace the bundle of a live app in place.
  Record the exact artifact, tests, live observations, failures and feedback.

Continue with [gameplay, browser, networking and release acceptance](DEVELOPMENT-CHECKLIST-RUNTIME.md).
