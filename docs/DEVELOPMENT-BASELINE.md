# Historical development baseline

Moved intact from the checklist during desktop integration; newer evidence is recorded in the checklist and linked loop reports.

## Current baseline

The C11/SDL2 desktop routes extracted CODE-resource applications to our
development runtime; unsupported archive formats retain the older Systemless
fallback. Our runtime combines Musashi instruction execution with independently
implemented loading and partial Macintosh API services. User screenshots now
show original MI1 running in a guest window inside the desktop and progressing
through scenes; mouse controls and effects respond. Ian reports intermittent
ticking and missing or delayed music in the SCUMM Bar and Sword Master scenes.
The current live session has no audio capture, trace, or FPS measurement. Guest
window drag containment, sustained play, saves, and full acceleration remain
unverified. Networking remains unimplemented. OS999 can open a host movie
window and play uncompressed 32-bit raw video from a classic movie container,
including play, pause, and the end of the movie. Guest QuickTime, audio-track
playback, compressed codecs, and the YouTube hardware path remain open gates.
See [the first implementation loop](RUNTIME-LOOP-2026-11-04.md) for evidence,
limitations, exact artifacts, and the next reproducible steps.

The first OS999 intro preview looked low quality to Ian: one quick rotation
followed by the desktop. A subsequent frame still showed faceting and a detached
leaf. Ian said the software-rendered Granny Smith mesh looked much better, then
requested 25% of its previous size. The fruit is now scaled to one quarter,
rebuilt, and visible in a disposable-volume preview; final user acceptance is
pending. Ian supplied a boot-screen mockup with a framed display, pixel-art
apple, OS999 wordmark, and “Starting Up…” treatment. Use it as a visual target
while preserving truthful readiness and the previously requested short startup.
The small fruit mark is visible in the menu bar, but its click/menu behavior
has not yet been checked.

Ian also supplied `build resources from ian/Desktop1.png` through `Desktop4.png`
as OS999 desktop references. `Desktop1` and `Desktop2` show the blue desktop,
disk and Finder windows, Granny Smith menu mark, and About panel; `Desktop3` and
`Desktop4` show a future app menu and Chromium window. The `.png` files are
actually JPEG mockups. `apple_logo.png` is a pixel-art Granny Smith reference,
not an Apple logo asset to ship. Treat all of these as visual references until
asset ownership/provenance is recorded; mockups do not prove app functionality.
The menu item and About This Mac text identify Ian Trimble and show
`remnantsecurity.com`. Ian clarified that the site is where he houses all
the things he creates publicly. His screenshot showed the About window too
large and exposing the temporary volume path. Current source describes the
site's role, removes that host path, and centers a smaller About window; a fresh
packaged visual review remains due.

On 4 November 2026, `BUILDFLEET_LOCAL=1 make test` passed the complete local C
suite. `BUILDFLEET_LOCAL=1 make runtime` reported no work. REA CLI 6.8.0 matches
the checked npm release; its Codex registration is aligned, but no analysis
provider is configured and REA tools are not connected in this agent session.
On 5 November the shared REA skill was refreshed to version 49. Earlier REA
ZIP inventories succeeded for Monkey Island 2 and Castle of Dr. Brain; their
Evidence IDs are retained in the loop record. CPU traces are separate evidence.
