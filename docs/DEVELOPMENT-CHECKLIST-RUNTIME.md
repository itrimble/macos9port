# Runtime and release acceptance

Part of the [development checklist](DEVELOPMENT-CHECKLIST.md). Stable requirement IDs and evidence are preserved below.

## 3. Gameplay and acceleration

- [ ] G01 Agree a reproducible gameplay route for each game: startup, menus,
  scene changes, interactions, audio, save, quit, relaunch, and restore.
- [ ] G02 Run original Macintosh executables through our runtime. ScummVM,
  ports, remakes, and external emulators do not satisfy this gate. 7 Nov: four Glider-family apps run headless to a halt, none to gameplay; see [record](RUNTIME-LOOP-2026-11-07.md).
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
| Castle of Dr. Brain | 68k; 256 colors; saves in game directory | Puzzles, palette/input/audio, local save/reload | Headless (8 Nov 2026): startup completes; the intro plays unattended for 100 s with no halt (logo, castle, front door); audio capture non-silent ~20-85 s (not listened to); scripted clicks operate its skip-intro dialog, the front-door room and the File menu (Restore Game? opens its file dialog) the Options menu (Small/Medium/Large Window work; Hide Desktop blacks out the desktop; Show Desktop not reached), Quit and save/restore of a start-of-room game; movement and real play unverified |
| Classic QuickTime | Multiple 68k/PPC installers in compressed image | Open the actual guest player and play a video with sound, seeking, and correct timing | Required; not run in our runtime |
| MacSurf | Correct identity/link needed; supplied Garden link repeats Castle of Dr. Brain | Selected browser's rendering and network flows | Identity pending; do not silently substitute another app |
| Chromium | Modern host-native browser track | Sandboxed browsing, GPU, updates, network/security tests | Not built or integrated |
| Thurrott.com | User-selected live website | Browse articles, navigate, scroll, and display media correctly in the integrated browser | Scripted load of the home page and one article succeeded in the embedded browser (12 Nov 2026, [record](DEMO-THURROTT.md)); human browsing, scrolling and media not tested |
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
  *Note, 12 November 2026 (no box ticked):* in a scripted run on Mac17,5 / macOS 27.2 / CEF
  156.0.26, OS999's own embedded Web Browser loaded `https://thurrott.com` (final URL
  `https://www.thurrott.com/`, HTTP 200, title "Home - Thurrott.com", TLS verified, no certificate
  error) and then one article URL (HTTP 200), with OS999 as the only window of the run
  ([record](DEMO-THURROTT.md)). Not shown: a person reading it, scrolling, links, back/forward,
  resize, menus, images judged by eye, embedded media, consent dialogs; 10 to 13 sub-resources per
  page failed (ad and tracker hosts plus one thurrott.com request) and were not investigated.
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
  *Note, 12 November 2026 (no box ticked):* the standalone helper on Mac17,5 / macOS 27.2 /
  CEF 156.0.26 reported `VideoToolboxVideoDecoder` with `kIsPlatformVideoDecoder true` for VP9 and
  AV1 at 1920x1080, and chrome://gpu `Video Decode: Hardware accelerated`
  ([record](DEMO-YOUTUBE-HWDECODE.md)). Not shown: playback inside the OS999 desktop window, GPU
  composition into the desktop, audio heard, H.264, load/sync figures, other hosts.

### Classic QuickTime playback

OS999 has a host movie window for uncompressed 32-bit raw video (`raw `, up to
640 by 480). Double-click a `.mov`, `.qt`, `.mp4`, or `.m4v` file to play it.
Click the picture or press Space to pause and resume. The player rejects a
truncated header, a sample that sits outside the media block, and a movie it
cannot draw. Guest QuickTime is still required for Q01–Q04. This window does
not decode Cinepak, rpza, Sorenson, or MPEG, and it does not claim hardware
video decoding.
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

Networking worktree checkpoint (5 November 2026): `codex/modern-networking`
adds host libcurl HTTPS downloads with mandatory chain/hostname verification,
TLS 1.2 minimum, explicit per-hop destination policy, limits, cancellation,
atomic files, host CA configuration and opt-in PEM client identities. Garden is
wired to this path. The isolated CEF source snapshot rejects certificate
exceptions and unsafe launch flags and derives its lock from SSL status.
Run `scripts/ralph-network-loop.py --browser --passes 2`; its source manifests,
commands, exit results and artifact hashes live under `build/ralph-network/`.
These are partial N01/N02/N03/N08 implementations; keep the broad gates open.
There is no guest socket adapter, legacy gateway, proxy configuration, inbound
listener, Linux/Pi proof, embedded browser acceptance, or exhaustive revocation
coverage. The full desktop suite and synthetic transport tests passed during
implementation; current browser results belong to the loop receipts, not the
older browser README's performance evidence. The resumed browser runtime gate
passes nine cases: unsafe launch switches, invalid certificate refusal, plaintext
refusal, and trusted HTTPS with verified connection state. A real window renders
example.com with the connection lock. The Certificates menu explains policy;
CEF's certificate-manager page did not open, so import and native machine-client
identity selection remain unverified. Earlier Keychain waits no longer reproduce;
the external resolution was not observed and no system trust was changed.
The host transport passes 29 cases, including rejected partial responses,
cancellation during flush, and missing/untrusted client identities, with a clean
sanitizer run. Canonical root/cache paths also cover symlinked test profiles.
The final two-pass Ralph receipt binds these checks to the source snapshot.

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

### Startup recovery checkpoint (8 November 2026)

Preserved the interrupted producer's stronger progress-edge regression in an
isolated checkout; 183 bar checks plus 61 startup checks pass without renderer
changes. [Recovery scope and evidence](BOOT-RECOVERY-2026-11-08.md). No fresh GUI
acceptance is claimed and the original running app was left untouched.
### Bluetooth recovery checkpoint (8 November 2026)

The isolated recovery adds the desktop PAN service/manager seam, cancellation,
and safe detach; [details and remaining protocol blocker](BLUETOOTH-RECOVERY-2026-11-08.md).
The existing BNEP simulator format is not standard wire interoperability.
Real-radio UI, classic runtime hooks, and hardware acceptance remain pending.

Bluetooth follow-up: standard setup/control UUID framing and host-channel
lifetime are corrected with independent wire vectors. macOS wrapper completion,
pipe initialization and cancellation are compile/contract checked. See
[wire recovery evidence and explicit unsupported capabilities](BLUETOOTH-WIRE-2026-11-08.md).
No physical peer, desktop radio wiring or classic runtime hook acceptance yet.

Bluetooth driver follow-up: production source manifest now links the macOS
backend; 31 fake-IOBluetooth-boundary checks cover real driver negotiation and
teardown. Combined desktop lifecycle hooks are tested; menu/panel hooks remain pending. No physical
peer acceptance is claimed. [Driver handoff](BLUETOOTH-DESKTOP-DRIVER-2026-11-08.md).

## Control Panels integration checkpoint

The next candidate combines all eight producer panels with Build10's startup,
Bluetooth lifecycle, host automation, movie player, storage and Quake work.
The original producer worktree and Build9/Build10 artifacts remain unchanged.

### Safety corrections

- RAM Disk cleanup requires an ownership marker bound to the directory inode.
  Existing unowned directories are refused; the panel reports unavailable and
  preserves their contents. Metadata refuses symlinks, hard links and FIFOs.
- Finder quotas include resource forks and sidecars, fail closed on errors and
  avoid charging same-disk moves twice.
- Native guest launch reads validated typed preferences before forking; unsafe
  settings files cannot block on FIFOs or silently control launch policy.
- Guest heap fallback preserves its stack reserve. Handle resize and relocation
  check redzones before moving or replacing their guards.
- Menu mute updates the panel cache and saved preferences, so reopening the
  Sound panel or restarting cannot restore an obsolete mute setting.
- Movie/session owners are destroyed before the shared audio service. The
  production cleanup helper has a real decoded-audio lifecycle regression test.

### Scope boundaries

Network HTTPS policy, redirect and timeout controls have real host consumers.
Guest isolation and host-addressing controls are visibly unavailable because the
runtime/broker enforcement and host address configuration are not connected.
Tailscale selection is a preference, not proof of a connected mesh. Offline
applies to the integrated host download/browser launch policy, not a system-wide
radio or socket kill switch. Bluetooth radio use remains separately unverified.

The RAM Disk is an owned, quota-limited scratch directory on the host filesystem;
it is not a RAM-backed mount. Panel memory bars are configured sizes, not measured
host allocation. Guest guard checks are software redzones, not OS guard pages.

Focused and full combined validation results belong to the numbered build receipt.
Source tests do not establish visual, audible, real-radio, guest ABI, mobile-device
or classic-application acceptance.

### R2 interface sound policy

The R2 candidate starts new volumes with interface effects and startup chimes off.
Enabling effects uses a conservative 20% system-bus default; existing explicit
volume and enable preferences are preserved. Interface toggles no longer change
the shared media/app master mute. Mobile keyboard feedback is muted before the
first event unless the volume explicitly enables interface sounds. Unknown or
malformed legacy preferences stay silent.

The design follows Apple's [Explore immersive sound design, WWDC23](https://developer.apple.com/videos/play/wwdc2023/10271/):
frequently repeated UI sounds should remain subtle and provide useful feedback.
The 20% gain is a chosen default, not a measured universal loudness guarantee.
Dummy-output PCM tests are separate from listening and device acceptance.
