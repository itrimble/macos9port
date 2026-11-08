# OS999 on folding phones: design

The target reviewer is someone who has handled every foldable since 2019 and
judges each one on a few questions. Is the cover screen good for anything?
What happens when I unfold it mid-task? Does Flex/tabletop mode do something
clever? Is anything stuck in the crease? Can I use it without a manual? This
port is built to answer those questions in the first minute.

## 1. A desktop per posture, not a shrunken 1024x768

The desktop is resolution independent: `session_paint` accepts any size. Each
posture therefore gets its own desktop resolution, and every desktop pixel is
drawn as a whole block of device pixels, so the pixel art stays sharp. The
block size aims for about 0.19 mm per desktop pixel. That keeps the 20 px menu
bar near 4 mm and the classic 16 px close box near 3 mm. Laptop mode aims for
0.15 mm, because the screen sits further away and you point with the trackpad.

| Device | Cover | Unfolded | Laptop |
|---|---|---|---|
| Pixel 10 Pro Fold | 540x1126 @2x | 692x684 @3x | 1043x498 @2x |
| Galaxy Z Fold8 | 540x1204 @2x | 656x696 @3x | 1060x471 @2x |
| iPhone Fold (rumoured specs) | 474x655 @3x | 879x626 @3x | 640x418 @3x |
| Galaxy Z Flip7 | 474x479 @2x | 540x1208 @2x | 540x583 @2x |

On the cover screen the desktop is portrait, much like the classic portrait
displays. The **Display** tile on the Control Strip lists the other sizes,
like a Monitors panel.

## 2. Unfolding keeps your work

There is one activity and one session. Android's `configChanges` covers fold,
unfold, rotation and resizing, so the app is never recreated. On every
resolution change, `mobile_continuity` moves each window's centre
proportionally and remembers the size the user chose. A window that had to
shrink on the cover screen grows back when you unfold. Windows you resized by
hand keep their new size.

## 3. Postures

The platform reports the hinge (Jetpack WindowManager `FoldingFeature` on
Android). The shell maps it to a posture:

- **Cover**: portrait desktop above, phone keyboard below when typing.
- **Unfolded**: the whole desktop. The keyboard splits into thumb halves with
  a trackpad in the gutter, and nothing sits on the crease.
- **Laptop** (half open, hinge horizontal): the desktop on the top half. The
  bottom half becomes a keyboard deck with a trackpad and button, so the phone
  works as a tiny laptop. This is the showcase posture.
- **Book** (half open, hinge vertical): a keep-clear band down the crease.

A balloon announces each posture change, and the phone gives a haptic "mode"
tick. The **Posture** tile can lock any posture, which is how iOS gets Laptop
mode until Apple publishes a hinge API.

## 4. Touch that needs no manual

| Gesture | Does |
|---|---|
| Tap | Click |
| Double-tap | Open |
| Touch, then move | Press and drag: windows, icons, menus |
| Touch and hold (0.3 s) | The loupe appears, magnifying the desktop under your finger with a crosshair. Slide to aim, lift to click. |
| Keep holding (1 s) | A ring fills around the loupe, then the contextual menu opens |
| Two-finger tap | Contextual menu |
| Two-finger drag | Scroll (pans when zoomed) |
| Pinch | Zoom from 1x to 4x around your fingers |

A first-run guide shows these gestures once, as a Platinum window with
pictures, and its last line adapts to the posture. **Help** on the strip
brings it back. The **Touch/Trackpad** tile switches the whole screen to a
relative trackpad with a drawn pointer.

## 5. The Control Strip

A grip tab sits in the bottom-left corner. Tap it and the tiles slide out:
Keyboard, Touch/Trackpad, Zoom, Display, Posture, Skin (eight keyboard skins),
Sound, Help. Each tile shows its current state, so the strip doubles as a
status readout. Tapping the desktop tucks it away again.

## 6. Haptics and sound

The shell uses the keyboard's haptic vocabulary (`keyboard/docs/HAPTICS.md`):
SELECTION when the loupe appears, FORCE for the contextual menu, TAP for strip
tiles and two-finger taps, MODE for posture changes. Android composes them
from `VibrationEffect` primitives, and iOS maps them onto
`UIImpactFeedbackGenerator` styles. Switch sounds come from the keyboard's
synthesizer and are muted along with the desktop's Sound setting.

## 7. Tablets and hardware keyboards

Tablets are profiles with one landscape screen and no hinge: iPad Pro 13",
iPad Air 11", iPad mini, Galaxy Tab S10 Ultra and Pixel Tablet. They get
classic desktop sizes at 2x:

| Tablet | Desktop |
|---|---|
| iPad Pro 13" | 1376x1012 |
| iPad Air 11" | 1180x800 |
| iPad mini | 1133x719 |
| Galaxy Tab S10 Ultra | 1480x906 |
| Pixel Tablet | 1280x800 |

The platform reports a tablet as one of these:

- **iOS:** the iPad interface idiom.
- **Android:** a screen at least 600 dp wide and no hinge-angle sensor. That
  rules out unfolded foldables, which are just as large.

All three keyboard styles work the same way: a keyboard case with a trackpad,
a fabric folio, or a separate Bluetooth keyboard and mouse.

- **Keys** go straight to the desktop. The on-screen keyboard gives way to the
  keyboard module's slim Platinum strip, and holding Command shows the
  shortcut sheet.
- **A trackpad or mouse** drives OS999's own pointer exactly. It isn't a
  finger, so there's no loupe. The second button, or Control-click, opens the
  contextual menu. Two-finger scrolling arrives as the wheel. The pointer also
  works the Control Strip and the guide.
- **Touch** still works alongside the pointer, and the pointer hides while
  you're using your fingers.
- **The Control Strip** shows "Tablet" and "HW Keys" so you can see what's
  attached.
- **The guide's last line** on a tablet explains the keyboard and pointer.

On iPad, `UIApplicationSupportsIndirectInputEvents` makes SDL deliver the
trackpad as a mouse. In the desk simulator, F8 makes the mouse a pointer
rather than a finger, and tablet profiles start with a keyboard and pointer
attached. To render the mockups:

```sh
make -f mobile/mobile.mk tablets
```

This writes the iPad Pro 13" with each keyboard style to
`mobile/assets/shots/tablets/`. The keyboards are drawn generically: no
maker's design, logo or name.

On the Android emulator, the touch panels also report stylus, so the host
mouse hovering over the emulator window arrives as pointer motion and shows
the OS999 pointer. A hovering pen does the same on real tablets, which is
intended. iPadOS's synthetic hover at (0,0) before any real pointer is
ignored.

## 8. Keyboard and pointer support by platform

| Input | iOS / iPadOS | Android |
|---|---|---|
| OS999 on-screen keyboards (cover, split, laptop deck) | yes: plain touches, drawn by OS999 | yes: tested on emulators |
| Hardware keyboard keys | SDL `pressesBegan` path | tested: `SDL_KEYDOWN` from the key-event path |
| Typed text (capitals, symbols, accents) | `SDL_TEXTINPUT` while a name is edited with a hardware keyboard attached. Text input is started only then, so the system keyboard never appears. | tested: "Hello World" typed into a new folder's name. Composed-text events are enabled without any system keyboard. |
| Modifiers | ⌘ is Command (Mac layout) | PC order swapped: Alt is ⌘ and Win is Option (`pc_modifiers`). Tested: Alt+N made a New Folder. |
| Keyboard attach detection | GameController `GCKeyboard` connect and disconnect (iOS 14+) | `Configuration.keyboard`, plus the first hardware key press on any platform |
| Hardware strip and ⌘-hold shortcut sheet | yes | tested: the strip appears after the first key |
| Back gesture, nav button or mouse back | n/a | acts as Escape and never quits OS999. `OnBackInvokedCallback` (13+), `onBackPressed` (older), trapped by SDL. Tested: Back pressed twice, same process. |
| Trackpad or mouse pointer | `UIPointerInteraction`, with the system pointer hidden | tested: mouse click selects the Trash, and the system pointer is hidden |
| Secondary click or Control-click | contextual menu | contextual menu. SDL drops the mouse "back" key. |
| Two-finger trackpad scroll | wheel (`preciseY`); fractions build up to whole lines | wheel (AXIS_VSCROLL) from mice. Android 14+ touchpads send a two-finger swipe as a fake finger (`CLASSIFICATION_TWO_FINGER_SWIPE`); the activity turns `AXIS_GESTURE_SCROLL_Y_DISTANCE` into wheel lines (24 dp a line) instead of letting SDL move the pointer. |
| Trackpad pinch | a `UIPinchGestureRecognizer` that accepts only `UITouchTypeIndirectPointer`, so screen pinches stay with OS999's own two-finger recognizer | `CLASSIFICATION_PINCH` with `AXIS_GESTURE_PINCH_SCALE_FACTOR` (Android 14+) |
| ⌘ or Control + wheel | zooms, as browsers do | zooms; ChromeOS and some touchpads send pinches this way |
| On-screen trackpad emulation | Laptop deck pad and button, the gutter pad in the split keyboard, hold Space for a pad, and the whole-screen Trackpad mode from the Control Strip | same |
| Clickpad pressure and Force Click | `tfinger.pressure` goes to the keyboard pad. iPhones haven't had pressure since 3D Touch was removed, so expect none there. | `tfinger.pressure` where the panel reports it |
| System IMEs, dictation, third-party keyboards | not used: OS999 draws its own keyboards | not used |

Reserved system shortcuts (iPadOS ⌘H, ⌘Tab and ⌘Space; Android
Meta-shortcuts) never reach OS999.

Platform trackpad gestures go through the bridge
(`os999_mobile_report_magnify`, `os999_mobile_report_scroll`). They queue up
(factors multiply, lines add) and the host takes them once a frame with
`os999_mobile_take_gestures`, then calls `mobile_shell_magnify` and
`mobile_shell_wheel`. A pinch zooms the view around the OS999 pointer, or
around the middle of the desktop when no pointer is shown, between 1x and 4x.
That's the same view and limits as a touch pinch and the Zoom tile.

While the slim hardware-keyboard strip is up, it keeps its own 36 dp band
below the desktop. The desktop is laid out above it, so the Trash and the
bottoms of windows stay in view. This happens once, when the strip first
appears; windows keep their place through continuity. The full on-screen keys
still slide over the desktop and lift it, as on a phone.

The platform's keyboard report reaches the keyboard only when it changes, or
when the keyboard is rebuilt. "Not attached" also forgets that hardware keys
are in use. Some platforms never report the keyboard at all: the Android
emulator, and some Bluetooth stacks. On those, passing the report on every
layout brought the full on-screen keys back after the first key press and on
every rotation.

## Status (6 November 2026)

- **Verified on this Mac:**
  - `make -f mobile/mobile.mk OUT=build/mobile-mac test` passes (590
    checks). The checks cover every device profile and posture, plus a real
    desktop session driven by fingers, a mouse, hardware keys, typed text and
    trackpad pinches. On the Mac use `OUT=build/mobile-mac`; `build/mobile`
    can hold Linux binaries from other sessions.
  - `make test` passes with the menu-bar change in `src/paint.c`.
  - The shots are rendered for six device profiles.
  - The desk simulator builds.
  - The iOS app builds for the arm64 simulator and launches on an iPhone Air
    simulator, which shows the desktop and the first-run guide.
  - The Android debug APK builds (`./gradlew :app:assembleDebug`), with
    native libraries for arm64-v8a and x86_64 and the desktop's resources.
    `mobile/scripts/install-android-sdk.sh` installs the toolchain: JDK 17,
    command-line tools, platform 36, build-tools 36.0.0, NDK r28c
    (28.2.13676358) and CMake 3.22.1.
  - The iPad Air 13" simulator runs OS999 with a portrait 1024x1356 desktop
    at 2x and the tablet guide. No keyboard or trackpad input was sent to
    it.
  - The Android Pixel Tablet emulator (`os999_pixel_tablet`) classifies as a
    tablet (`OS999Fold: ... tablet=true`: 2560x1600, no hinge sensor) and
    shows the tablet guide.
  - OS999 keeps rendering while Android's "Viewing full screen" notice holds
    focus: the menu-bar clock advanced from 1:50 to 1:51 under it. SDL 2.32
    doesn't pause on focus loss on API 24+.
  - The headless checks cover tablets:
    - layout
    - the hardware-keyboard strip and its band, which keeps the Trash above it
    - pointer clicks on the desktop and the strip
    - a secondary-button contextual menu
    - a pinch zoom that holds the point under the pointer
    - the platform gesture queue
    - fractional wheel lines
  - **Android Pixel Tablet emulator, hardware keyboard and mouse:**
    - Alt+N made a New Folder.
    - "Hello World" was typed into its name, capitals included, and Return
      committed it.
    - The hardware strip appeared after the first key.
    - A mouse click selected the Trash.
    - With the slim strip up, the Trash and its label sit fully above it, in
      landscape and after a rotation to portrait. Typed letters kept
      selecting icons by name (R selected Read Me, D selected Documents).
    - Back pressed twice left OS999 running in the same process.
  - **Android emulator, Pixel 10 Pro Fold profile** (`mobile/scripts/android-emulator.sh`):
    - The app runs and taps reach the desktop.
    - The posture follows the hinge that Jetpack WindowManager reports:
      unfolded, Book (half-folded upright, with its balloon) and Laptop
      (half-folded in landscape: desktop above, keyboard deck below).
    - Moving the deck's trackpad moves the pointer.
    - Closing the phone moves the session to the cover screen in the same
      process, and the open window carries over.
- **Not verified:**
  - Touch input in the iOS Simulator. No taps were sent; fingers are only
    exercised by the headless checks.
  - A real phone of any kind.
  - Android on a real phone. Only emulators have been used; there's no real
    foldable available.
  - Two-finger gestures on Android. `adb input` sends one finger at a time.
  - Any key, pointer or text input on iOS. The simulators can't be scripted
    to send keys.
  - Trackpad pinch and two-finger swipe on real touchpads, on either
    platform. Neither emulator nor simulator can produce an
    indirect-pointer pinch or a touchpad-classified MotionEvent. The Android
    scroll sign follows the framework's definition (positive scrolls the
    content down) and needs a check on a real touchpad.
  - Right-click and mouse scroll on Android. `adb input` can't send them.
  - The generic 6.7in flip emulator. It delivers each fold report one
    posture late (seen in the `OS999Fold` log). Re-subscribing returned the
    same stale value, so the workaround was dropped. Its open/half/closed
    states show the previous posture.
  - Apple's foldable. Its figures are leaks.
- **Known gaps:**
  - The app bundle is about 139 MB because the wallpapers ship as BMP. Ship
    them as PNG, or decode on first run.
  - Classic-program launching uses fork/exec, which isn't available on iOS.
  - Android needs minSdk 28 (posix_spawn), and an Android-only shim backs
    shm_open/shm_unlink with files in private storage
    (`sdl/mobile_compat_android.c`).
  - There's no boot animation on mobile yet.
  - The Galaxy Z Flip cover screen is cramped once the keyboard is up.
  - The translucent keyboard skin blends over the composed desktop, as
    intended, but is untested on a device.

## Integration recovery, 7 November 2026

The keyboard, pointer and gesture work described above was recovered from the
6 November source backup. Its earlier emulator observations are historical;
this integration has not repeated device or simulator interactions. Current
validation is recorded separately in the integration receipt. Native CMake now
uses the shared desktop core manifest and requires target builds of curl,
zlib and FFmpeg (optional bzip2). The dependency fetch script does not provision
these new target libraries. iOS and Android compilation remains unverified.
