# Demo B evidence: YouTube with verified hardware video decode

Checklist items: B08 and B09 in `DEVELOPMENT-CHECKLIST-RUNTIME.md`. Date: 12 November 2026.
No boxes are ticked by this record.

## Verdict

**Hardware decode verified for the standalone OS999 Browser helper**, for VP9 (profile 0, 1920x1080)
and for AV1 (profile main, 1920x1080, a pre-roll ad stream), on this one host. Verified means
Chromium's own `chrome://media-internals` reported `kVideoDecoderName "VideoToolboxVideoDecoder"`
with `kIsPlatformVideoDecoder true` for the player, and `chrome://gpu` reported
`Video Decode: Hardware accelerated`. Smooth playback, frame rate and GPU compositing were not used
as evidence.

**Also verified inside the OS999 desktop window** (same decoder, VP9 1080p, about 4 minutes): see
"Embedded in the OS999 desktop". Ian confirmed sound and video in the standalone helper window on
12 Nov; nobody has yet listened to or watched the embedded run.

**Not shown:** H.264, 4K, audio heard in the embedded run, A/V sync, CPU/GPU load, a ten-minute
run, and any other Mac. See "What is NOT shown".

## Environment

| Item | Value |
|---|---|
| Mac | `hw.model` Mac17,5; chrome://gpu reports "Apple A18 Pro" |
| macOS | 27.2, build 26B5091g (`sw_vers`) |
| CEF / Chromium | 156.0.26+g8ea0c73+chromium-156.0.8078.88 (pin in `browser/cef-pin.txt`, SHA-1 checked by `fetch-cef.sh`) |
| Source | branch `demo/youtube-hwdecode`, based on main 8950fd6, plus the diagnostics commit |
| Binary | developer build with `OS999_ENABLE_TEST_HOOKS=ON` (needed for `--os999-test` autoplay), `OS999Browser` SHA-256 `ada499aee28ab26cc26c7c83d21d8b7f62f5908b3cee4bc6045a11e11afc7180`, signed with the local Apple Development identity |
| Profile | disposable, inside the worktree `build/diag4/p`; not the user's real OS999 Browser profile |
| Build | `BUILDFLEET_LOCAL=1 sh browser/scripts/build.sh` (default build, tests pass), then `BUILDFLEET_LOCAL=1 cmake --build` of a second tree configured with hooks on |
| Video | `https://www.youtube.com/watch?v=LXb3EKWsInQ` (the same video used in `browser/README.md`), forced to hd1080 |

## Method

1. New opt-in mode, off unless `OS999_DECODE_DIAG=1` is in the environment
   (`browser/src/decode_diag.*`, `decode_diag_host.*`). It does not change sandbox, TLS or keychain
   policy (the existing `use-mock-keychain` is untouched).
2. It adds a `--vmodule` for the decoder source files and `--enable-logging=stderr`.
3. At `--os999-report-at` seconds it opens two extra windows (so the playing window is not navigated
   away): `chrome://media-internals` and `chrome://gpu`. A page script reads their text, the host
   parses it and writes `<profile>/diagnostics/decode-diag.log` (created 0600, path must be inside
   the private profile folder).
4. On quit it writes a verdict line. The verdict is "verified" only if a player has a platform
   decoder with a hardware decoder name and a coded size.
5. Page-side `<video>` statistics (`--os999-stats-log`) give resolution, codec string and dropped
   frames. These are context, not proof.

Command (run 5):

```
OS999_DECODE_DIAG=1 OS999Browser --url="https://www.youtube.com/watch?v=LXb3EKWsInQ" --os999-test \
  --os999-quality=hd1080 --os999-stats-log=... --os999-profile=<disposable> \
  --os999-report-at=105 --os999-exit-after=125 --os999-size=1280x720
```

## Raw diagnostic excerpts

From `decode-diag.log`, run 5 (18:29 local), VP9 main content:

```
player decoder=VideoToolboxVideoDecoder kIsPlatformVideoDecoder=1 codec=vp9 coded=1920x1080 hardware=true |
  Selected VideoToolboxVideoDecoder for video decoding, config: codec: vp9, profile: vp9 profile0,
  level: not available, alpha_mode: is_opaque, coded size: [1920,1080], visible rect: [0,0,1920,1080],
  natural size: [1920,1080], ... color space: {primaries:BT709, transfer:BT709, matrix:BT709, range:LIMITED}
raw media-internals rows:  kVideoDecoderName "VideoToolboxVideoDecoder" ... kIsPlatformVideoDecoder
verdict verified: VideoToolboxVideoDecoder (platform decoder) codec vp9 1920x1080
```

From run 4 (18:26), pre-roll ad stream, same decoder:

```
player decoder=VideoToolboxVideoDecoder kIsPlatformVideoDecoder=1 codec=av1 coded=1920x1080 hardware=true |
  Selected VideoToolboxVideoDecoder for video decoding, config: codec: av1, profile: av1 profile main, ...
```

`chrome://gpu` rows (identical in every run):

```
Canvas: Hardware accelerated          Compositing: Hardware accelerated
Rasterization: Hardware accelerated   Video Decode: Hardware accelerated
Video Encode: Hardware accelerated    WebGL: Hardware accelerated
WebGPU: Hardware accelerated          Direct Rendering Display Compositor: Disabled
Raw Draw: Disabled                    WebNN: Disabled
```

Video Acceleration Information (decode): h264 baseline/main/extended/high 64x64 to 4096x4096;
vp9 profile0 and profile2 64x64 to 4096x4096; av1 profile main 64x64 to 8192x8192.
Skia backend GraphiteDawnMetal; tile update mode "Zero-copy".

Page-side playback context, run 5 (`stats.log`, VP9 itag 303, 1920x1080, `muted:false`, volume 1):

| Page time | Decoded frames | Dropped |
|---|---|---|
| 25.8 s | 1549 | 209 |
| 55.8 s | 3347 | 331 |
| 85.8 s | 5146 | 401 |
| 118.9 s | 7128 | 410 |

About 119 s of content played; the decoded rate was about 60 fps. Most drops (209) occurred in the
first 26 s, roughly 200 over the last 90 s. Earlier runs had pre-roll ads (AV1) first, which is why
the run 4 verdict line names AV1; run 5 had no ad.

The full logs are in the worktree `build/diag4/p/diagnostics/decode-diag.log` and
`build/diag5/stats.log` (not committed).

## What did not work

* Chromium VLOG route: with `--vmodule` for the decoder files and `--enable-logging=stderr`, no
  decoder-selection VLOG lines appeared in the profile's `chromium.log` or in stderr. The first run
  (log file only) also had none. Sandboxed child processes may not be able to open the log file; I
  did not relax the sandbox to find out. The parser for those lines exists
  (`ExtractDecoderLines`) but the run produced no input for it, so it is untested against real data.
* The `--os999-exit-after` quit ends with Chromium's "Teardown watchdog expired; recording dump and
  terminating" and exit status 2 in every run. The verdict is therefore written when quit begins
  (before teardown), not after `CefShutdown`. I did not investigate the watchdog.
* The `chrome://gpu` log repeats each second while the page text changes slightly, and one parsed
  row is junk (`Vulkan: Information ===`). Cosmetic.
* The per-player click in the media-internals scraper shows the same page for every player in this
  Chromium, so the parser sees the decoder of whichever player is selected, not necessarily every
  player. Runs 4 and 5 each surfaced distinct decoders over time as players came and went.

## Embedded in the OS999 desktop (12 November 2026, evening)

Background: Ian saw YouTube play with sound and picture in a separate macOS window, outside OS999,
and confirmed sound and video in the standalone helper (12 Nov). That was the standalone helper
runs above, not the Web Browser item. This section asks the other question: does OS999's own
browser draw inside the OS999 window, and is the decoder still hardware there?

### Verdict

**Embedded: yes.** Launching OS999 and opening the browser (auto-open hook, same code path as the
Web Browser item) produced exactly one host window, OS999's own. The Chromium helper ran with no
window at all. Chromium's decoder inside that embedded helper was `VideoToolboxVideoDecoder`
(hardware, VP9 1920x1080) while the page played 1080p for about 4 minutes. I found nothing to fix on
the pop-out question: the embedding, helper launch flags and staging all worked as designed.
What I did add is a diagnostics path that reaches the embedded helper (it had none) and test hooks
to see pixels, because the desktop framebuffer cannot show them (see "Pixel proof" below).

Not verified: the sound and the picture with human senses (nobody listened or watched), and a
SnapDog capture (it failed, see below).

### Method

* Build: `BUILDFLEET_LOCAL=1 OS999_ENABLE_TEST_HOOKS=1 make app APP_BUNDLE="build/OS999 YT.app"`
  (the project's local-build override, not the fleet), plus the helper from this tree with
  `-DOS999_ENABLE_TEST_HOOKS=ON` (needed for `OSPI_BROWSER_TEST` autoplay), signed with the local
  Apple Development identity, placed at `build/browser/Release/OS999Browser.app`.
* Disposable volume: `ospi --volume <tmpdir>`; nothing under the real OS Pi volume or the user's
  real browser profile was touched. The helper's profile is `<volume>/.os999/Browser`.
* Launch (no mouse driving): environment
  `OSPI_BROWSER_URL=https://www.youtube.com/watch?v=LXb3EKWsInQ OSPI_BROWSER_TEST=1
  OSPI_BROWSER_LOG=<file> OSPI_BROWSER_QUALITY=hd1080 OS999_DECODE_DIAG=1
  OS999_DECODE_DIAG_DELAY=60 OSPI_DUMP=<file>.ppm OSPI_DUMP_AT=30000,100000`.
  `OS999_DECODE_DIAG` is simply inherited: the desktop spawns the helper with its own environment
  (`posix_spawn(..., environ)`), and the helper's launch policy rejects unknown argv, so an
  environment variable is the right carrier. Unset, nothing changes.
* New in this tree: `browser/src/embed/embed_diag.{h,mm}`. When `OS999_DECODE_DIAG=1`, the embedded
  helper opens two hidden windowless browsers on `chrome://media-internals` and `chrome://gpu` after
  `OS999_DECODE_DIAG_DELAY` seconds (default 30, 1..3600), scrapes them once a second with the same
  script as the standalone helper, and writes `<profile>/diagnostics/decode-diag.log` (0600, inside
  the private profile folder). The verdict is written when the desktop asks the helper to shut down,
  before teardown. These browsers have no tab id and send nothing to the desktop.
* New test hooks (only in `OS999_ENABLE_TEST_HOOKS` builds): `OSPI_DUMP_AT=ms,ms,...` writes the
  desktop framebuffer at those times as `<OSPI_DUMP>-1.ppm`, `-2.ppm`; with `OSPI_BROWSER_LOG` set,
  the once-a-second "presented" line also samples the IOSurface that OS999 last presented for the
  browser window (a 48x27 grid: distinct colours, mean, min/max).
* Window proof: `CGWindowListCopyWindowInfo` (option all) filtered to owners containing `ospi`,
  `OS999` or `Chromium`, and System Events for the process list.

### Evidence

Windows while the video played (run 3, 19:11 local), the only entry for any OS999 or Chromium owner:

```
pid=50414 owner=OS999 id=1765 layer=0 onscreen=1 title=OS999 bounds=243,71 922x721
```

System Events: `ospi bg=false windows=0` (AX does not list the SDL window; the CG list above does),
`OS999Browser bg=true windows=0`, `OS999Browser Helper bg=true windows=0`. The helper runs with the
accessory activation policy, so it has no Dock icon, menu or window. In a nested bundle the desktop
clones the helper out of the .app (the macOS 27.2 crash fix, `browser_stage.c`); the running helper
was the clone at `<volume>/.os999/Helpers/OS999Browser.app/...`, and it started fine.

Decoder diagnostics from the embedded helper (`<volume>/.os999/Browser/diagnostics/decode-diag.log`,
run 3):

```
19:09:19 start cef 156.0.26+g8ea0c73+chromium-156.0.8078.88 vmodule on, pid 50769
19:10:20 player decoder=VideoToolboxVideoDecoder kIsPlatformVideoDecoder=1 codec=vp9 coded=1920x1080 hardware=true | Selected VideoToolboxVideoDecoder for video decoding, config: codec: vp9, profile: vp9 profile0, ... coded size: [1920,1080] ...
19:10:20 player decoder=VideoToolboxVideoDecoder kIsPlatformVideoDecoder=1 codec=av1 coded=1920x1080 hardware=true | Selected VideoToolboxVideoDecoder for video decoding, config: codec: av1, profile: av1 profile main, ... coded size: [1920,1080] ...
19:10:21 gpu-feature Compositing: Hardware accelerated
19:10:21 gpu-feature Video Decode: Hardware accelerated
19:14:39 verdict verified: VideoToolboxVideoDecoder (platform decoder) codec vp9 1920x1080
```

The AV1 line is the pre-roll ad, the VP9 line is the video. Run 2 (earlier the same evening, delay
75 s) gave the identical VP9 line and verdict. The helper exited cleanly each time the desktop was
terminated (verdict line written, no leftover processes).

Page-side statistics from the embedded page (`OSPI_BROWSER_LOG`, run 3, VP9 itag 303, `muted:false`):

| Time | Page time | Decoded frames | Dropped | Presented in OS999 |
|---|---|---|---|---|
| 19:10:42 | 32 s | 1933 | 51 | 59 to 60 fps |
| 19:11:23 | 72 s | 4334 | 51 | 59 to 60 fps |
| 19:12:43 | 152 s | 9141 | 52 | 59 to 60 fps |
| 19:14:03 | 233 s | 13946 | 52 | 60 fps |

About 4 minutes of playback, 1920x1080 at about 60 fps decoded, 52 dropped frames in total (all 51
before the first minute, one more in the next three minutes). The desktop's own counter ("frames OS999
actually received and presented") reached 16397 frames by the last probe line.

Pixel proof. The desktop framebuffer dump is not usable for this on macOS: the browser's frames are
GPU surfaces presented by a Core Animation layer on top of the SDL window, so `frame-2.ppm` (taken
at 100 s) shows the browser chrome and a white content area. I am recording that because it looked
like a failure at first. It is the expected zero-copy path. So the probe reads the presented
IOSurface (1852x1238) directly. Samples from the log, video playing:

```
presented 58 fps, frames 1998,  surface 1852x1238 colors=367 mean=61 range=0..255   (pre-roll ad)
presented 59 fps, frames 9801,  surface 1852x1238 colors=542 mean=85 range=0..255   (about 2 min in)
presented 60 fps, frames 16397, surface 1852x1238 colors=435 mean=88 range=0..255   (about 3 min 40 s in)
```

A blank or frozen surface would show 1 colour. Hundreds of colours and a changing mean at 60 fps
mean content is arriving and being replaced; it does not tell me the picture looks right.

SnapDog capture: not obtained. `open "snapdog://capture?mode=window"` raised SnapDog's window
picker (new layer-1000 panels appeared) but moving and clicking over the OS999 window with `cliclick`
produced no file in `~/Pictures/SnapDog` (the newest file there is still from 10 Nov). I did not
use any other capture tool. I could not tell whether SnapDog lacks Screen Recording permission, or
the synthetic click does not reach its picker. SnapDog (Ian's own instance, running since 11 Nov)
was left with its picker panels showing; they may need an Esc or a SnapDog restart. A caption for
a later capture should read: *OS999 desktop with the Web Browser window playing the Costa Rica 4K
video at 1080p, captured with SnapDog on 12 Nov 2026.*

### What needs Ian

* Listen and watch: build this branch (`make app`, plus `sh browser/scripts/build.sh` for the
  helper; the disposable `OS999 YT.app` used here was deleted), launch it, open Web Browser, load the video, and confirm the sound and the picture are inside the
  OS999 window and in sync. I have not heard or seen it.
* One SnapDog window capture of that window (Screen Recording permission for SnapDog, then a real
  click on the picker).
* Run with `OS999_DECODE_DIAG=1` set in the environment that launches OS999 (for example
  running `Contents/MacOS/ospi` from a shell with the variable exported) if the diagnostics log
  is wanted; it is off by default. A Finder launch does not pass your shell environment.

### What is still not shown (embedded)

H.264, 4K, A/V sync, CPU/GPU load, ten minutes, another Mac, and any behaviour of a signed
release bundle (this was a developer build with test hooks, ad-hoc signed desktop).
Frame counts come from the helper and desktop logs, not from a screen recording.

## What is NOT shown

* ~~Playback inside the OS999 desktop window~~: see the section above (shown, with the limits
  listed there). The standalone-helper results in the rest of this record remain separate.
* Audio actually heard (the page reported `muted:false`, volume 1; nobody listened). Ian
  confirmed sound and video in the standalone helper window on 12 Nov; that is his report, not
  something measured here.
* H.264. YouTube served VP9 and AV1 only; `chrome://gpu` lists H.264 decode support but no H.264
  stream was decoded.
* 4K or 60 fps at 1080p being drop-free. Run 5 dropped 410 of 7128 frames (5.7%).
* Ten minutes, A/V sync, CPU/GPU load, frame pacing (B08/B09 baseline).
* Another Mac, Linux or Raspberry Pi. No GUI interaction by a person; the run was scripted.
* No screen recording or window capture was made. If one is needed for the record, use SnapDog,
  one window per capture.

## What remains for the full demo

1. Done by script on 12 Nov (embedded run above). Still open: the same run watched and listened
   to by a person, with a SnapDog capture of the OS999 window.
2. A human listening check for audio and a visual check of the picture.
3. A ten-minute 1080p60 run with CPU/GPU load and dropped-frame figures (B08/B09).
4. Optionally a codec that exercises H.264, and a run on any other target host.

## Reproduce

```
BUILDFLEET_LOCAL=1 sh browser/scripts/build.sh                      # default build + unit tests
cmake -S browser -B build/browser-hooks -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPROJECT_ARCH=arm64 -DOS999_ENABLE_TEST_HOOKS=ON            # measurement build
```

then sign (`browser/scripts/sign.py`) and run as in "Method". Unit test: `build/browser/.../decode_diag_test`
(`decode diagnostics: ok`).
