# OS999

I wanted a classic Mac desktop I could actually click on this Mac, on a
Raspberry Pi, and on a plain x86_64 Linux box. OS999 1.1 is that desktop.
It is a 64-bit program I wrote from scratch. It is not Mac OS 9.

Jeff Price posted a photo of something he called OS π HD running on a
Raspberry Pi 4. I have not seen his source. This repository is not his
work, and it does not contain an Apple ROM.

## Development checkpoint — 7 November 2026

I am combining the separate development branches in this candidate. The
[master tracker](docs/OS999-MASTER-TRACKER.md) records that work and its remaining
checks. The [6 November status audit](docs/PROJECT-STATUS-2026-11-06.md) and
[worktree inventory](docs/WORKTREE-INVENTORY-2026-11-06.md) retain the earlier
branch evidence; their revisions and paths are historical.

Monkey Island opened its screens, accepted clicks, and played sound in my
hands-on run. Sustained play, save/reload and latest-build performance still
need separate verification. An older runtime recorded a 60 FPS median and
52.5 minimum; those numbers do not describe this integration candidate.
Host browser, networking and movie code also do not establish guest MacTCP,
Open Transport or original classic QuickTime acceptance. A passing build is
one checkpoint. It is not the finished product.

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

That builds `build/OS999.app` and opens it. The window is 1024 by 768.
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
File also offers Get Info (Cmd-I) and Close All desktop windows. Edit offers
Select All (Cmd-A), Copy Names for folder selections and Copy Text for text
windows (Cmd-C), using the Mac clipboard. These copy text, not files; Cut,
Paste and Undo remain unavailable. Arrow keys navigate folder items; Home/End
select the first/last item, and typing a letter cycles matching initials.
List headings sort by Name or Kind when clicked. View switches folders between Icon/List
and sorts by Name/Kind; view settings last while that window remains open.
Use arrow keys and Return in open menus, or Escape to dismiss them.
Help includes Keyboard Shortcuts and What Works Today.
Double-click a title bar to windowshade it. The `999` menu has About and
Shut Down.

The Special menu toggles the startup intro and previews it (F9/F10 when
no guest has keyboard focus). Interface Sounds has a checkmark when enabled;
its setting is saved per volume. Four original Classic 9 sounds cover startup,
alerts and menu transitions. Host effects are suppressed while a guest runs.
The desktop uses the delivered icon pack for recognized applications, folder
roles and document types, retaining app-provided guest icons. Unknown names
keep a generic icon; icon selection does not change how an item opens.
These remain logical 32px icons; native HiDPI icon rendering is not implemented.
Appearance lists 59 original wallpapers, ten per page, and remembers the choice
per volume. Plain Desktop keeps the original gradient. The macOS app packages
1024×768 bitmaps converted with `sips`; HD masters remain under `assets/`.
Sixteen built-in color themes are available in Appearance → Color Themes
or Control Panels → Appearance → Themes. Cmd-T cycles themes; each volume
remembers its selection. These are fresh local palettes, not the missing
delivered skin engine. Themes change desktop colors; wallpaper, sounds and
fonts remain separately configured. Custom theme files are not supported yet.
Use OS999 → Control Panels → Appearance for Desktop and Themes tabs.
Desktop offers the same saved picture selection; Themes selects a color palette. Right-click or Control-click desktop space, disks, folders
or files for contextual actions. Escape dismisses a popup; Escape or Cmd-W
closes the control panel. Control Panels shows an Appearance icon with its own
Open contextual action. Guest application clicks keep their existing routing.

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

There is no guest networking stack yet. Garden page and archive downloads go
through the HTTPS-only libcurl transport described below (never the host `curl`).
OS999 does not serve SSH or SFTP. Double-click a
`.mov`, `.qt`, `.mp4`, or `.m4v` file and OS999 opens a movie window when the
video is uncompressed 32-bit `raw ` frames in a classic movie container. Other
codecs stay closed, with an explanation, and audio tracks are not played yet.
A QuickTime page on Macintosh Garden is a classic installer disk. Opening that
disk does not run Apple's QuickTime.

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
Integrated Chromium, guest networking APIs, and classic QuickTime playback remain
separate unchecked goals. The host HTTPS layer is described below. Guest execution is not a security sandbox.

## Terminal

Open **OS999 HD → Applications → Utilities → Terminal** or **File → New Terminal**.
The launcher is added to seeded volumes without overwriting existing files.
Terminal opens the host's interactive login shell in the OS999 volume; your
shell startup files can choose another working directory. On this Mac that is
zsh with Oh My Zsh and Powerlevel10k. OS999 reads the normal shell configuration
through zsh; it does not install plugins or rewrite `.zshrc`.

The dark terminal uses bundled Meslo Nerd Font Mono (regular and bold),
antialiased with FreeType, with Unicode prompt symbols, Powerline separators,
256 colors, truecolor, and Unicode copying. The font and upstream license notices
ship inside the app; a system-wide font installation is unnecessary. Without
FreeType, the fallback is limited bitmap text and does not provide Nerd glyphs.
This is a fixed-cell terminal, not a complete Unicode shaping/emoji engine.
The existing theme menu switches to light mode when desired.

`--terminal` opens a shell when launching a desktop for testing. SSH uses the
host's `ssh`, keys and configuration. Authentication, interactive remote programs,
and extended sessions need hands-on verification in the packaged preview.

## Tailscale

Open **Applications → Utilities → Tailscale** or **OS999 → Tailscale**.
The built-in terminal utility lists the host's tailnet devices. Type `ssh 2 ian`
to connect to device 2 as ian; device numbers refer to the last displayed list.
Use `refresh` to reload it and `settings` to inspect host connection settings.
`connect`, `disconnect`, `login`, `dns on|off`, `routes on|off`, and
`shields on|off` explain their host-wide impact and require typing `yes`.
These settings affect other applications and existing remote connections too.

Install and authenticate the official Tailscale host client first. OS999 packages
its management helper, not a VPN daemon. It stores no login keys and does not
enable a Tailscale SSH server. SSH uses `/usr/bin/ssh`, normal authentication and
host-key verification; the remote device must provide an SSH service.
Missing client/service permissions are reported without automatically escalating.

The helper supports standard macOS/Homebrew/Linux client paths; an absolute
`OS999_TAILSCALE_CLI` can select another installed client. `--tailscale` opens the
utility at desktop startup. `build/os999-tailnet --status` performs a read-only
probe. Run `make test-tailnet` for isolated fake-client integration checks.
Live settings changes and authenticated remote SSH are separate manual checks.

## Fonts

OS999 includes Font Book 999 under Applications → Utilities and in the OS999
Control Panels submenu. It scans bundled fonts and the simulated
`System Folder/Fonts`, validates files before installation, indexes TrueType,
OpenType, collections, Type 1, and exported suitcase/resource containers, and
persists activation without installing anything into the host Mac. Font Book
uses a classic OS999 window with Collections, Font Family, and Preview panes;
it is an OS999 utility, not a claim that Mac OS 9 shipped Apple's later Font
Book application.

The optional base coverage pack in `fonts/system/` contains licensed Noto
faces and exact upstream notices. OS999 body text now uses HarfBuzz shaping,
FriBidi ordering, libunibreak opportunities, and ordered per-cluster fallback
when those libraries are available; builds without them retain an explicit
degraded path. Automated shaping and pixel tests are not language-quality
acceptance. Color emoji, complete ZWJ/variation behavior, locale-sensitive CJK,
and language-by-language human review remain open. See
`fonts/system/SOURCE-MANIFEST.md` for hashes, sources, and current limitations.

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

Mac, Mac OS, and Finder are trademarks of Apple. OS999 is an
independent program and is not affiliated with Apple.


## Modern HTTPS and certificates

The networking worktree adds `modern_network.h` and a libcurl transport used by
Garden page/archive downloads. It requires libcurl 7.85 or newer; use a currently
patched distribution build. On macOS, `brew install curl` supplies a separate
maintained library; the Makefile prefers its pkg-config metadata without replacing
system curl. The resulting development app depends on that installed library.
Redistributable packaging of its dylibs has not been implemented.

Connections require HTTPS, verified certificate chains and hostnames, and TLS 1.2
or newer. The default uses the host trust configuration (explicit Apple SecTrust
with current macOS curl). A trusted host caller may supply a PEM CA file or a PEM
client certificate/key pair. No roots are installed, no private keys are copied,
and guest files/environment variables cannot configure these policies. Custom
trust is backend-dependent and is not a browser trust-store import mechanism.
Client-authenticated requests refuse all redirects to prevent identity forwarding.

Each request needs an explicit destination callback, checked again on every
redirect. Garden restricts it to its page and mirror hosts. Ambient proxies,
netrc credentials, cookies, and curl configuration files are not used. Downloads
have a total deadline, cancellation, a byte limit across all hops, and a maximum
of five redirects. They replace the destination only after a successful complete
2xx response. Failed downloads preserve an existing destination. Calls are
blocking worker operations; this does not implement guest MacTCP/Open Transport,
UDP, inbound listeners, or a network sandbox. Garden's existing synchronous UI
flow remains synchronous.

`browser/` is an isolated source snapshot of the existing CEF browser, with
certificate enforcement and launch-policy changes. Chromium retains its own
network process and trust handling. Native client-certificate selection has not
been verified.
The libcurl CA/client-key policy does not override Chromium. Browser navigation
requires HTTPS (apart from explicit internal diagnostic pages), invalid
certificates have no click-through, and unsupported command-line switches are
rejected. The lock reflects Chromium SSL status rather than the URL prefix.
Its separate profile is `~/Library/Application Support/OS999 Browser Networking`.
Profile aliases are resolved before deriving the cache path, preventing CEF from
silently falling back to in-memory storage for macOS `/var` paths or symlinks.
The browser remains a standalone app, not embedded in this worktree's desktop.

Run `BUILDFLEET_LOCAL=1 make test-network` for policy and real loopback TLS tests
(Python `cryptography` is a test-only dependency). The certificate fixtures and
keys are generated in a temporary directory and removed after each run. After
configuring/building CEF, run the requested bounded Ralph verification loop:

```sh
BUILDFLEET_LOCAL=1 python3 scripts/ralph-network-loop.py --browser --passes 2
```

It stops on failures for the owning agent to repair, records immutable source
hashes and artifact hashes, and requires consecutive successful passes. Evidence
is under `build/ralph-network/`. Browser checks use disposable profiles, reject
unsafe switches, attempt to reject an untrusted local server and HTTP navigation, and load
example.com over HTTPS. Public-network availability is required for that last
check. These checks do not prove phishing protection, exhaustive revocation,
Linux/Pi compatibility, browser keychain-selection UX, or end-user acceptance.

Current browser security gate: **passed** on this Mac: six unsafe launch flags
are rejected, an untrusted certificate and plaintext navigation are blocked,
and public-trust HTTPS completes with verified connection state. All 29 transport
cases also pass, including the sanitizer run. The standalone browser visibly
renders example.com with its connection lock. The Certificates menu explains
validation policy; CEF's certificate-manager page did not open and is not exposed.
Certificate import and native machine-client-identity selection remain unverified.

Earlier runs stalled while a worker waited in macOS Keychain retrieval. The
resumed run no longer stalls; how that external condition cleared was not
observed. No Keychain items, ACLs, passwords or trust roots were changed by this
work. Diagnostic logs remain in ignored `build/`. This is a standalone host
browser and transport implementation, not guest socket or integrated desktop
browser acceptance. See the final Ralph receipt for the exact tested snapshot.
