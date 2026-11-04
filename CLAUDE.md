# OS Pi

Clean-room desktop that looks like a classic Mac, for arm64 and x86_64.
It is a userspace program, not an OS kernel. Extracted CODE-resource apps now
use our independent development runtime. Older archive launches retain a
Systemless fallback; that fallback does not satisfy the development contract.

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
explicitly overrides the shared build-orchestrator requirement here.

The checklist supersedes older scope exclusions below for planned networking,
media, and execution work. Older handoff observations remain historical evidence,
not current acceptance or prohibitions on the newly requested capabilities.

## Do not add

- Apple ROMs, System files, or a Toolbox built by decompiling Apple code
- Apple's Chicago, Charcoal, Geneva, Monaco, or the Apple logo
- SheepShaver, Basilisk II, Mini vMac, or any wrapper that needs a ROM
- Copied text from Inside Macintosh
- The Systemless source tree. It is GPL-3. OS Pi starts the installed
  `systemless` binary. It does not vendor or link that code.

The on-screen name is OS Pi. The menu mark is the Greek letter pi.

## Try it on this Mac

```bash
make test
make run
```

`make run` builds `build/OS Pi.app` and opens it. The volume is
`~/Library/Application Support/OS Pi/Volume`. On Linux the same sources
build with SDL2; `make run` starts `./build/ospi --volume ./volume`.
That Linux path has not been executed on a Raspberry Pi from this tree yet.

Outline fonts need FreeType (`brew install freetype`, or
`libfreetype6-dev` on Debian). The bundled face is Robin Casady's
public-domain ChicagoFLF in `fonts/`. It is not Apple's Chicago.
TrueType, OpenType, and PostScript Type 1 files already on the machine,
or dropped in `System Folder/Fonts`, are used and are not copied into git.

Classic archives open with Systemless
(`brew install benletchford/tap/systemless`). Downloaded Garden software
stays in the volume. Do not commit it.

Files stay under 500 lines. Tests are headless and do not need SDL.

## Pickup, 4 November 2026, about 13:05 local

Uncommitted work on top of `552a235` ("Install Garden archives and open
them with Systemless."). Do not commit or push unless Ian asks. Author
stays `Ian <ian@example.com>`. No `Co-Authored-By`. No force-push.
Do not commit `build/`, the volume, Garden downloads, or third-party
program screenshots.

Ian asked for two things: classic programs should open from OS Pi, and
StuffIt should be installed. The code for the first is in the tree and
`BUILDFLEET_LOCAL=1 make test` printed `ok` before the app was rebuilt.
The desktop double-click was not shown to open a folder or a program.
Leave that claim out.

### What the uncommitted code does

`open_item` calls `classic_open`. A StuffIt, Zip, MacBinary, BinHex,
or disk image launches that file. A binary file with no classic
extension walks parent folders inside the volume and launches the best
archive there: `.sit`/`.sitx`, then `.zip`/`.hqx`/`.bin`/`.sea`/`.cpt`,
then `.img`/`.dsk`/`.toast`/`.iso`/`.cdr`/`.dmg`. Same rank, the name
that sorts first wins. The clicked file's name, cut at 31 bytes, is
passed only in the grandchild as `SYSTEMLESS_LOAD_EXECUTABLE`. An
archive click passes no hint. Text files still open as text. If
Systemless exits within about 800 ms the desktop shows "Systemless
could not open that program." Stdout and stderr of that child go to
`/dev/null`. A missing `systemless` still tells the user
`brew install benletchford/tap/systemless`.

That hint does not select WordPerfect. Cold starts with the hint set
to `WordPerfect`, `Install Corel WordPerfect 3.5e`, and `Install Corel`
all drew Corel OrgChart Builder. Do not describe the hint as a
WordPerfect switch.

`unar` on a future install uses `-k visible`, so AppleDouble
`Name.rsrc` files are ordinary files. Listings hide names ending in
`.rsrc`. `copy_file` still drops a native resource fork. Installs made
before this rebuild, including StuffIt, have no `.rsrc` sidecars. The
original archive is what Systemless must launch.

`session.c` is 495 lines, `install.c` is 498, `volume.c` is 418,
`classic.c` is 293. Do not cross 499.

### Volume, do not commit

`~/Library/Application Support/OS Pi/Volume/Applications/` has
NativeChecker, QuickTime, WordPerfect_3.5e, stuffitexpander_5_5_0,
and the old "Nothing Installed Yet" note. Seed text is create-if-missing,
so that note still has the old sentence.

StuffIt Expander 5.5 came from
`https://macintoshgarden.org/apps/stuffit-expander-55` through the old
binary, before `-k visible` was compiled. Folder
`Applications/stuffitexpander_5_5_0` contains `StuffItExpander_5_5.toast`
(Macintosh HFS, 512-byte blocks, 7919 blocks, volume name
`DropStuff/Expander (v5.5)`, 4059136 bytes),
`stuffitexpander_5_5_0.zip` (2682915 bytes, method store), and
`checksum.md5`. Tokens on that page expire. Refetch before any
re-download. `garden_pick_url` takes the first allowlisted host.

Headless, one process at a time:

```bash
systemless --headless --max-ticks 50 \
  "$HOME/Library/Application Support/OS Pi/Volume/Applications/stuffitexpander_5_5_0/StuffItExpander_5_5.toast"
```

Exit 0. The shot was copied to `/tmp/sit-toast.png`
(sha1 `712b49802fe46d70fb43079083896878cf246472`). It is the Aladdin
splash "Version 5.5 DropStuff With Expander Enhancer" and a Continue
button, guest 800x600. It does not show Expander expanding another
file. Do not claim that. The headless PNG path is one file,
`$TMPDIR/systemless_headless_9999.png`. A second run overwrites it.
Copy immediately.

WordPerfect_3.5e holds `WordPerfect_3.5e.sit` and a folder
`WordPerfect 3.5e` whose installer is `Install Corel WordPerfect 3.5e`
(no extension, resource fork already dropped). Double-clicking that
installer used to show the "cannot show this file as text" note.
The new code should launch the sibling `.sit`. Systemless then starts
OrgChart, not WordPerfect. README already says so.

QuickTime's `.img` and `.img_.sit_.bin` still make Systemless exit 101
(`failed to parse HFS image`). The 800 ms wait should now surface the
dialog. That dialog was not screenshotted. There is no video codec
and no in-process TCP/IP, SSH, or SFTP. Do not add them.

Systemless save for the WordPerfect sit, sha256 of the archive:

`~/Library/Application Support/Systemless/saves/13e4f6c5f5e3d44164292cab846bb8e81691a13851dcc04317c07138db44e7f3`

That directory was recreated at 12:44. `.bak-orgchart` (12:41) is the
earlier OrgChart session. `.bak-fresh` (12:42) is an intermediate cold
save. Do not delete any of the three. A relaunch resumes OrgChart and
ignores the hint while a save exists. There is no `--no-resume` flag.

### App that is running

Pid 68617 is `build/OS Pi.app`, rebuilt after the source edit, ad-hoc
signed, launched with `open`. `make app` deletes the `.app`; quit this
pid first. Do not use Shut Down or the pi menu. As of 13:02 the real
window was id 15421, bounds x=640 y=203, 1024x800. Re-read bounds
before every click. Ignore phantom windows (500x500 at y=796, or
64x64). `peekaboo window list` takes `--pid` or `--app`, not both.
`see` of this window is a 1024x800 logical image. The top 32 rows are
the macOS title bar. Framebuffer (x, y) is image (x, y+32).

A single `cliclick` click at image (97, 136), screen (737, 339),
selected Applications. `cliclick dc:`, Peekaboo
`click --double --foreground`, and `cliclick kp:return` did not open
the folder. Shots `/tmp/ospi-open-3.png` through `/tmp/ospi-open-6.png`
still show the volume window with Applications selected.
`/tmp/ospi-dclick`, an ad-hoc CoreGraphics click with click state 2,
posted nothing visible. SDL is given `ev.button.clicks`. The failure
is consistent with every automated click arriving as clicks 1.
Do not treat that as proof a human double-click fails. It is proof
this session never opened a folder in the running app.

`input.c` was not changed. A time-based second click is unwritten.
Tests click the same zoom box twice with clicks 1
(`tests/test_ospi.c` around the zoom checks). Zoom ignores the count,
so a 500 ms promotion may still pass, but run `make test` if you add it.
Opening must keep working when SDL already reports clicks >= 2.

To prove the open path: select Applications, open it, open
`stuffitexpander_5_5_0`, open `StuffItExpander_5_5.toast`, and capture
the DropStuff splash in a real Systemless window. Then open
`WordPerfect 3.5e/Install Corel WordPerfect 3.5e` and expect OrgChart,
not a text note and not WordPerfect. `Cmd-O` and Return call
`CMD_OPEN` on the selected item (`src/input.c`). Keyboard events from
`cliclick` did not arrive. Custom-drawn pixels ignore Accessibility
element clicks. `osascript` frontmost, then a real click, is what
selected the icon.

`make test` and `make app` were local with `BUILDFLEET_LOCAL=1` because
each is a few seconds, the `.app` is signed on this Mac, and Ian runs
it here. Say that if you do it again. Do not resubmit fleet job
`719d99d9e872438c`. Omarchy is quarantined. Do not start Docker.
Linux `make test` on `552a235` passed in an Apple `container` Debian
12 arm64 container. It has not been rerun on this dirty tree.

Font work is not started. Do not vendor Apple Chicago, Charcoal,
Geneva, or Monaco. Do not vendor Chicago Kare, ChiKareGo, or
JohnDDuncanIII/macfonts. ChicagoFLF is already the bundled face.
Host Geneva is the document face on this Mac.

Clipboard last known value was `https://macintoshgarden.org/apps/all`.
That URL is the catalog index. Do not confirm Install from Garden
while it is on the clipboard.
