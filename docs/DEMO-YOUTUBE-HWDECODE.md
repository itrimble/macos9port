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

**Not shown:** playback inside the OS999 desktop window, H.264, 4K, audio actually heard, A/V sync,
CPU/GPU load, a ten-minute run, and any other Mac. See "What is NOT shown".

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

## What is NOT shown

* Playback inside the OS999 desktop window (the embedded helper, `--os999-embed-fd`). The
  standalone helper window was used. Diagnostics only run in the standalone helper window; the
  embedded host would need its own hook.
* GPU composition into the desktop. The helper's own "GPU frames" figure was seen in the log but
  the OS999 desktop was not run.
* Audio actually heard (the page reported `muted:false`, volume 1; nobody listened).
* H.264. YouTube served VP9 and AV1 only; `chrome://gpu` lists H.264 decode support but no H.264
  stream was decoded.
* 4K or 60 fps at 1080p being drop-free. Run 5 dropped 410 of 7128 frames (5.7%).
* Ten minutes, A/V sync, CPU/GPU load, frame pacing (B08/B09 baseline).
* Another Mac, Linux or Raspberry Pi. No GUI interaction by a person; the run was scripted.
* No screen recording or window capture was made. If one is needed for the record, use SnapDog,
  one window per capture.

## What remains for the full demo

1. Repeat this inside the OS999 desktop window (Cmd-B browser), with diagnostics reachable from the
   embedded helper, driven or watched by a person.
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
