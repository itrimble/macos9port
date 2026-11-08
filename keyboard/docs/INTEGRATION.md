# Wiring the keyboards into OS999 Stylized

The keyboard sends the same SDL events a real keyboard and mouse send, so
`src/input.c` and `session_key` need no changes. The desktop already maps
`KMOD_GUI` to Command, and that's what the on-screen ⌘ key sends.

This guide is written against `src/main.c` as of 6 November 2026. Line
positions move, so anchor on the calls named below rather than on line
numbers. Nothing here has been applied to `main.c` yet.

## 1. Makefile

Add the keyboard sources to the `build/ospi` target. They compile with the
existing flags; FreeType is optional, the same as for the desktop.

```make
KBD := keyboard/src/kbd_core.c keyboard/src/kbd_keys.c keyboard/src/kbd_layout.c \
       keyboard/src/kbd_text.c keyboard/src/kbd_touch.c keyboard/src/kbd_pad.c \
       keyboard/src/kbd_motion.c keyboard/src/kbd_anim.c keyboard/src/kbd_gfx.c keyboard/src/kbd_skin.c \
       keyboard/src/kbd_draw.c keyboard/src/kbd_draw_over.c keyboard/src/kbd_synth.c \
       keyboard/src/kbd_mixer.c keyboard/src/kbd_sdl.c keyboard/src/kbd_haptics.c
CFLAGS += -Ikeyboard/include -Ikeyboard/src
```

`src/font.c` is already in `CORE`, and the bitmap fallback uses it. On macOS,
also add `-lobjc` to the link line, for the Force Touch haptics.

## 2. Start-up (after `SDL_Init`)

```c
#include "kbd_sdl.h"

SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");      /* fingers are not mice */
kbd_load_fonts("<bundle>/fonts/ChicagoFLF.ttf");   /* same face the desktop uses */
Kbd *kbd = kbd_create(1.0f);                       /* px per point in the 1024x768 frame */
kbd_set_surface(kbd, OSPI_W, OSPI_H);
kbd_set_posture(kbd, KBD_COVER);                   /* host decides; see section 6 */
KbdAudio *kbd_audio = kbd_audio_open();            /* its own 5 ms device */
KbdHaptics *kbd_haptics = kbd_haptics_open();      /* Force Touch on a Mac, rumble elsewhere */
KbdSdlTarget kbd_target = { .window_id = SDL_GetWindowID(win),
                            .width = OSPI_W, .height = OSPI_H, .pointer_scale = 1 };
static const char *const shortcuts[][2] = {
    { "N", "New Folder" }, { "O", "Open" }, { "W", "Close" }, { "Q", "Quit" } };
kbd_set_shortcuts(kbd, shortcuts, 4);
```

Mirror the desktop's sound preference with `kbd_settings(kbd)->sounds =
desktop_sound_enabled();`, and call `kbd_audio_set_muted(kbd_audio, 1)`
whenever `desktop_sound_guest_active(1)` is called, so a guest keeps the
audio to itself.

## 3. Event loop (first thing inside `while (SDL_PollEvent(&ev))`)

```c
if (kbd_sdl_handle(kbd, &ev, OSPI_W, OSPI_H)) continue;
```

What this call does with each event:

- **Touches** go to the keyboard when they land on it.
- **Physical keys** are only observed: they drive auto-hide, Caps Lock, the
  shortcut sheet and click-along, then pass through. With `pc_modifiers` on,
  Alt and the Windows key are swapped in place.
- **The keyboard's own synthetic events** are ignored, because they carry a
  marker.

## 4. Each frame (next to `session_tick`)

```c
kbd_tick(kbd, SDL_GetTicks());
KbdEvent ke;
while (kbd_poll_event(kbd, &ke)) {
    kbd_audio_play(kbd_audio, &ke, kbd_skin(kbd));
    kbd_haptics_play(kbd_haptics, &ke);
    if (ke.kind == KBD_EV_POINTER) {           /* keep the trackpad's pointer where the desktop's is */
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        kbd_target.cursor_x = (float)mx;
        kbd_target.cursor_y = (float)my;
    }
    kbd_sdl_push(&kbd_target, &ke);
}
```

The pushed events show up on the next `SDL_PollEvent` pass as:

- `SDL_KEYDOWN`, `SDL_TEXTINPUT` and `SDL_KEYUP`
- `SDL_MOUSEMOTION`
- `SDL_MOUSEBUTTONDOWN` and `SDL_MOUSEBUTTONUP`, carrying the click count the
  desktop's double-click logic reads
- `SDL_MOUSEWHEEL`, which goes to `session_wheel`

A two-finger tap arrives as Control down, click, Control up.

## 5. Drawing (after `session_overlay_paint`, before `SDL_UpdateTexture`)

```c
Fb kfb = { .px = frame, .w = OSPI_W, .h = OSPI_H };
kbd_draw(kbd, &kfb);
```

`kbd_occluded_height(kbd)` returns how many rows at the bottom the keyboard
(or the hardware strip) is covering. Use it to scroll the focused text window
clear of the keyboard.

Call `kbd_show(kbd, 1)` when a text field or a guest takes text focus, and
`kbd_show(kbd, 0)` when it loses focus. If a hardware keyboard is in use, only
the slim strip appears.

## 6. Posture

SDL2 can't tell you how a device is folded, so the host passes the posture
in. On Android, Jetpack WindowManager's `FoldingFeature` reports it
(see [Make your app fold aware](https://developer.android.com/guide/topics/large-screens/make-apps-fold-aware)):

| Device state | Posture |
|---|---|
| Cover display active | `KBD_COVER` |
| `FoldingFeature.State.FLAT` | `KBD_OPEN` |
| `HALF_OPENED` with a `HORIZONTAL` fold | `KBD_FLEX` |
| `HALF_OPENED` with a `VERTICAL` fold (book) | `KBD_OPEN` with split on |

Changing posture on the same screen (Open to Flex) morphs the keyboard.
Changing screens (cover to inner) calls `kbd_set_surface` with the new size,
and the keyboard slides in fresh.

On a native phone host, map `KBD_EV_HAPTIC` to the platform's haptics using
the table in [HAPTICS.md](HAPTICS.md). On a Mac, or with an SDL haptic
device, `kbd_haptics_play` already handles it.

Forward contact pressure with `kbd_touch_force`; `kbd_sdl_handle` does this
for SDL finger events. `KBD_EV_FORCE` (force click) is the host's to assign,
for example Get Info.

## 7. What to verify after wiring

These are separate claims, so report them separately. `make test` passing
proves none of them.

1. In the real app window, touch typing reaches a text window.
2. The trackpad pointer clicks a desktop icon, and a double tap opens a
   folder.
3. With a guest focused, the guest receives keys from the on-screen keyboard.
4. The switch sounds and the desktop's interface sounds don't cut each other
   off.
