# Boot startup recovery checkpoint

Recovered `feature/boot-startup-sequence` at `5a3ef78` into a separate
`recovery/boot-startup` worktree. The original branch had one unfinished edit:
a stronger progress-edge test sampled every 50 ms instead of 100 ms, requiring
the lit edge to advance on every filling sample and reach the far end.

The copied test passes against the existing renderer: 183 bar checks and
61 startup checks, zero failures. No renderer changes were necessary. Original
dirty source and the running producer app were preserved. No new GUI acceptance
or screenshot is claimed; these are headless framebuffer tests.

Validation: `BUILDFLEET_LOCAL=1 make -j2 test all` passed with the private
SDL2 pkg-config path. Desktop and runtime binaries built. Existing linker
common-section alignment warnings remain; there were no test failures.
Log: `build-logs/recovery-validation.log` (ignored local artifact). Genuine SDL2
2.32.10 is kept privately pinned because the repository records a synthetic
keyboard regression in the system SDL2 compatibility package.
