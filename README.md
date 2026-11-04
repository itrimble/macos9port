# OS Pi

I wanted a classic Mac desktop I could actually click on this Mac, on a
Raspberry Pi, and on a plain x86_64 Linux box. OS Pi 0.1 is that desktop.
It is a 64-bit program I wrote from scratch. It is not Mac OS 9.

Jeff Price posted a photo of something he called OS π HD running on a
Raspberry Pi 4. I have not seen his source. This repository is not his
work, and it does not contain an Apple ROM.

## Try it on a Mac

You need [SDL2](https://www.libsdl.org/) (`brew install sdl2`), FreeType
(`brew install freetype`), and clang. To open a classic program you also
need [Systemless](https://github.com/benletchford/systemless):

```bash
brew install benletchford/tap/systemless
```

Systemless is GPL-3 and it is not part of this repository. OS Pi runs
that installed program. It does not contain a Toolbox or an Apple ROM.

```bash
make run
```

That builds `build/OS Pi.app` and opens it. The window is 1024 by 768.
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
Double-click a title bar to windowshade it. The pi menu has About and
Shut Down.

File, Install from Garden, takes a `macintoshgarden.org` link from the
clipboard and downloads that page's archive into Applications. If the
main download host refuses the file, OS Pi tries the same path on the
page's other two Garden hosts. The original StuffIt or MacBinary file
stays in the folder. Double-click it and Systemless opens the program
in its own window. Many programs still fail. I do not commit those
downloads.

There is no networking stack inside OS Pi. The download is the host's
`curl`. OS Pi does not serve SSH or SFTP, and it does not include video
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

Mac, Mac OS, and Finder are trademarks of Apple. OS Pi is an independent
program.
