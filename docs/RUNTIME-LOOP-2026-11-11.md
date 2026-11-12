# Runtime Loop — 2026-11-11: Monkey Island 1, save, quit, relaunch, load

Goal: launch, play, save, quit, relaunch, load in `ospi-runtime` (not
Systemless, not a host port), with the deterministic probe, and compare the
loaded state with the saved state.

**Verdict: proven headlessly, with one limit (below). No runtime change was
needed.** Everything here is headless probe output (PPM frames, a file on
disk, exit status). None of it was seen in a real OS999 window, and sound and
real mouse input were not exercised.

Host: Apple Silicon Mac, macOS 27.2. Built with `BUILDFLEET_LOCAL=1 make probe`
(Ian's local-build override). Branch `worktree-agent-af7184835de8e4963`, merged
with `main` at `a9712ec` (internal-disk capacity work) before any run.

## Title and files

Ian's own copy, copied read-only from the MBP to
`~/os999-guest-software/from-mbp-20261111/Monkey Island/` (never modified; hashes
rechecked after the runs). Working copy in the scratch volume
`$S/vol/Monkey Island/` where `$S` =
`/private/tmp/claude-501/-Users-ian-projects-macos9port/69a5c518-f46b-437f-a1d7-97e2d6077560/scratchpad/mi1`.

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `Monkey Island.rsrc` (dated 17 Mar 1992) | 290,897 | `7fb6f377db93987454235f7c273c7131af5be81409852bb443cfd3cae2fda8e3` |
| `MONKEY1.000` (10 Feb 1993) | 9,455 | `030a8b7a3151ab0ae01bc2d1ba6dc68f09b62f0833f619e1234c56cdfaffe528` |
| `MONKEY1.001` (10 Feb 1993) | 4,805,154 | `3a96c5301177292c2cf7844ab9a045ab26f70eb4043e3a1f78b2619af3e64599` |
| `Monkey Island` (data fork) | 0 | `e3b0c442...b855` (empty) |

Exact game version string was not read; the dates above are all I have.
Layout: `$S/vol/Monkey Island/` (game), `$S/saves/` (the writable overlay passed as
`--saves`; the game's data directory is read-only).

## Procedure and commands

All runs are `build/ospi-runtime-probe` (fixed 60 Hz clock, 4 slices per tick):

```
build/ospi-runtime-probe --seconds N --script X.script --frames $S/fN --frame-every 1 \
  --saves $S/saves "$S/vol/Monkey Island/Monkey Island.rsrc"
```

Scripts (in `$S`, not in Git; times are guest seconds). `bar.script` is the
2026-11-06 route (copy of `.ralph/evidence/mi1-bar.script`): Escape at 22,
clicks through the dock dialogue, door click, SCUMM Bar reached by about 162.

Save run, `save5.script` = `bar.script` plus:

```
176 click 330 300          Guybrush walks to the middle of the bar
190 move 40 4 / 191 down / 192 move 60 38 / 193 up      Game menu > Save?
194.x key 51 8  (x10)      delete the default name "Game file"
195 key 1 83 / 37 76 / 31 79 / 17 84 ; 196 key 36 13   type SLOT, Return
206 move 40 4 / 207 down / 208 move 60 122 / 209 up     Game menu > Quit
212 click 292 189          "Are you sure you want to quit?" > Okay
```

Load run (fresh process, same `--saves`), `load2.script` = `bar.script` plus:

```
176 move 40 4 / 177 down / 178 move 60 26 / 179 up      Game menu > Open?
181 click 126 123          select SLOT in the list
183 click 391 221          Open
```

The menu bar appears when the pointer is at the top of the 640x480 guest
screen. F5 does nothing in this build; command keys are not reachable because
the probe sends no Command modifier, so menus are driven by the pointer.

## Results

1. **State reached.** Frame at 175 s shows the SCUMM Bar with Guybrush at the
   left door. After the click at 176 s he stands at about x=325, y=250 in the
   middle of the bar (frame `$S/f1/frame-0205s.ppm`). A distinguishable
   state, not a deep one: a position in one room, no inventory.
2. **Save dialog.** Game > Save? opens our native "Save Game File as..."
   (`src/mac_stdput.c`), prompt "Save in: OS999 HD", default name "Game file".
3. **Save file.** `$S/saves/SLOT`, 28,254 bytes, SHA-256
   `3ed38b6f0fc56447dcff62a683a98c757919d6d1a8a7dc645616b1c6345944b2`.
   Written by the guest into the `--saves` overlay. The game carried on
   (frame 205 s). Re-running the identical script reproduced the same bytes
   and hash.
4. **Quit.** Game > Quit shows "Are you sure you want to quit?"; Okay ends
   the run with `halt: Guest called ExitToShell.` and probe `result=0`
   (a clean guest exit, not a halt on a missing trap). The save was
   unchanged afterwards (same size and hash).
5. **Relaunch and load.** A new process on the same `--saves`: the game
   restarts from the intro, and at 183 s Game > Open? lists exactly `SLOT`
   (game data files are not listed). Select and Open. Frames from about 190 s
   show Guybrush in the middle of the bar, not at the door where a fresh
   launch leaves him.

## How the states were compared

Pixel diff of two 640x480 PPM frames (`$S/cmp.py`): saved-state frame
`$S/f1/frame-0205s.ppm` (taken after the save, before Quit) against the loaded
frame `$S/f2/frame-0200s.ppm`, region by region:

| Region | Saved vs loaded | Negative control: saved vs fresh launch before loading (`f2/frame-0176s.ppm`) |
| --- | --- | --- |
| Guybrush box x300-350, y210-320 | **0 of 5,500 pixels differ** | 1,539 differ |
| Verb panel y335-480 | 0 of 92,800 | 991 |
| Whole frame | 385 of 307,200 | 10,293 |

All 385 differing pixels sit in x578-639, y268-327 (an animated figure at the
right edge of the bar, not Guybrush), so those are animation phase, not state.
The frame 198 s vs 205 s comparison has 5,060 differing pixels, all
chandelier and the same right-edge figure (animation again).

## What is NOT shown

- Anything in a real OS999 window; sound; real mouse or keyboard input.
- Game state beyond Guybrush's position in one room. I did not read game
  variables or diff guest RAM, so "state matches" means the picture matches.
  A save with inventory or a later room was not tried.
- That the 28,254-byte file is complete or identical to what the original game
  would write; only that our runtime reloads it to the same picture.
- The loaded frame was compared by me against one saved frame; the 385-pixel
  residue is attributed to animation by location, not by a second load run.
- Monkey Island 2 and Castle of Dr. Brain were not run.
- Frames, scripts and game data stay in the scratch folder and are not in Git.

## Manual checklist for Ian (pending human verification)

Pending human verification. None of this was seen in a window by me.

1. Put `Monkey Island/` into the OS999 volume's Applications folder and open
   `Monkey Island` from the Finder.
2. Press Escape to skip the credits, click through to the SCUMM Bar (or use
   the saved route in the scripts above); confirm sound and mouse work.
3. Click to walk Guybrush to a spot away from the door; optionally talk to
   someone so the state is distinctive.
4. Game > Save? (pointer at the top of the game screen); name the slot; Save.
5. Game > Quit, then Okay; confirm the guest window closes.
6. Reopen Monkey Island from the Finder.
7. Game > Open?, pick the slot, Open; confirm Guybrush is where you left him
   and the room matches.
8. In the Finder, check the save exists in the volume's save overlay and note
   its size (expect about 28 KB for this simple state).
