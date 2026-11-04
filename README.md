# OS Pi

I wanted a classic Mac desktop I could actually click on this Mac, on a
Raspberry Pi, and on a plain x86_64 Linux box. OS Pi 0.1 is that desktop.
It is a 64-bit program I wrote from scratch. It is not Mac OS 9.

Jeff Price posted a photo of something he called OS π HD running on a
Raspberry Pi 4. I have not seen his source. This repository is not his
work, and it does not contain an Apple ROM.

## Try it on a Mac

You need [SDL2](https://www.libsdl.org/) (`brew install sdl2`) and clang.

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

## What it does not do

It does not run classic Mac applications. There is no 68k or PowerPC
translator, no Toolbox, and no networking stack in this build. Those are
the point of the project, and they are not in 0.1.

I have built and run this on an Apple Silicon Mac. I have not run it on
a Raspberry Pi or on x86_64 yet. The sources are the same C. On Debian
or Raspberry Pi OS:

```bash
sudo apt install build-essential libsdl2-dev pkg-config
make
./build/ospi --volume ./volume
```

The Mac build links Homebrew's arm64 SDL2, so the app on this machine
is arm64 only.

## Font

The letters are the public-domain [font8x8](https://github.com/dhepper/font8x8)
set by Daniel Hepper. The pi glyph in the menu is mine. None of the
type is Chicago.

Mac, Mac OS, and Finder are trademarks of Apple. OS Pi is an independent
program.
