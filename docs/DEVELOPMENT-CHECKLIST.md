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

This checklist records Ian’s requirements through 5 November 2026.
Unchecked items are requirements, not claims of existing functionality.

## How we use this checklist

- Keep stable IDs. Mark an item complete only with a linked evidence record.
- Each record names the source revision (or exact dirty-tree snapshot), host,
  architecture, tool versions, artifact SHA-256, command or interaction,
  expected result, observed result, limitations, and date.
- Track implementation, automated checks, windowed/device checks, performance,
  and Ian's acceptance separately. A pass on one host does not cover another.
- At each work session: read this file and `CLAUDE.md`, select the earliest
  dependency-ready item, preserve unrelated work, and record the result.
- At each checkpoint: update affected items and target rows; retain failures
  and the next reproducible step. Never turn unknowns into passes.
- Run direct local builds/tests for this project, as Ian requested. Do not use
  the build orchestrator. Check tool currency; preserve active jobs and saves.

## Current baseline

The C11/SDL2 desktop routes extracted CODE-resource applications to our
development runtime; unsupported archive formats retain the older Systemless
fallback. Our runtime combines Musashi instruction execution with independently
implemented loading and partial Macintosh API services. User screenshots now
show original MI1 running in a guest window inside the desktop and progressing
through scenes; mouse controls and effects respond. Ian reports intermittent
ticking and missing or delayed music in the SCUMM Bar and Sword Master scenes.
The current live session has no audio capture, trace, or FPS measurement. Guest
window drag containment, sustained play, saves, and full acceleration remain
unverified. Networking and classic QuickTime playback remain unimplemented.
See [the first implementation loop](RUNTIME-LOOP-2026-11-04.md) for evidence,
limitations, exact artifacts, and the next reproducible steps.

The first OS999 intro preview looked low quality to Ian: one quick rotation
followed by the desktop. A subsequent frame still showed faceting and a detached
leaf. Ian said the software-rendered Granny Smith mesh looked much better, then
requested 25% of its previous size. The fruit is now scaled to one quarter,
rebuilt, and visible in a disposable-volume preview; final user acceptance is
pending. Ian supplied a boot-screen mockup with a framed display, pixel-art
apple, OS999 wordmark, and “Starting Up…” treatment. Use it as a visual target
while preserving truthful readiness and the previously requested short startup.
The small fruit mark is visible in the menu bar, but its click/menu behavior
has not yet been checked.

Ian also supplied `build resources from ian/Desktop1.png` through `Desktop4.png`
as OS999 desktop references. `Desktop1` and `Desktop2` show the blue desktop,
disk and Finder windows, Granny Smith menu mark, and About panel; `Desktop3` and
`Desktop4` show a future app menu and Chromium window. The `.png` files are
actually JPEG mockups. `apple_logo.png` is a pixel-art Granny Smith reference,
not an Apple logo asset to ship. Treat all of these as visual references until
asset ownership/provenance is recorded; mockups do not prove app functionality.
The menu item and About This Mac text identify Ian Trimble and show
`remnantsecurity.com`. Ian clarified that the site is where he houses all
the things he creates publicly. His screenshot showed the About window too
large and exposing the temporary volume path. Current source describes the
site's role, removes that host path, and centers a smaller About window; a fresh
packaged visual review remains due.

On 4 November 2026, `BUILDFLEET_LOCAL=1 make test` passed the complete local C
suite. `BUILDFLEET_LOCAL=1 make runtime` reported no work. REA CLI 6.8.0 matches
the checked npm release; its Codex registration is aligned, but no analysis
provider is configured and REA tools are not connected in this agent session.
On 5 November the shared REA skill was refreshed to version 49. Earlier REA
ZIP inventories succeeded for Monkey Island 2 and Castle of Dr. Brain; their
Evidence IDs are retained in the loop record. CPU traces are separate evidence.

## 0. Clean-room boundary and target identity

- [ ] C01 Record provenance and compatible licenses for every shipped source,
  font, icon, codec, dependency, and generated test fixture. Ship no Apple ROM,
  System file, proprietary font, logo, game asset, or copied implementation.
- [ ] C02 Use REA for selected application artifacts and behavioral evidence.
  Keep observations, inferred contracts, implementation, and unknowns separate.
  Do not decompile Apple code to implement our runtime. Record unsupported
  formats/providers and retain actual Evidence IDs when REA returns them.
- [ ] C03 Identify exact editions, languages, architectures, and hashes for all
  target applications. Keep user-supplied binaries and saves outside Git.
- [ ] C04 Resolve the MacSurf name and correct URL; the supplied URL repeats
  Castle of Dr. Brain. Do not substitute a different browser silently.
- [x] C05 QuickTime scope resolved by Ian on 4 November 2026: open classic
  QuickTime and play a video. Run separately supplied QuickTime as guest software
  on our independent runtime. Its code/assets stay outside our implementation
  and distribution; do not derive runtime code from Apple's implementation.
  This records a scope decision only; playback remains unimplemented/unverified.

## 0.5 OS999 Stylized branding and startup

- [ ] S01 Use “OS999 Stylized” as the display/app name, “OS999” in startup,
  “OS999 HD” for the default disk, and the original Granny Smith fruit mark
  in the menu bar.
  Remove visible OS Pi labels while preserving internal symbols, bundle/data
  compatibility, and the existing support-volume path. Verify the packaged app.
- [ ] S02 Ship the optional startup introduction enabled by default, with a
  persistent end-user on/off setting and a visible preview/replay action.
- [ ] S03 Use a replaceable, original OS999 emblem; do not copy Apple logos,
  recordings, or other proprietary assets. The supplied startup prompt describes
  a spinning Apple-shaped icon; adapt that concept to an original silhouette.
- [ ] S04 Drive the initialize → settle → reveal → desktop/failure state machine
  from actual startup work. Keep status truthful, prevent duplicate reveals,
  provide recovery on failure, and do not invent progress or spin indefinitely.
- [ ] S05 Keep the wordmark and “Made for the fun of it.” readable and still;
  keep any supporting tagline (“For enthusiasts. By enthusiasts.”) secondary.
  Spin only while work is pending, land upright smoothly, and avoid long waits.
  Provide a development preview/replay control for repeatable review.
- [ ] S06 Respect reduced motion with a static mark and short dissolve. Any
  startup sound is one optional original sound, respects sound preferences, and
  never uses a copied Apple chime or delays startup.
- [ ] S07 Reveal the existing functional desktop, menus, windows, icons, and
  application launching without resizing or animating the host window. Pause
  hidden-view animation and release startup resources after completion.
- [ ] S08 Render a small, crisp Granny Smith fruit icon at the left edge of the
  simulated menu bar. Keep the OS999 menu hit area and dropdown behavior intact;
  use original generated artwork rather than Apple-owned logo assets.
- [ ] S09 Make the Granny Smith menu open the OS999 menu with “About This Mac”
  first. Credit author Ian Trimble, include remnantsecurity.com as the public
  home for his creations, plus accurate version/runtime details.
- [ ] S10 Use supplied desktop and boot mockups as layout references. Preserve
  the existing functional desktop; treat the Chromium image as a future target,
  not implemented browser evidence. Verify wallpaper, windows, menu, About, and
  app launching separately before marking visual acceptance complete.
- [ ] S11 Keep About This Mac compact and centered, fit the author/site/system
  information without an oversized window, and never show temporary host paths
  or the private volume location.

## 1. Containers, resources, and guest storage

- [x] L01 Implement bounds-checked resource-fork enumeration and type/ID lookup;
  test truncated headers, offsets, names, counts, and oversized payloads.
  Raw forks/AppleSingle/AppleDouble parser checks plus 20,000 deterministic
  mutations pass. [Evidence and format limits](RUNTIME-LOOP-2026-11-04.md).
- [ ] L02 Preserve data/resource forks, Finder metadata, names, and IDs through
  MacBinary, BinHex, AppleDouble, ZIP, and required StuffIt variants.
- [ ] L03 Decode required Disk Copy containers before parsing HFS. QuickTime's
  page describes a compressed disk image; `.img` does not imply raw HFS.
- [ ] L04 Implement guest volumes and writable files with path containment,
  symlink/traversal defense, quotas, atomic save updates, and restart recovery.
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
  shortcuts respect overlays. Blank-desktop focus still needs an explicit model
  so clicking the background cannot leave old folder commands as the target.
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

## 3. Gameplay and acceleration

- [ ] G01 Agree a reproducible gameplay route for each game: startup, menus,
  scene changes, interactions, audio, save, quit, relaunch, and restore.
- [ ] G02 Run original Macintosh executables through our runtime. ScummVM,
  ports, remakes, and external emulators do not satisfy this gate.
- [ ] G03 Implement GPU-backed rendering for supported guest drawing operations;
  demonstrate which work runs on the GPU. Uploading a CPU-rendered framebuffer
  alone does not establish full guest graphics acceleration.
- [ ] G04 Preserve correct palette animation, scaling, clipping, transparency,
  and readback/direct-write semantics. Compare accelerated and reference output.
- [ ] G05 Measure CPU/GPU time, frame pacing, p50/p95/p99 frame time, memory,
  audio underruns, and input latency during equal gameplay on each target host.
  Agree numerical budgets before acceptance and record hardware/backend names.
- [ ] G06 Document every software fallback. Required accelerated paths must fail
  their acceleration gate if they silently fall back to software.
- [ ] G07 Complete sustained play and save/reload tests, then obtain Ian's
  hands-on acceptance. No game currently has a passing acceptance record.

| Target | Page-level evidence | Required acceptance | Current result |
| --- | --- | --- | --- |
| Secret of Monkey Island | Macintosh minimum: 68020, 2 MB, System 6.0.7+, color display, hard disk | Original executable, scene/input/audio checks, save/reload | User screenshots show Finder-launched MI1 running and progressing inside the OS999 desktop; Ian reports working mouse controls and effects and says gameplay otherwise works. Audio has intermittent ticking, delayed music, and an initial doubled-note report. Current live session has no FPS or audio trace. Guest-window containment, guest QuickDraw acceleration, sustained play, save/reload, and final acceptance remain open |
| Monkey Island 2 | Macintosh minimum: 16 MHz 68020, 2 MB, System 6.0.7+, 256-color display | Scene/music transitions, smoothing, save/reload | Disposable runs accepted the observed 54/54 Trench Mouth and 56/47 Gout entries, but later generic-table answers were rejected. A Mac player reports the Mac wheel differs subtly from the PC wheel, so the table is not validated as a complete Mac answer key ([report](https://www.reddit.com/r/MonkeyIsland/comments/1ojz11v/is_the_mac_code_wheel_different/)). Full mode passed copy protection, title, and multiple opening story scenes. After raising Doubleback and timer callback budgets from 10,000 to 100,000 (each with a >10k regression), the runtime remained live through 4+ minutes of scripted opening. Space paused/resumed and Escape advanced the intro. Audio used a dummy device. Free-play, sustained sound/music, frame pacing, and save/reload remain unverified. Earlier lookup-screen trace is not gameplay evidence. See [Part 3](RUNTIME-LOOP-2026-11-04-PART3.md) |
| Castle of Dr. Brain | 68k; 256 colors; saves in game directory | Puzzles, palette/input/audio, local save/reload | Creates resource-defined windows and paints its 384 by 198 startup picture; then stops at Alert 143, “Can't load !??”; no gameplay |
| Classic QuickTime | Multiple 68k/PPC installers in compressed image | Open the actual guest player and play a video with sound, seeking, and correct timing | Required; not run in our runtime |
| MacSurf | Correct identity/link needed; supplied Garden link repeats Castle of Dr. Brain | Selected browser's rendering and network flows | Identity pending; do not silently substitute another app |
| Chromium | Modern host-native browser track | Sandboxed browsing, GPU, updates, network/security tests | Not built or integrated |
| Thurrott.com | User-selected live website | Browse articles, navigate, scroll, and display media correctly in the integrated browser | Not tested |
| YouTube.com | User-selected live website | Sustained video/audio playback with proven hardware decode and GPU composition | Not tested |

The Macintosh minimums above are documented by [PixelatedArcade's MI1
specifications](https://www.pixelatedarcade.com/games/the-secret-of-monkey-island/techspecs)
and [MI2 specifications](https://www.pixelatedarcade.com/games/monkey-island-2-lechuck-s-revenge/techspecs).
[Just Adventure's MI2 requirements](https://www.justadventure.com/2005/01/01/monkey-island-2-lechucks-revenge/)
also lists a 16 MHz 68020 and 256-color display. These are compatibility
minimums, not an FPS benchmark; acceptance needs measured playback targets.

The game pages support a 68k-first plan. They do not establish a 3D requirement.
Add QuickDraw 3D/RAVE/OpenGL gates when an identified target requires them.

## 4. Modern browser integration

- [ ] B01 Evaluate Chromium as the host-native, isolated browser integrated
  with the OS999 desktop. Building current Chromium as a classic 68k application
  is a separate port, not an ordinary compilation option. A macOS WKWebView
  feasibility prototype compiles, but is a separate, unlaunched Cocoa window;
  it is not desktop integration, a cross-platform path, or acceptance evidence.
- [ ] B02 Select supported Chromium revisions, host architectures, reproducible
  build configuration, dependency licenses, storage budget, and update ownership.
  Compile a dedicated build if integration requires it; keep it outside this
  small runtime's source tree and preserve upstream security boundaries.
- [ ] B03 Verify renderer, GPU, and network process separation and sandboxing.
  Reject `--no-sandbox` and global certificate-validation bypasses.
- [ ] B04 Specify a narrow IPC surface for windows, input, clipboard, downloads,
  and URL opening. Validate messages; require explicit host-file permissions.
- [ ] B05 Test modern HTTPS, JavaScript, storage isolation, accessibility,
  downloads, Unicode, resizing, GPU rendering, and crash recovery.
- [ ] B06 Establish security-update checks, supported-version enforcement,
  rollback handling, and repeatable upgrade tests before calling it secure.
- [ ] B07 Browse `https://thurrott.com` from OS Pi's integrated browser: load
  the homepage, open multiple public articles, follow links, use back/forward,
  scroll, resize, and verify text, images, menus, and permitted embedded media.
  Record date and exact URLs because site content changes. Authentication and
  paywalls must retain their normal behavior; do not bypass access controls.
- [ ] B08 Browse `https://youtube.com`, search, select a video, and test playback,
  pause/resume, seeking, volume, captions, fullscreen, and windowed return.
  Initial proposed baseline: 1080p60 for ten minutes on each target host;
  confirm the performance budget before acceptance. Record the exact video,
  delivered resolution/frame rate, codec, network conditions, and test duration.
- [ ] B09 Prove full acceleration for the selected YouTube workload using the
  browser's GPU/media diagnostics and host measurements: hardware video decode,
  GPU composition/presentation, decoder/backend identity, CPU/GPU load, dropped
  frames, frame pacing, and A/V synchronization. A GPU-enabled browser or smooth
  playback alone is insufficient. Software decode fails this acceleration gate.
  Record codec/profile-specific support; do not infer support for every codec
  or resolution from one successful stream.

### Classic QuickTime playback

- [ ] Q01 Select an exact classic QuickTime/player version and architecture;
  inventory the guest components it requires. Preserve separately supplied
  installers and extensions outside Git. Do not substitute a modern host player.
- [ ] Q02 Open the guest player from OS Pi, choose a permitted test movie through
  its file dialog, and play visible video with audible synchronized sound.
  Use an original or redistributable fixture with known codec, dimensions,
  frame rate, duration, and hash. Select a codec supported by that guest version.
- [ ] Q03 Verify play/pause, repeated seeking, end-of-file, window resize,
  close/reopen, and graceful handling of malformed or unsupported movies.
  Include a sustained playback test and record drift, dropped frames, and leaks.
- [ ] Q04 Measure QuickTime's actual rendering/decode paths separately from
  YouTube. Record GPU presentation and any CPU codec execution explicitly;
  do not claim hardware video decoding merely because the movie is displayed
  through a GPU. Any required accelerated path must pass its own evidence gate.

## 5. Modern networking and legacy compatibility

- [ ] N01 Use maintained host socket and TLS implementations. Implement guest
  API adapters rather than inventing cryptography or an unnecessary kernel stack.
- [ ] N02 Test IPv4/IPv6, DNS, TCP/UDP, async completion, cancellation, timeouts,
  partial I/O, disconnects, proxy behavior, and bounded buffering.
- [ ] N03 Require certificate-chain and hostname verification, secure trust-store
  handling, TLS 1.2/1.3 policy, and tested rejection of invalid/expired identities.
- [ ] N04 Identify the MacTCP/Open Transport and application protocol subset
  needed by confirmed targets; document unsupported APIs and error behavior.
- [ ] N05 Inventory desired legacy protocols (candidate HTTP, FTP, Telnet,
  AppleTalk/AFP) against actual use cases before claiming support.
- [ ] N06 Put insecure legacy protocols behind an explicit compatibility profile:
  disabled by default, destination restrictions, isolated credentials, and clear
  status. Do not weaken Chromium or the modern TLS policy to satisfy old apps.
- [ ] N07 For a legacy-to-modern gateway, disclose where encryption terminates
  and where plaintext exists. Test downgrade resistance and credential handling;
  a TLS gateway does not make a plaintext guest connection end-to-end encrypted.
- [ ] N08 Enforce per-app network permissions, inbound-listener restrictions,
  guest/host isolation, and secret-redacted diagnostics. Test denied destinations.
- [ ] N09 Fuzz parsers and protocol state machines, test hostile servers and
  resource exhaustion, and document supported dependency/security-update windows.

## 6. Cross-platform and release acceptance

- [ ] A01 Run unit/regression checks and sanitizer/fuzz suites on exact revisions;
  retain deterministic synthetic fixtures without proprietary contents.
- [ ] A02 Separately validate Apple Silicon macOS, Raspberry Pi Linux arm64, and
  Linux x86_64. Record actual hardware, display, audio, and input evidence.
  6 Nov: Debian 12 arm64 and x86_64 containers pass `make test all probe`
  (loop record); no Raspberry Pi or windowed Linux run.
- [ ] A03 Audit host filesystem/network containment and guest isolation. CPU
  emulation itself is not a security sandbox.
- [ ] A04 Produce dependency/provenance manifests and package only permitted
  components. Verify installation, upgrade, rollback, and save preservation.
- [ ] A05 Capture real single-window evidence with dated italic captions when
  publishing. Keep third-party program screenshots out of Git per project rules.
- [ ] A06 Require all selected target, acceleration, browser, QuickTime,
  networking, security, OS999 branding/startup, and Ian's acceptance gates
  before declaring
  project success.
  Publishing, deployment, deletion, and spending retain separate approval rules.

## Sources and evidence boundaries

Pages inspected on 4 November 2026; page descriptions are not execution proof.

- [Secret of Monkey Island](https://macintoshgarden.org/games/secret-of-monkey-island)
- [Monkey Island 2](https://macintoshgarden.org/games/monkey-island-2-le-chucks-revenge)
- [Castle of Dr. Brain](https://macintoshgarden.org/games/castle-of-dr-brain)
- [QuickTime archive](https://macintoshgarden.org/apps/quicktime)
- [Chromium Linux builds](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md)
- [Chromium macOS builds](https://chromium.googlesource.com/chromium/src/+/main/docs/mac_build_instructions.md)
- [Chromium sandbox design](https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md)
- [OpenSSL release/support policy](https://openssl-library.org/policies/releasestrat/index.html)
