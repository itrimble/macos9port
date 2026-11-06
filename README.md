# OS999 Stylized

I wanted a classic Mac desktop I could actually click on this Mac, on a
Raspberry Pi, and on a plain x86_64 Linux box. OS999 Stylized 0.1 is that desktop.
It is a 64-bit program I wrote from scratch. It is not Mac OS 9.

Jeff Price posted a photo of something he called OS π HD running on a
Raspberry Pi 4. I have not seen his source. This repository is not his
work, and it does not contain an Apple ROM.

## Try it on a Mac

You need [SDL2](https://www.libsdl.org/) (`brew install sdl2`), FreeType
(`brew install freetype`), and clang. Only the older archive-launch fallback uses
[Systemless](https://github.com/benletchford/systemless):

```bash
brew install benletchford/tap/systemless
```

Systemless is GPL-3 and it is not part of this repository. Extracted applications
with supported CODE resources now use OS999's independent development runtime.
Neither path includes an Apple ROM in this repository.

```bash
make run
```

That builds `build/OS999 Stylized.app` and opens it. The window is 1024 by 768.
Your files live in a real folder:

`~/Library/Application Support/OS Pi/Volume`

New Folder creates a real directory. Dragging an icon to the Trash moves
the real item. Empty Trash deletes it. Quit, then delete that folder, and
the next launch seeds the four original items again.

Headless checks, no window:

```bash
make test
```

## What this build does

The menu bar, the striped windows, the blue desktop, and the four icons
are drawn here. The clock is the machine's clock. The memory figure is
the memory the process can see. Dates are 64-bit host time, so this
program does not inherit the old HFS overflow on 6 February 2040.

Shortcuts: Cmd-N new folder, Cmd-O open, Cmd-W close, Cmd-Q quit.
Double-click a title bar to windowshade it. The `999` menu has About and
Shut Down.

The Special menu toggles the startup intro and previews it (F9/F10 when
no guest has keyboard focus). Interface Sounds has a checkmark when enabled;
its setting is saved per volume. Four original Classic 9 sounds cover startup,
alerts and menu transitions. Host effects are suppressed while a guest runs.
The desktop now uses the delivered original icon pack, retaining guest app icons.
These remain logical 32px icons; native HiDPI icon rendering is not implemented.
The 59 wallpapers and HD variants are retained under `assets/`. The separate
sixteen-theme delivery has not arrived, so Appearance and Cmd-T are not present.

For a parallel test build, use `make app APP_BUNDLE="build/OS999 Test.app"`
and launch it against a copied volume. Never rebuild a live bundle in place.

File, Install from Garden, takes a `macintoshgarden.org` link from the
clipboard and downloads that page's archive into Applications. If the
main download host refuses the file, OS999 tries the same path on the
page's other two Garden hosts. The original StuffIt or MacBinary file
stays in the folder. Double-click a supported 68k application and OS999 opens it
in its independent runtime. Some archive formats still use the optional
Systemless fallback; it may choose a different application from the archive. On
the WordPerfect 3.5e archive that is Corel OrgChart Builder, not WordPerfect.
Many programs still fail. I do not commit those downloads.

There is no integrated browser or guest networking stack yet. Garden downloads
use the host's `curl`. OS999 does not serve SSH or SFTP, and it does not include video
or audio codecs. A QuickTime page on Macintosh Garden is a classic
installer disk. Opening it does not play a movie.

I have built and run this on an Apple Silicon Mac. On 4 November 2026
`make test` passed in a Debian 12 arm64 container on that Mac, using
Apple's `container` tool. I have not opened the windowed app on a
Raspberry Pi or on x86_64 yet. The sources are the same C. On Debian
or Raspberry Pi OS:

```bash
sudo apt install build-essential libsdl2-dev libfreetype6-dev pkg-config
make
./build/ospi --volume ./volume
```

The Mac build links Homebrew's arm64 SDL2, so the app on this machine
is arm64 only.

## Independent runtime development

I have added a separate `ospi-runtime` helper. It uses the open-source Musashi
instruction interpreter and our own resource loader and Macintosh API adapters.
It loads original 68k game code without an Apple ROM or System file. Ian's
5 November screenshots show the original Mac Secret of Monkey Island playing
several scenes inside the OS999 desktop; he reports that gameplay continues
normally, but the music turns into persistent ticking in some scenes. The game
has not passed sustained audio, save/reload, measured frame-rate, or full GPU
acceleration acceptance. Color drawing and sound support remain incomplete.

```bash
BUILDFLEET_LOCAL=1 make test runtime
./build/ospi-runtime --inspect '/path/Game.rsrc'
./build/ospi-runtime --window --indexed --steps 100000000 '/path/Game.rsrc'
./build/ospi-runtime --window --indexed --interactive '/path/Game.rsrc'
python3 scripts/runtime-checkpoint.py --indexed '/path/Game.rsrc'
```

Desktop launches use `--interactive`: the window runs until closed or a runtime
failure occurs. CPU batches and callback work remain bounded. Headless probes
retain instruction limits; `--interactive` cannot be combined with `--steps`.

The first build downloads a pinned, hash-checked Musashi revision into ignored
`build/deps/`. The local-build flag follows Ian's explicit instruction to bypass
the build orchestrator for this project. Read
[the development checklist](docs/DEVELOPMENT-CHECKLIST.md) and
[the first loop record](docs/RUNTIME-LOOP-2026-11-04.md) before continuing.

Keep an extracted application file beside its `Name.rsrc` sidecar and game data
inside the OS999 volume. Double-click the application, or select it and use
Cmd-O. The helper must sit beside `ospi`; `make app` packages both. Quit that
bundle before rebuilding it. Native resource forks also work. Unsupported
runtime calls stop with a diagnostic; they do not silently switch to Systemless.
The runtime currently reads only ASCII-named sibling data files, with no writes.

The SDL helper requests an accelerated presentation backend. Guest rasterization
and framebuffer conversion remain CPU work. That is not full game acceleration.
Chromium, modern browsing, networking, and classic QuickTime playback remain
separate unchecked goals. Guest execution is not a security sandbox.

## Fonts

Menus use [ChicagoFLF](fonts/ChicagoFLF.ttf). Robin Casady drew it and
put it in the public domain. The notice is in the font and in
`fonts/LICENSE.txt`. It is a revival. It is not Apple's Chicago, and
Apple's Chicago, Charcoal, and Geneva are not in this repository.

FreeType also opens TrueType (`.ttf`, `.ttc`), OpenType (`.otf`), and
PostScript Type 1 (`.pfa`, `.pfb`). Drop those into
`System Folder/Fonts` on the volume. If this Mac already has Geneva,
that face is used for documents. The 8 by 8 public-domain
[font8x8](https://github.com/dhepper/font8x8) set is the fallback when
FreeType or a font file is missing. The pi glyph, when the outline face
has no pi, is mine.

Mac, Mac OS, and Finder are trademarks of Apple. OS999 Stylized is an
independent program and is not affiliated with Apple.
