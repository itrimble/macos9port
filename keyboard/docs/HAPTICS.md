# Haptics

The keyboard emits `KBD_EV_HAPTIC` events. Each one carries a `haptic` type
plus the `strength`, `sharpness` and `duration_ms` from `kbd_haptic_spec`,
with `strength` already scaled by `KbdSettings.haptic_strength`. Every
platform plays the same vocabulary, so a key press feels like a key press
whether it's rendered by a Force Touch trackpad or a phone's actuator.

| Type | When | Intensity | Sharpness | ms |
|---|---|---|---|---|
| `KEY` | a character key goes down | 0.45 | 0.85 | 8 |
| `KEY_WIDE` | space, return, delete, shift | 0.60 | 0.60 | 12 |
| `MODIFIER` | a modifier latches or unlatches, Caps Lock | 0.50 | 0.70 | 10 |
| `SELECTION` | each new accent in the long-press menu, a layer change, word-delete starting, three-finger drag starting | 0.35 | 0.90 | 6 |
| `TAP` | tap to click, two-finger tap (Control-click), the hardware strip's Keyboard button | 0.50 | 0.80 | 9 |
| `CLICK` | clickpad pressed past the click threshold, the Flex pad button down | 0.85 | 0.75 | 14 |
| `RELEASE` | clickpad released, pad button up | 0.55 | 0.90 | 9 |
| `FORCE` | the second, deeper stage of a press | 1.00 | 0.45 | 22 |
| `MODE` | holding the space bar turns the keyboard into a trackpad | 0.60 | 0.40 | 16 |

The `CLICK`/`RELEASE` pair copies a Mac trackpad: the press is fuller and the
release lighter and crisper. Because the release threshold sits well below
the click threshold, every press gets exactly one of each.

## SDL hosts: `kbd_haptics_open` / `kbd_haptics_play`

- **macOS:** the haptics play on the Force Touch trackpad through AppKit's
  [`NSHapticFeedbackManager`](https://developer.apple.com/documentation/appkit/nshapticfeedbackmanager)
  default performer, with performance time `Now`. AppKit offers three
  patterns and no intensity control, so the types are mapped like this:
  - `KEY`, `KEY_WIDE`, `TAP`, `CLICK` and `RELEASE` play as `Generic`.
  - `MODIFIER` and `SELECTION` play as `Alignment`.
  - `FORCE` and `MODE` play as `LevelChange`.

  You only feel it while a finger is on the trackpad. In the simulator that's
  whenever the mouse-finger is down, so the on-screen keyboard and trackpad
  give real haptic feedback on a desk Mac. The demo build links `-lobjc` on
  Darwin.
- **Everything else:** the first SDL haptic device that supports rumble,
  driven with
  [`SDL_HapticRumblePlay`](https://wiki.libsdl.org/SDL2/SDL_HapticRumblePlay)
  at each type's strength. Durations are stretched to at least 14 ms so a
  rumble motor can render them.
- **Both backends:** pulses closer together than 14 ms are dropped, so fast
  typing stays a clean rhythm instead of turning into a buzz. Release pulses
  are always kept.

## Native phone hosts

**Android.** Compose each type from
[`VibrationEffect.Composition`](https://developer.android.com/reference/android/os/VibrationEffect.Composition)
primitives, passing `strength` as the primitive's scale. Check
`Vibrator.arePrimitivesSupported` first.

| Type | Primitive |
|---|---|
| `KEY`, `SELECTION` | `PRIMITIVE_TICK` |
| `KEY_WIDE`, `MODIFIER`, `TAP`, `RELEASE` | `PRIMITIVE_CLICK` (`RELEASE` at a lower scale) |
| `CLICK` | `PRIMITIVE_CLICK` |
| `FORCE` | `PRIMITIVE_THUD` |
| `MODE` | `PRIMITIVE_LOW_TICK` then `PRIMITIVE_CLICK` |

**iOS.** Core Haptics transient events take intensity and sharpness
directly, so pass the event's `strength` and `sharpness` through unchanged.

## Pressure

`kbd_touch_force` takes the contact's pressure, from 0 to 1:

- SDL hosts: `tfinger.pressure`. `kbd_sdl_handle` already forwards it.
- Android: `MotionEvent.getPressure`.
- iOS: `UITouch.force / maximumPossibleForce`.

The clickpad turns on only after the panel reports a value strictly between
0.02 and 0.98. Many panels report a constant 1.0 instead, and on those the
pad keeps tap to click.

The click thresholds follow the Mac's Light, Medium and Firm settings:

| Setting | Click at | Release at | Force click at |
|---|---|---|---|
| Light | 0.28 | 62% of click | 1.85× click (capped at 0.92) |
| Medium | 0.40 | 62% of click | 1.85× click (capped at 0.92) |
| Firm | 0.55 | 62% of click | 1.85× click (capped at 0.92) |

A force click emits `KBD_EV_FORCE`. The host decides what it does; Get Info,
or a Quick Look–style preview, would fit OS999.
