> Security integration worktree, 6 November 2026: based on `browser-embed`
> at `8d6a6f4`. Earlier screenshots and performance figures below belong to
> that branch and are not acceptance evidence for this modified build.
> This build enforces HTTPS and certificate validation in both CEF clients,
> sends per-tab TLS status to OS999, and preserves the version guard. The
> `Verified HTTPS` status comes from Chromium SSL state, never the URL spelling.
> Default builds disable scripted keyboard injection, autoplay/quality hooks,
> and the outdated-version override. Developer builds must explicitly set
> `OS999_ENABLE_TEST_HOOKS=ON` in CMake and `OS999_ENABLE_TEST_HOOKS=1` for make.
> The integration uses separate Security profiles and the existing application signing identity.
>
> Run `BUILDFLEET_LOCAL=1 python3 scripts/ralph-network-loop.py --browser --passes 2`
> after configuring CEF. The embedded security suite uses the normal IPC socket,
> disposable profiles and native certificate verification; it never activates
> a window, injects keyboard events, or changes system trust. It covers denied
> plaintext/local-file/credential URLs, invalid certificates, trusted HTTPS,
> independent tabs, trust reset/recovery and shutdown. Transport coverage is
> separate: `make test-network` exercises the Garden download API.
>
> The official CEF index was checked on 6 November: 156.0.8078.80 remains the
> newest published build for macOS arm64 and Linux arm64/x64. No 157+ binary
> was available to upgrade to. Google's served releases still include this
> macOS version; Linux's served version is 157.0.8103.38, so the existing
> unsupported-version warning remains relevant. Do not disable the guard.
>
> The first macOS run passes all 16 embedded security checks and all 29
> transport cases; the desktop suite and version/launch-policy tests also pass.
> Final repeated-run results and hashes belong to `build/ralph-network/`.
> New visual, keyboard, media-duration, Linux GPU/decode and real Pi acceptance
> are not claimed. Certificate import and native client-identity selection
> remain unverified. Release signing/notarization and a full sandbox audit
> remain open; rejecting unsafe switches alone does not complete B03.

# OS999 Browser

OS999 Browser is real Chromium, embedded with CEF 156.0.15 (Chromium
156.0.8078.80), inside OS999's classic chrome. It has a Platinum striped title
bar with close, zoom and WindowShade boxes, back/forward/reload/home buttons,
an address field with an HTTPS lock, and a status bar with a live frame-rate
readout and a grow box. Everything is drawn here from scratch; there are no
Apple assets. The title face is the public-domain ChicagoFLF from `../fonts`.

This is the B01/B02 host-native track from `docs/DEVELOPMENT-CHECKLIST.md`.
It runs as its own app. It is **not yet embedded in the OS999 desktop
window**; see "Desktop integration" below.

## Build and run (Apple Silicon Mac)

```bash
sh browser/scripts/build.sh      # fetches pinned CEF (SHA-1 checked), cmake + ninja
open build/browser/Release/OS999Browser.app
```

Needs Xcode, `cmake`, and `ninja` (`brew install cmake ninja`). CEF goes into
the ignored `build/deps/cef` (~1 GB unpacked); the app is ~330 MB. The
profile lives in `~/Library/Application Support/OS999 Browser`.

Measurement switches (test runs only):

| Switch | Effect |
|---|---|
| `--url=URL` | first page (default youtube.com) |
| `--os999-test` | allows audible autoplay for scripted runs; normal launches keep Chromium policy |
| `--os999-quality=hd2160,hd1440,...` | forces YouTube quality; with `--os999-quality-step=N` cycles every N s |
| `--os999-stats-log=FILE` | per-second GPU frame rate plus the page's own `<video>` decoded/dropped counts and codec |
| `--os999-report-at=N` | after N s, opens chrome://media-internals and logs the decoder actually selected |
| `--os999-exit-after=N` | quits after N s |
| `--os999-size=WxH`, `--os999-fps=N` | window size, Chromium frame-rate cap (60) |

## How the GPU path works

1. The browser is **windowless (OSR)** with `shared_texture_enabled`, so
   Chromium's GPU process renders each frame into an IOSurface. There is no CPU
   pixel buffer: `OnPaint` is ignored on purpose, so a broken GPU path cannot
   silently fall back to software.
2. `OS999Compositor` (`src/browser_compositor.mm`) blits that IOSurface on
   the GPU, using Metal, into a ring of three IOSurfaces that OS999 owns.
   CEF recycles its surface when the callback returns.
3. Core Animation presents the owned surface directly as layer contents.
   These owned surfaces are what the OS999 desktop will import in the next
   phase.
4. Video is decoded by Chromium's media pipeline, which picks Apple
   VideoToolbox (hardware) where the codec allows. Audio goes from Chromium's
   audio service to CoreAudio.
5. The Chromium sandbox is mandatory: the build fails with `#error` when
   `CEF_USE_SANDBOX` is off. No `--no-sandbox`, `--disable-gpu`, or
   certificate bypass switches are used.

## Evidence, 5 November 2026

Host: Apple M4 Max, macOS 26.6.2, Apple clang 21.0.0, cmake 4.4.5. Tree:
`5f18aa8`, dirty, with the `browser/` folder untracked. Binary
`OS999Browser.app/Contents/MacOS/OS999Browser` SHA-256
`e558d31dd271f7426f3e648268aa3edbbc8fc21b33cbe1d57971110f1f01377e`.
Video: `https://www.youtube.com/watch?v=LXb3EKWsInQ` (a 4K60 HDR-labelled
title, served SDR BT.709 here). Logs are in `build/browser-logs/`
(`yt-1080.log`, `yt-sweep.log`, `yt-decoder2.log`).

| YouTube quality | Codec (itag) | Decoded rate | Dropped (steady state) | OS999 GPU frames |
|---|---|---|---|---|
| 2160p60 | VP9 (315) | 60 fps | 0 over ~35 s and ~30 s runs | 60 fps, 0.3–0.5 ms copy |
| 1440p60 | VP9 (308) | 60 fps | 0 over ~20 s | 60 fps |
| 1080p60 | VP9 (303) | 60 fps | 0 over ~20 s | 60 fps |
| 720p60 | VP9 (302) | 60 fps | 0 over ~20 s | 60 fps |
| 480p | VP9 (244) | 30 fps (the only rate YouTube serves here) | 0 | 30–42 fps |
| 1080p ads | AV1 (399) | 30 fps | — | 30 fps |

* **Hardware decode:** chrome://media-internals shows
  `kVideoDecoderName: "VideoToolboxVideoDecoder"`, `kIsPlatformVideoDecoder:
  true`, "Selected VideoToolboxVideoDecoder ... codec: vp9 ... coded size
  [3840,2160]". Audio: `OpusAudioDecoder` (software, as normal for Opus).
* **Sound:** `video.muted=false` with volume 0.56–1.0 in every run. Ian heard
  the sound and reported it as clear.
* **Input:** Ian clicked YouTube's Skip Ad button, and the log shows that ad
  ending early.
* **Startup:** 6–29 frames dropped in the first seconds of each run, while
  the scripted quality switch happens. None dropped afterwards.

### Not yet proven

These runs do not close any checklist gate on their own.

* B08's proposed baseline is 10 minutes per host; these runs are 1–3 minutes.
  Search, seeking, captions, fullscreen and windowed return were not
  exercised. Fullscreen is implemented (Esc exits) but untested.
* B09 still needs host CPU/GPU load, frame-pacing and A/V-sync measurements.
  AV1 hardware decode was not checked; only the VP9 stream was.
* B03: the sandbox is compiled in, but process separation has not been
  audited. The build is ad-hoc signed, so Chromium logs a
  `process_requirement` signature warning.
* There is no IME/composition input (plain typing only), and no downloads,
  file pickers, drag and drop, or multiple tabs. Popups open in the same
  window.
* macOS arm64 only. Linux and Raspberry Pi need the CEF dmabuf
  shared-texture path and a GL/Vulkan importer in the desktop.
* B07 (Thurrott.com) has not been tested.

## Inside the OS999 desktop (`browser-embed` branch)

Open it from 999 > Chromium Browser (Cmd-B). OS999 draws everything around
the page: Platinum title bar with close box, a tab strip (close box per tab,
"+"), back/forward/reload/home, an editable address field, a status bar with
the frame rate OS999 presents, and a grow box. Chromium supplies only pages.

| Feature | How it works |
|---|---|
| Windows | Up to 4 browser windows, stacked with OS999's z-order. Each window has its own layer, masked wherever another window or an open menu is in front. |
| Tabs | Up to 8 per window. Hidden tabs are paused with `WasHidden`, and only the active tab sends frames. `target=_blank` links open tabs; `window.open` popups open windows. |
| Native dropdowns | Chromium's `<select>` popup widget is GPU-composited into the page surface. |
| Fullscreen | Page fullscreen (for example YouTube's button) fills the whole OS999 screen. Esc returns. |
| Downloads | Saved only to the volume's `Downloads` folder (checklist B04), with safe file names and no overwrites; progress shows in the status bar. |
| IME | SDL composition events become `ImeSetComposition`/`ImeCommitText`, and the caret rect is fed back to `SDL_SetTextInputRect`. |
| Keys | Cmd-T/W new/close tab, Cmd-N/B new window, Cmd-L address, Cmd-R reload, Cmd-[ ] back/forward, Ctrl-Tab and Cmd-Shift-[ ] switch tabs, Cmd-C/V/X/A/Z editing. |

One helper process (`OS999Browser --os999-embed-fd=3`) serves every window and
tab. The protocol is fixed-size records (`include/os999_browser_ipc.h`).
Helper code is in `browser/src/embed/`; desktop code is in `src/browser_*.c`.

### Frames per platform

* **macOS:** GPU zero-copy. Each frame is composed (page plus popup) into
  helper-owned IOSurfaces with Metal and sent as a Mach port. OS999 hands it
  to Core Animation directly.
* **Linux / Raspberry Pi:** CPU path. The helper renders off-screen
  (`--ozone-platform=headless`, so it needs no X/Wayland connection of its
  own) and publishes frames into one shared-memory surface per window (the
  same `runtime_bridge` the classic runtime uses). OS999 copies them into its
  framebuffer at 1:1 logical pixels. GPU zero-copy on Linux (dmabuf import)
  and Pi hardware video decode are not done yet.

### Build

```bash
sh browser/scripts/build.sh                       # macOS helper (signed, see below)
BUILDFLEET_LOCAL=1 make app APP_BUNDLE='build/OS999 Browser Embed.app'
sh browser/scripts/linux-container.sh linuxarm64  # Pi / arm64 Linux -> build/linux-arm64/
sh browser/scripts/linux-container.sh linux64     # x86_64 Linux -> build/linux-x86_64/
```

On a Pi or other Linux box: copy `build/linux-<arch>/ospi` and the
`os999-browser/` folder next to each other, install the runtime libraries
listed in `browser/scripts/linux-inside.sh`, and run `./ospi`. Run as a
normal user: Chromium's sandbox stays on and needs unprivileged user
namespaces (the default on Raspberry Pi OS and Debian 12).

**macOS keychain and signing.** Chromium encrypts cookies with a key in the
login keychain ("Chromium Safe Storage"). A binary with a new code signature
must be allowed to read that key, so macOS shows a password prompt, and every
network load hangs until it is answered. Local files still load. Ad-hoc
signatures change on every build, so `build.sh` signs with the first "Apple
Development" identity it finds (override with `OS999_SIGN_IDENTITY`). Answer
the prompt once with **Always Allow** and later rebuilds stay allowed. The
password is the user's to enter; automation must never type it.

### Supported-version check (checklist B06)

`browser/cef-pin.txt` pins the CEF build and its release date; `fetch-cef.sh`
downloads that version and CMake compiles the date in. At startup the browser
process asks Google's public Chrome release list which stable versions are
being served now
(`versionhistory.googleapis.com/v1/chrome/platforms/<mac_arm64|mac|linux>/channels/stable/versions/all/releases?filter=endtime=none`).
The request carries no cookies or user data. Rules (`src/version_policy.cc`,
tested by `version_policy_test`, which `build.sh` runs):

- Our exact version still served, or newer than stable: current, no notice.
- Same major, newer patch served: "Security update available" in the status
  bar for 30 seconds; pages keep loading.
- Our major no longer served: "no longer supported". Pages are blocked when we
  are two or more majors behind stable, or when a newer major has been served
  for more than 21 days (time for CEF to catch up). The API's `startTime`
  restarts with each rollout step, so the majors rule does most of the work.
- No answer (offline): allowed until the pinned build is 60 days old, then
  blocked.

Blocked means every http/https navigation shows a local page explaining why,
including a page that started loading before the answer arrived; local files
still open. OS999 shows a blocking verdict in the status bar for as long as it
lasts. `OS999_ALLOW_OUTDATED=1` overrides blocking for development only.

Checked 6 Nov 2026 (Chrome stable 158.0.8127.18, 156.0.8078.80 still served on
mac_arm64): the real build reports current. A build patched to claim
Chromium 140 (not committed) blocked example.com inside OS999 and standalone.

### Test hooks (measurement only)

`OSPI_BROWSER_URL` opens a window at start. `OSPI_BROWSER_TEST=1` allows
scripted autoplay. `OSPI_BROWSER_LOG` writes helper stats and per-tab
lifecycle, and prints the frames OS999 presents to stderr.
`OSPI_BROWSER_SCRIPT="ms:click:x,y;ms:drag:x1,y1,x2,y2;ms:key:Down;ms:cmd:T;ms:text:abc;ms:ime:abc"`
injects real SDL events into OS999's queue, so automated checks use the same
routing as a person. Feature fixtures: `browser/scripts/smoke.html` and the
feature page described in the evidence below.

### Evidence, 5–6 November 2026

**macOS inside OS999** (M4 Max, macOS 26.6.2; `make test` passes):

* YouTube 4K60 (AV1, 3324×2160) decoded at 60 fps, 2 drops in ~65 s; OS999
  presented 58–60 fps.
* Feature page, driven through OS999's event routing:
  * A native `<select>` list rendered composited; choosing Down + Return
    selected "Second option".
  * A `data:` download was saved as `Volume/Downloads/os999-test.txt`.
  * A `target=_blank` link opened a second tab, and switching tabs worked.
  * Typing plus an IME composition ("nihon" → "日本") produced
    "hello 日本".
  * Page fullscreen covered the whole OS999 screen, and Esc restored the
    window.
  * `window.open` opened a second OS999 browser window.
  * Cmd-T opened a tab with the address field focused.

**Thurrott.com inside OS999 (checklist B07), 6 November 2026, 19:01–19:06 CST**
(macOS, CEF 156.0.15, driven by real SDL events through OS999's routing):

* Homepage `https://www.thurrott.com/` rendered with its header, search, the
  Recent Stories list, thumbnails, authors and dates, and the Premium labels.
* Clicking a story opened
  `https://www.thurrott.com/windows/windows-11/343608/microsoft-releases-five-windows-insider-preview-builds`.
  PageDown scrolled it. Its five-item build list, links, body text, image ads
  and a video ad slot all rendered (the list sits below the ad slot).
* Back returned to the homepage. Clicking a second story opened
  `https://www.thurrott.com/a-i/343596/uber-only-had-four-rides-booked-by-ai-agents-last-week`.
  Back and Forward moved between them.
* The site's own menu opened from a click.
* Dragging the grow box shrank the window. The page reflowed and the GPU
  surface followed (1852x1238 to 1316x894 px).
* Ad servers `us-u.openx.net` and `ads.us.criteo.com` failed with
  ERR_SSL_PROTOCOL_ERROR, and `*.fb.html-load.com` requests were aborted.
  This was not investigated; it is probably network-level filtering.
* No login or paywalled content was used. "Permitted embedded media" was
  tested only through the page's ads, not an article video embed.

**YouTube inside OS999 (checklist B08), 6 November 2026, 19:07–19:33 CST**
(macOS 26.6.2, M4 Max, home network, CEF 156.0.15, events through OS999's
routing, OS999 frontmost):

* Search: a click in YouTube's search box, typing "big buck bunny 60fps 4k"
  and Return opened the results. Clicking the Blender result opened
  `https://www.youtube.com/watch?v=aqz-KE-bpKQ`.
* Controls, from the per-second `<video>` log: k paused (`paused:true`) and
  resumed, l seeked +10 s, and m muted and unmuted. f filled the whole OS999
  screen, and Esc returned to the window. Clicking the player's CC button on
  `https://www.youtube.com/watch?v=jNQXAC9IVRw` enabled English captions
  (the Big Buck Bunny upload has no caption track). Volume was tested only as
  mute/unmute, not as a level change.
* Auto quality, fullscreen: YouTube chose 720p60 (AV1 `av01.0.08M.08`,
  itag 398). 586 s fullscreen, 35,156 frames decoded, 0 dropped.
* **1080p60 baseline**: quality set to hd1080 (`OSPI_BROWSER_QUALITY`, as with
  the gear menu), fullscreen. AV1 1920x1080 `av01.0.09M.08` (itag 399) with
  Opus audio (251). 633.9 s of playback, 38,035 frames decoded (60.0 fps),
  **1 dropped frame**.
* Not covered here: A/V sync measurement, Linux hosts, and network conditions
  beyond "home broadband, unmeasured".

**Acceleration, AV1 1080p60 (checklist B09, partial), 6 November 2026 19:34**,
standalone helper (same CEF build and GPU path), same video at hd1080:

* chrome://media-internals: "Selected VideoToolboxVideoDecoder for video
  decoding, config: codec: av1, profile: av1 profile main, coded size:
  [1920,1080]", `kVideoDecoderName: "VideoToolboxVideoDecoder"`,
  `kIsPlatformVideoDecoder: true`. AV1 main 8-bit 1080p is hardware-decoded
  on this M4 Max. VP9 4K was shown the same way earlier. This proves nothing
  for other codecs, profiles or resolutions.
* Decode rate: 59.9–60.1 fps, 0 dropped frames in the sampled window.
* Load, four `ps` samples 5 s apart: GPU process 10.6–12.6% of one core,
  renderer 4.8–6.2%, browser 4.1–4.6%, network/audio/utility ~0%.
  IOAccelerator "Device Utilization" 33–36%, "Renderer Utilization" 30–32%.
  The latter covers the whole Mac, including other apps.
* Still missing for B09: frame-pacing distribution, A/V sync, a powermetrics
  GPU/media-engine reading (needs admin), and the same proof inside OS999's
  embedded path (it uses the same decoder; not yet captured there).

**Linux arm64 (Raspberry Pi class)** in Debian 12 under Apple `container`:
`make test` passes. ospi and OS999Browser build, and under Xvfb a
Chromium page renders inside the OS999 desktop's browser window, with the
sandbox on, running as a normal user (`build/linux-arm64/smoke.png`). This is
a functional smoke test, not a performance test, and not on real Pi hardware.

**Linux x86_64** in Debian 12 amd64 (Rosetta) under Apple `container`: the
same results. `make test` passes, both programs build, and the page renders
inside OS999 (`build/linux-x86_64/smoke.png`).

Not yet proven: a Pi 4/5 run on real hardware, Linux GPU zero-copy and
hardware decode, IME with a real input method (only synthetic composition
events were tested), and 10-minute playback runs (B08).

### Web games and installed web apps

WebGL 1/2 and JavaScript are enabled in both browser clients. WebAssembly,
service workers, persistent site storage and the native Chromium Gamepad API
are available. Graphics still obey Chromium's GPU blocklist and sandbox;
controller discovery and mappings depend on the host and controller.

Open `https://os999.invalid/play/` for the bundled Controller Arena. Click Play,
then use the left stick and A/Cross, or arrows and Space. It renders with WebGL,
uses an original WASM scoring module, and caches its assets with a service
worker. Focus loss pauses this game. OS999 grants native browser focus only to
the active tab while the desktop has input focus. Other games control their
own pause behavior. Connect a controller through the host OS first.

The **A** toolbar button / Cmd-Shift-A opens the app library. **+** /
Cmd-Shift-I installs the current page's manifest shortcut; Cmd-Shift-U removes
it from its start page without deleting site data. The standalone browser has
matching File menu items. Install requires a verified HTTPS page, a named
manifest and same-origin HTTPS identity/start URL. Installed apps reopen in
OS999 browser windows and persist within that browser profile. This is an
OS999 app library, not native macOS app packaging; manifest display modes,
OS-level launch integration and push notification delivery are not implemented.
Offline operation depends on each app's service worker.

The reserved `os999.invalid` origin serves only compiled OS999 resources; it
never exposes host files. Its built-in pages do not represent a TLS-verified
remote website. Bundled game cache versions change with resource content.

`python3 tests/test_web_games.py` uses ordinary helper IPC without activating
OS999 or injecting OS input. It checks pixel readback from WebGL 1/2, actual
WASM execution, a cache-only service-worker response, focus ownership, app
installation, restart persistence, removal and rejection on an unverified page.
The Linux invocation takes the helper executable as its first argument; it
reports GPU capabilities without requiring hardware acceleration. Physical
controller gameplay, vibration, third-party game compatibility, real Pi GPU
support and performance remain separate acceptance tests.
