# Build 20 acceptance record — 10 November 2026

Artifact: `OS999 Build 20.app`, run `r9-integration-20` on the MBP, gate passed
9 November 06:15–06:20 CST (15 of 15 stages). Source: commit `a1d2bbe`, tree
`3f385375b033628c380f3d7a89f4c17fe74e3a7f`. The copy tested here was the Neo's
`~/os999-build13/OS999 Build 20.app`; all 448 files match the gate receipt's
SHA-256 list and `codesign --verify --deep --strict` passes.

## How this pass was run

- Machine: Neo, macOS 27.2. A clone of the bundle in a scratch folder, started
  as `ospi --volume <fresh seeded volume>`. No live volume or save was touched.
- Input: scripted single clicks and keys sent to the window in the background
  (cua-driver), with a window capture after each step. Nobody listened to audio.
  No double-click, drag or hardware pointer was used.
- This is a scripted windowed pass by Claude. It is not Ian's hands-on
  acceptance, and items marked "Not tested" still need it.

## Results

| Area | Check | Result | What was seen |
| --- | --- | --- | --- |
| Startup | Launch, desktop, disk window | Pass | Desktop and `OS999 HD` window drawn at 1024x768 (2048x1536 drawable) |
| Startup | Build identity shown | Fail | About says "OS999 Stylized 0.1 (arm64)"; `Info.plist` says 1.1.0; "Build 20" appears nowhere in the app |
| Startup | Startup recovery, boot animation | Not tested | |
| Session | Shut Down from the OS999 menu | Pass | Process exited 0; no confirmation dialog was observed |
| Session | Relaunch and restore | Pass | Applications window reopened; new folder and note still on disk |
| Session | Quit OS999 from File | Pass | Process exited 0 |
| Finder | Open a folder (select, File > Open) | Pass | Applications opened with 10 items |
| Finder | Open by double-click | Not tested | Scripted double-clicks are not evidence either way |
| Finder | New Folder | Pass | "untitled folder" created in rename mode and present on disk |
| Finder | Rename by typing | Not tested | The driver refused to type into the rename field |
| Finder | Get Info | Pass | Name, kind, item count, path and dates shown; OK dismisses |
| Finder | Copy, move, drag, keyboard navigation | Not tested | |
| Menus | Menu ownership | Pass with note | App name switches Finder/Browser; while a menu was open just after closing the browser, the windows behind drew blank until the menu closed |
| Apps | About This Mac | Pass | OS 9 style panel with memory bar and credits |
| Apps | Notes | Pass | Typed text appeared and was saved to `Documents/Notes` with no save prompt |
| Apps | Control Panels window | Pass | Ten panels listed with category tabs |
| Apps | Bluetooth panel | Pass (panel only) | Opens and reports "Adapter: present and powered on"; its own text says pairing, real devices and timing are unverified. Scan and Connect not pressed |
| Apps | Extensions Manager, Font Book, Console, Terminal | Not tested | |
| Browser | Launch embedded Chromium | Pass | Opened to YouTube's home page; no keychain prompt |
| Browser | Load `https://www.thurrott.com/` | Pass | Home page rendered, "Verified HTTPS", GPU frame 1852x1238 reported in the status bar |
| Browser | Article navigation, scrolling, media | Not tested | |
| Browser | Chromium currency | Fail | Status bar: "Security update available: Chromium 158.0.8127.18 (this is 156.0.8078.80)" |
| Browser | Helper lifecycle | Pass with note | Seven helper processes stayed alive after the browser window closed; all exited at Shut Down |
| Browser | YouTube playback, hardware decode proof | Not tested | |
| Movies | Movie chooser | Pass | Empty state on a fresh disk; listed `Test Pattern.mp4` once present |
| Movies | Play and pause a generated H.264/AAC clip | Pass (picture only) | Frames advanced to 00:11; Pause held. Audio not listened to; seek, unsupported input and cancel not tested |
| Movies | Guest QuickTime | Not tested | Separate gate; this is the host player |
| Games | Quake III Arena | Blocked | Menu items disabled on a fresh volume with no demo data installed |
| Classic apps | 68K or PowerPC guest launch | Not tested | No guest software on the disposable volume |
| Telemetry | Off until the user opts in | Fail | A fresh volume wrote `~/Library/Application Support/OS999/Telemetry/events-20261110.jsonl` from the first click. `main.c` calls `telemetry_enable()` unconditionally; `OS999_TELEMETRY=0` is the only switch and there is no in-app consent or toggle |
| Telemetry | Content | Pass | 49 events: clicks, menu titles, window counts and front-window names. The typed note text and the Thurrott URL were not in the file. Local file only |

## Branding findings

- The host movie window is titled "QuickTime Player 999: <file>" (`src/movie_window.c:244`).
  That is an Apple product name on our own player and blurs the separate guest
  QuickTime gate.
- About This Mac lists "Letters: ChicagoFLF, Geneva". Confirm which bundled face
  that second label refers to; Apple's Geneva must not ship.
- The menu item reads "OS999 Amp (Winamp)...".
- Version strings disagree (0.1 in the app and log, 1.1.0 in `Info.plist`).

## Open items carried to the next build

1. Telemetry: make it off by default with a real consent control, or rename the
   claim. This is the only finding that contradicts Build 20's own title.
2. One version string and a visible build number.
3. Rename the host movie window; check the Geneva label and the Winamp label.
4. Chromium: corrected the same day. The CEF index has no 157 or 158 build;
   the newest stable is Chromium 156.0.8078.88. The pin can move from
   8078.80 to that patch, and the status-bar notice will remain until CEF
   publishes a newer major.
5. Hands-on pass by Ian for every "Not tested" row, plus listening tests.
