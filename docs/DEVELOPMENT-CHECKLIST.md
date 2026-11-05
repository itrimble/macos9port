# OS Pi Development Checklist

## Success means working software

Build our own clean-room classic Macintosh runtime. Run the original target
applications and games with correct graphics, sound, input, persistence, and
measured hardware acceleration. Add modern browsing and secure networking,
with explicitly isolated legacy protocol compatibility. A desktop, splash
screen, successful build, or external Systemless launch does not pass this goal.

Required end-to-end demonstrations: browse `https://thurrott.com`, play YouTube
video with verified hardware acceleration, and open classic QuickTime inside
our runtime to play a video. These are required outcomes, not optional demos.

This is the development contract requested by Ian on 4 November 2026.
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

The C11/SDL2 desktop now routes extracted CODE-resource applications to our
development runtime; unsupported archive formats retain the older Systemless
fallback. Our runtime combines Musashi instruction execution with independently
implemented loading and partial Macintosh API services. Original game startup
code executes, but no target has passed sustained play and save/reload. MI1 has
responsive controls and audible music/effects, with an opening music duplication
report still under investigation. SDL presentation is distinct from guest
graphics acceleration. Networking and media remain unimplemented.
See [the first implementation loop](RUNTIME-LOOP-2026-11-04.md) for evidence,
limitations, exact artifacts, and the next reproducible steps.

On 4 November 2026, `BUILDFLEET_LOCAL=1 make test` passed the complete local C
suite. `BUILDFLEET_LOCAL=1 make runtime` reported no work. REA CLI 6.8.0 matches
the npm release checked that day; its Codex registration is aligned, while its
shared skill copy is stale and the live MCP server is unknown. `rea analyze
build/ospi-runtime` returned `provider_unavailable` because no native analysis
provider is configured. No REA setup or global configuration change was made.
Earlier REA ZIP inventories succeeded for Monkey Island 2 and Castle of Dr.
Brain; their Evidence IDs are retained in the loop record. CPU traces are
separate evidence.

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

## 2. Our execution and Macintosh API runtime

- [ ] R01 Implement the 68k execution core, memory model, exceptions, instruction
  accounting, and reproducible traces. Validate with independently authored
  instruction vectors, including flags, alignment, branches, and fault cases.
- [ ] R02 Load CODE segments, establish application state and stack, implement
  trap dispatch, and report unsupported instructions/traps with guest addresses.
- [ ] R03 Implement the needed Memory, Resource, File, Event, Window, Menu,
  Dialog, Control, TextEdit, and timing behavior with focused regression probes.
- [ ] R04 Implement color QuickDraw, palettes, clipping, regions, CopyBits,
  offscreen buffers, fonts, and direct framebuffer writes used by target games.
- [ ] R05 Implement Sound Manager behavior, mixing, timing, callbacks, and
  independent media decoding. Test synchronization and resource cleanup.
  For the MI1 SCUMM Bar opening, record a fresh note trace and correlate it
  with listening: Ian reports audible music that sounded doubled for about 20
  seconds, then mostly alright before returning to normal. A startup gap and
  callback underruns correlate but do not prove cause. Zero clipped samples or
  a healthy host queue alone do not pass.
- [ ] R06 Add PowerPC/PEF/CFM and Mixed Mode only against selected targets and
  separate tests; a 68k pass does not establish PPC compatibility.
- [ ] R07 Make the independent runtime the application launch path. Remove
  Systemless from required execution and packaging after replacement is proven.

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
| Secret of Monkey Island | Macintosh minimum: 68020, 2 MB, System 6.0.7+, color display, hard disk | Original executable, scene/input/audio checks, save/reload | Ian confirmed mouse interaction, door animation, and its sound effect. SCUMM Bar music initially sounded doubled for about 20 seconds, then mostly okay before returning to normal. The stale Finder bundle was rebuilt after closing a disposable game copy; its runtime hash now matches standalone (`3e1a78c0…224ff7c`) and its code signature verifies. A fresh trace on the rebuilt runtime measured about 15.9–16.2 MHz and 59–60 FPS, with zero underruns or clipped frames after ring priming. Ian has not listened to this build yet. Finder-based launch, audio fidelity, save/reload, and hands-on acceptance remain open. Metal accelerates presentation only; guest QuickDraw still runs on CPU |
| Monkey Island 2 | Macintosh minimum: 16 MHz 68020, 2 MB, System 6.0.7+, 256-color display | Scene/music transitions, smoothing, save/reload | Fresh runtime reaches the Voodoo recipe lookup. Five disposable-copy entries failed: 50/38 Warts; 27/12 Worms; 26/17 Scurvy (Breath Mints + Boxes of Voodoo Helper); 38/17 Scurvy (Bloated Ticks + Servings of Fruit Cocktail); 56/16 Gout (Grave Digger's Socks + Servings of Fruit Cocktail). Generic wheel/index mapping does not verify this Mac floppy copy; Mac floppy/CD boot paths differ. One isolated run logged 324 ms and 442 ms host scheduling gaps. No gameplay yet. Previous lookup run: 16.45 MHz median guest clock (15.98–17.1), 54–60.9 presenter FPS, 46.2–49k queued audio frames/sec. Correct Mac wheel mapping, gameplay/audio acceptance, and save/reload remain open |
| Castle of Dr. Brain | 68k; 256 colors; saves in game directory | Puzzles, palette/input/audio, local save/reload | Creates resource-defined windows and paints its 384 by 198 startup picture; then stops at Alert 143, “Can't load !??”; no gameplay |
| Classic QuickTime | Multiple 68k/PPC installers in compressed image | Open the actual guest player and play a video with sound, seeking, and correct timing | Required; not run in our runtime |
| MacSurf | Correct identity/link needed | Selected browser's rendering and network flows | Identity pending |
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

- [ ] B01 Define Chromium as a host-native, isolated companion integrated with
  OS Pi's desktop. Building current Chromium as a classic 68k application is a
  separate port, not an ordinary compilation option.
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
- [ ] A03 Audit host filesystem/network containment and guest isolation. CPU
  emulation itself is not a security sandbox.
- [ ] A04 Produce dependency/provenance manifests and package only permitted
  components. Verify installation, upgrade, rollback, and save preservation.
- [ ] A05 Capture real single-window evidence with dated italic captions when
  publishing. Keep third-party program screenshots out of Git per project rules.
- [ ] A06 Require all selected target, acceleration, browser, QuickTime, networking, and
  security gates plus Ian's acceptance before declaring project success.
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
