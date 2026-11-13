# Demo A evidence: Thurrott.com in the OS999 desktop browser

Checklist item: B07 in `DEVELOPMENT-CHECKLIST-RUNTIME.md`. Date: 12 November 2026.
No boxes are ticked by this record.

## Verdict

**Loaded: yes, twice, inside the OS999 window.** OS999's own embedded Web Browser (the CEF helper
driven by the desktop) loaded `https://thurrott.com`, followed the redirect to
`https://www.thurrott.com/`, and finished the main frame with HTTP 200 and the title
"Home - Thurrott.com". Chromium reported the connection as secure with no certificate error. A second
load of an article from the front page also finished with HTTP 200. The only OS999 window of the run
was the desktop's own; the helper had no window. The surface OS999 presented held hundreds of
colours, not a blank.

**Not a human browsing test.** Nobody looked at the window, scrolled it, or read the page. Every
step was scripted by environment variables. See "What is NOT shown".

## Environment

| Item | Value |
|---|---|
| Mac | `hw.model` Mac17,5 |
| macOS | 27.2, build 26B5091g |
| CEF / Chromium | 156.0.26+g8ea0c73+chromium-156.0.8078.88 (`browser/cef-pin.txt`; the distribution was borrowed read-only from another worktree's `build/deps/cef`, whose `.version` matches the pin; its SHA-1 was not re-checked here) |
| Source | branch `demo/thurrott`, based on `mbp/main` 5e2d4a6, plus the load-diagnostics commit |
| Desktop | `BUILDFLEET_LOCAL=1 OS999_ENABLE_TEST_HOOKS=1 make app APP_BUNDLE="build/OS999 TH.app"` (ad-hoc signed) |
| Helper | `-DOS999_ENABLE_TEST_HOOKS=ON` Release build, `OS999Browser` SHA-256 `e22c19e385c896fce94c1c1101f2881b031b84a31b160b8e45b5aaf493b15a33`, signed with the local Apple Development identity, placed at `build/browser/Release/OS999Browser.app` |
| Volume / profile | fresh disposable volume in the session scratchpad; the helper's profile is its `.os999/Browser`. Nothing under the real OS Pi volume or any real browser profile was touched |
| Network | the Mac's normal home connection; no proxy configured by me |
| Sandbox, TLS, keychain | unchanged. The existing launch policy, `use-mock-keychain` and certificate handling are untouched; `OnCertificateError` still refuses to continue |

## Method

New opt-in code: `browser/src/load_diag.{h,cc}` (pure counters and formatting, unit tested) and hooks
in `browser/src/embed/embed_browser.cc`. It is **off unless `OS999_LOAD_DIAG=1`** is in the
environment that launches OS999 (the desktop spawns the helper with its own environment) and
`OSPI_BROWSER_LOG` names a log file. It only reads what Chromium reports:

* main-frame request start (`OnBeforeBrowse`, redirects do not restart the clock),
* `OnLoadEnd` for the main frame: HTTP status, final URL, title, the navigation entry's SSL status
  (the same `BrowserVerified` rule the status bar uses: https, secure connection, certificate status
  none, normal content), count of `OnCertificateError` callbacks, and the time from request start
  to load end,
* `OnResourceLoadComplete` for every sub-resource: ok, HTTP error, network failure, cancelled,
  bytes received. The first 25 failures per load are named by scheme and host only (no path or query),
* a resource summary 10 s after the load, and a final one when the next navigation starts.

A second test hook, only in `OS999_ENABLE_TEST_HOOKS` builds: `OS999_LOAD_DIAG_NEXT=<https URL>`
makes the helper load that URL once, 4 s after the first main-frame load ends with HTTP 200. The URL
must be https, without credentials, spaces or control characters (`ValidNextUrl`). It is a URL load
from the helper, not a click: no mouse driving. I took the article URL from the front page with
`curl` before the run (`/games/343808/halo-campaign-evolved-to-get-firefight-mode`, the first
article link in the HTML at about 22:00).

Launch (run 2, the one quoted below):

```
OSPI_BROWSER_URL=https://thurrott.com OSPI_BROWSER_TEST=1 OSPI_BROWSER_LOG=<run>/helper.log \
OS999_LOAD_DIAG=1 \
OS999_LOAD_DIAG_NEXT=https://www.thurrott.com/games/343808/halo-campaign-evolved-to-get-firefight-mode \
OSPI_DUMP=<run>/frame.ppm OSPI_DUMP_AT=40000 \
"build/OS999 TH.app/Contents/MacOS/ospi" --volume <run>/vol
```

Window proof: `CGWindowListCopyWindowInfo(.optionAll)` filtered to owner names containing `ospi`, `os999`
or `chromium` (a 15-line Swift program). Pixel proof: the desktop's test-hook probe that samples the
IOSurface OS999 last presented for the browser window (distinct colours of a 48x27 grid, mean, range).

## Raw log excerpts

From the helper log (`OSPI_BROWSER_LOG`), run 2, 22:06 local. Blank continuation is my wrapping.

```
22:06:09 tab 1 created 926x619 visible 1
22:06:10 tab 1 load start
22:06:11 tab 1 first frame
22:06:15 load #1 requested=https://thurrott.com/ final=https://www.thurrott.com/ http=200
         title="Home - Thurrott.com" tls=verified cert_status=0x0 cert_errors=0 net_error=0 load_ms=6275
22:06:20 resources #1 after 10.4 s: total=100 ok=87 http_error=0 failed=13 canceled=0 bytes=8083597 (at next navigation)
22:06:20 tab 1 load start
22:06:22 load #2 requested=https://www.thurrott.com/games/343808/halo-campaign-evolved-to-get-firefight-mode
         final=https://www.thurrott.com/games/343808/halo-campaign-evolved-to-get-firefight-mode http=200
         title="Halo: Campaign Evolved to Get a Firefight Mode - Thurrott.com" tls=verified cert_status=0x0
         cert_errors=0 net_error=0 load_ms=2170
22:06:32 resources #2 after 10.0 s: total=44 ok=34 http_error=0 failed=10 canceled=0 bytes=984447
```

Failures named in the log, by origin (first load; the second load names the same set minus a few):
`ads.adthrive.com` (2), `www.googletagmanager.com`, `sb.scorecardresearch.com`, `html-load.com` (2),
`pagead2.googlesyndication.com` (4), and `www.thurrott.com` (3, request paths not logged). I did not
find out why these failed. They are ad, analytics and tracker hosts; the log does not carry an error
code, so I cannot say whether they were refused by the site, by Chromium or by the network. The
three `www.thurrott.com` failures are the ones I would look at first.

Time to load: 6.3 s from request to the load event for the home page, 2.2 s for the article. The
first number includes the helper's first-navigation warm-up (the helper had been created 1 s
earlier); it is one run, not a benchmark. An earlier run (run 1, same code before I added the
failure names) gave 9.9 s and 2.9 s with HTTP 200 and `tls=verified` for the same two pages, and
12 failures out of 50 resources for the article; its first resource summary was lost because the
summary was only written if no navigation had happened first, which is the bug the "at next
navigation" line fixed.

The helper's version check in the same log: `Security update available: Chromium 158.0.8127.37
(this is 156.0.8078.88)`, verdict not blocking. That is the existing B06 behaviour, not something
this change touches.

### Window list

`CGWindowListCopyWindowInfo` while the page was loaded (22:06:29), my run is pid 19884:

```
pid=19884 owner=OS999 id=2042 layer=0 onscreen=1 title=OS999 bounds=192,30 1024x800
pid=19884 owner=OS999 id=2043..2047 layer=0 onscreen=0 title= (five untitled, off-screen: menu bar and tool strips)
```

No `OS999Browser` or `Chromium` owner appears in the window list. The helper processes were running
(`ps` showed the cloned helper under the volume's `.os999/Helpers/` and its renderer/GPU children)
with no window. One honest caveat: the list also contains pid 46344, an OS999 build of Ian's that
was already running (`/Users/ian/os999-builds/OS999 Build 35.app`), with its own on-screen window at
the same bounds. I did not start or touch it. The statement "only the OS999 window" is therefore
"the only window from my run".

### Presented surface

From the desktop's probe line (once a second), same run. The page is static after load, so 0 fps is
normal later on:

```
presented 1 fps, frames 1,  surface 1852x1238 colors=21  mean=63  range=3..255   (first paint)
presented 4 fps, frames 12, surface 1852x1238 colors=125 mean=70  range=0..255
presented 2 fps, frames 21, surface 1852x1238 colors=469 mean=169 range=0..255   (article painted, mean jumps)
presented 2 fps, frames 31, surface 1852x1238 colors=469 mean=168 range=0..255   (last frame)
```

A blank surface would be 1 colour. 469 colours and a mean of 168 mean real content was
presented; they do not say it looks right. The desktop framebuffer dump (`frame-1.ppm`) shows the
browser chrome with a white content area, because on macOS the frames are GPU surfaces presented
above the SDL window (see `DEMO-YOUTUBE-HWDECODE.md`, "Pixel proof"); it is not evidence either way.

### SnapDog capture

Not taken. `snapdog://capture?mode=window` needs an interactive window pick and the previous demo
could not drive it. I did not use any other capture tool. Suggested caption for a later capture:
*OS999 desktop with the Web Browser window showing the Thurrott.com front page, captured with SnapDog
on 12 Nov 2026.*

## What is NOT shown

* Ian, or anyone, reading the page. Nobody looked at the window. Layout, fonts, images and colours
  could be wrong; the probe only proves the surface is not blank.
* Scrolling, scroll feel, resize, back/forward, menus, link following by a person. The second load
  was a scripted `LoadURL`, not a click or a link on the page.
* Images and embedded media being displayed correctly (`bytes` shows they were fetched).
* Logged-in state, paywalls, comments, search.
* Ad slots, consent or cookie dialogs: the page has ad and tracker hosts that failed to load (13 and
  10 sub-resources) and I did not see what that does to the layout, whether a consent dialog appeared,
  or whether it blocked reading.
* The three failed `www.thurrott.com` sub-requests, and why any request failed.
* More than two pages, other articles, or the site on another day: Thurrott's content changes.
* Another Mac, Linux, a signed release bundle (developer build with test hooks, ad-hoc signed
  desktop), or a Finder launch (a Finder launch does not pass the shell environment, so
  `OS999_LOAD_DIAG` would be off).
* Sound, video playback, A/V sync (no media was played).

## Reproduce

```
BUILDFLEET_LOCAL=1 sh browser/scripts/build.sh          # default build; runs load_diag_test ("load diagnostics: ok")
cmake -S browser -B build/browser-hooks -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPROJECT_ARCH=arm64 -DOS999_ENABLE_TEST_HOOKS=ON   # measurement build (add -DCEF_ROOT=... to reuse a CEF)
BUILDFLEET_LOCAL=1 OS999_ENABLE_TEST_HOOKS=1 make app APP_BUNDLE="build/OS999 TH.app"
```

Then sign the helper (`browser/scripts/sign.py`), put it at `build/browser/Release/OS999Browser.app`
and launch as in "Method". Unset `OS999_LOAD_DIAG` and nothing is logged.
