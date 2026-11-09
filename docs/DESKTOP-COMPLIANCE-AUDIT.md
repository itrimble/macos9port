# OS999 desktop compliance audit

## What I audited

I audited the desktop as it exists in local `main` at `a14d464` and compared it
with the 163-item clean-room desktop specification in
`desktop/MAC-OS-9-DESKTOP-REQUIREMENTS.md`. That specification contains 119
Must items, 40 Should items, and four period-reference items. Its evidence set
contains 107 original web screenshots: 93 Mac OS 9.0 images from GUIdebook and
14 Mac OS 9.2.2 images from ToastyTech. The downloaded screenshots are ignored
research material. They are not assets we can ship.

I inspected source, tests, current documentation, Git history, worktree state,
and the active feature branches. I did not build, run tests, launch the GUI, or
make a new visual comparison in this audit. Historical Build 8 evidence remains
historical evidence. It does not automatically cover later feature branches or
the current dirty root checkout.

At the start of this pass:

- local `main` was 39 commits ahead of `origin/main` at `a14d464`;
- the root checkout had unrelated dirty dashboard work and many nested,
  intentionally untracked worktrees;
- the Font Book worktree was on `feature/font-book`, also at `a14d464`;
- the last complete-gate source commit was `f19ead6`, with the exact tested tree
  and artifact recorded in `docs/OS999-MASTER-TRACKER.md`;
- no remote push was part of this work.

## The short answer

The desktop is no longer a mock shell. It has a substantial Finder model,
global menus, persisted spatial windows, original assets, app stacking, a boot
presentation, guest-window composition, a browser, a terminal, media windows,
and unusually good path-boundary tests.

It is not yet a complete Mac OS 9-style desktop by the new specification.

The largest desktop gaps are not cosmetic. There is no user/login model, no
Control Strip, no list disclosure rows, no cancellable file-operation progress,
no standard Open/Save system, no real logout/sleep/shutdown lifecycle, no
keyboard-complete accessibility pass, and no three-resolution end-to-end
acceptance record. The menu layout also misses the explicit 9.2.2 `Window`
menu position. Some menu entries exist even though their backend is deliberately
disabled. That is better than pretending they work, but it is not completion.

## Evidence labels used below

- **Integrated** means production source exists in `main` at `a14d464`.
- **Automated evidence** means focused tests exist, or the Build 8 gate reports
  a pass. I did not rerun them here.
- **Visual/manual open** means the source or logic exists, but the required
  real-window, listening, device, or human acceptance is absent or stale.
- **Sibling-only** means implementation exists on another branch/worktree and
  is not in `main` or the Font Book branch.
- **Missing** means I found no implementation that satisfies the requirement.

## Requirements audit by desktop area

| Area | Integrated and automated evidence | Still open or missing | Status |
| --- | --- | --- | --- |
| Boot (`BOOT-*`) | `src/os999_startup.c` and `src/main.c` implement deterministic phases, a settled desktop snapshot, reduced motion, failure/retry handling, and a persisted per-volume preference. `tests/test_os999_startup.c` covers the state machine. | The progress presentation is time/state driven rather than a measured inventory of actual startup services. Extension/service progress is not integrated. Fresh audio and one-reveal visual acceptance are open. | Integrated baseline; visual/manual open |
| Login and users (`LOGIN-*`) | Credentials are not part of the current desktop state because there is no login system. | Setup Assistant, user chooser, password flow, per-user preferences, user-state isolation, and logout-to-login are absent. | Missing |
| Desktop canvas (`DESK-*`) | `include/ospi.h` fixes a 1024×768 logical framebuffer with a 20-pixel menu reservation. Wallpaper decoding rejects symlinks and falls back safely. Main includes 59 wallpapers, 16 color palettes, saved theme/wallpaper choices, saved desktop positions, and original icon/sound assets. | `src/main.c` always renders a 1024×768 texture. A host window resize only scales that fixed canvas; it does not prove crisp native 640×480 or 800×600 layouts. HiDPI icon/backing-store rendering and the full contrast/color-blind matrix remain open. | Integrated at one logical size; required resolution QA missing |
| Global menus (`MENU-*`) | `src/menu.c`, `menu_navigation.c`, `menu_hierarchy.c`, and `menu_paint.c` implement one global bar, scoped enablement, disabled items, checkmarks, shortcuts, submenus, recent items, application switching, and a real clock. Focused menu tests exist. | The top-level order is currently `999`, File, Edit, View, Appearance, Special, Help, then the active-app menu. It does not expose the required 9.2.2 `Window` menu between View and Special. Eject, Erase, and Sleep are present but disabled. Logout is absent. Menu clipping/tracking still needs a real pointer-drag acceptance pass. | Strong integrated core; target layout and lifecycle incomplete |
| Desktop objects (`ICON-*`) | Original built-in icon mapping, runtime guest icons, disk/Trash placement, marquee and modifier selection, spatial drag, position persistence, labels, aliases, arrange/cleanup, and system-icon movement all have production code and focused tests. | Additional mounted-volume/media states are not a complete desktop object model. Drag target highlighting and failure visuals are not proven in a fresh window. Native HiDPI icon rendering remains absent. | Mostly integrated; visual/device acceptance open |
| Window chrome (`WIN-*`) | `src/geom.c`, `paint.c`, and `input.c` provide striped active titles, inactive titles, close/shade/zoom/grow controls, vertical and horizontal tracks, status text using real free-space data, content clipping, z-order, dragging, resizing, WindowShade, and pop-up folders. Tests cover layouts and interactions. | Scrollbar input handles arrow stepping and wheels, but full thumb dragging and page-region behavior are not established by the inspected code. Finder proxy icons are absent from title bars. A real multi-resolution clipping pass is still required. | Broadly integrated; several interaction details open |
| Finder views (`VIEW-*`) | Icon, Button, and List views exist. List headers expose Name/Date/Size/Kind, sorting includes created date and labels, columns resize, selections survive refresh, folder state persists atomically, and keyboard navigation includes arrows, Home/End, paging, and type-to-select. | List disclosure triangles and inline folder expansion are absent. View Options is an informational dialog, not a configuration panel for icon size, label position, and grid spacing. Read-only behavior is not presented as a complete user-facing state. | Useful implementation; two central Finder behaviors missing |
| File operations (`FILE-*`) | New Folder, open dispatch, inline/dialog rename, Get Info, duplicate, alias, put-away, Trash, labels, recent items, bounded recursive copy, path containment, exclusive destination creation, `.rsrc` sidecar handling, rollback cleanup, refresh, and an identity-bound Undo journal are implemented. macOS uses `fcopyfile(..., COPYFILE_ALL)` for copied metadata. Focused capacity, symlink, collision, resource-fork, and stale-identity tests exist. | There is no general Cut/Paste file workflow. Long copy/move/install/delete operations have no cancellable byte/item progress UI. Name collisions are resolved by generating another name or returning an error, not by the specified source/destination decision dialog. Cross-volume semantics and complete Finder metadata coverage need dedicated fixtures. | Strong safety core; operation UX incomplete |
| Drag, Trash, disks, servers (`DROP-*`) | In-volume move, Option-copy, alias targets, spring folders, Trash origin records, put-away, Empty Trash confirmation, and undo are present. | Multiple mounted volumes are not represented by the desktop Session model, so cross-volume defaults, removable-media eject, server disconnect, busy-file refusal, and genuine printer/server objects are missing. Trash fullness artwork/state needs fresh visual proof. | Single-volume behavior integrated; device/server behavior missing |
| Applications (`APP-*`) | Finder, guest, media, and browser applications have explicit focus/stack state. Command-Tab, reverse cycling, Hide/Hide Others/Show All, app-scoped commands, guest clipping/composition, modal checks, and graceful guest close routing have tests. | Force Quit is absent. Full menu ownership for arbitrary classic applications remains limited by the guest bridge. Sustained guest interaction and performance remain separate acceptance gates. | Strong host-app model; guest/full lifecycle open |
| Control Strip (`STRIP-*`) | No qualifying implementation was found in `main`. | The strip, collapse state, overflow, real modules, reordering, persistence, and synchronized panel state are all missing. | Missing |
| Dialogs and progress (`DLG-*`) | OS999 has framed modal dialogs, OK/Cancel handling, a default action, rename/find/settings dialogs, About, Clipboard, printing hooks, and headless interaction tests. | There is no general standard Open/Save dialog, color picker, cancellable progress dialog, or full tab-order model. `About This Mac` reports real architecture and RAM but not the required per-application memory list. Small-screen, long-translation, and high-contrast acceptance is open. | Basic dialog system integrated; system dialog suite missing |
| Appearance and control panels (`PREF-*`) | Main exposes Control Panels and an Appearance overlay with Desktop and Themes. Wallpaper/theme changes persist and avoid changing guest pixels. Finder spring-folder preferences exist. | The required panel set is not integrated in `main`. Appearance lacks Fonts, Sound, Options, system-font choices, and coordinated theme packages. A Control Panels Finder folder is absent. Real monitor/mouse/keyboard/date/energy/user backends are not in the current branch. | Minimal integrated baseline; richer system is sibling-only |
| Accessibility and international (`A11Y-*`) | Menus and Finder objects have substantial keyboard navigation. FreeType and UTF-8 host text paths exist, and the terminal has a separate Unicode/Nerd-font renderer. | Keyboard focus is not consistently distinct from selection. Sticky/Slow/Mouse Keys, magnification/inversion, Key Caps, locale formatting, shaping/emoji, translation expansion tests, and the complete contrast/color-blind audit are absent. Fixed `OSPI_NAME`/`OSPI_PATH` buffers are bounds checked, but that is not a Unicode filename compatibility result. | Partial keyboard support; broad accessibility/i18n missing |
| Search, Help, network, utilities (`SYS-*`) | Filename search, reveal/open, bounded traversal, recent items, static Help topics, host HTTPS, a Tailscale helper, browser integration, terminal, media player, and host movie window are present. | Search is synchronous and filename-only rather than cancellable metadata/content search. Help is not searchable or contextual. There is no real Finder Network Browser. Browser, guest networking, printing, classic QuickTime, and media remain separate acceptance tracks exactly as the spec requires. | Useful utilities; period system services incomplete |
| End of session (`END-*`) | Restart has an explicit dialog, but it restarts the desktop view only. Shutdown sets the desktop process quit flag. Session destruction atomically saves several preferences and Finder state. | Logout, real host-policy Sleep/Restart/Shut Down, dirty-document/transfer arbitration, failure-visible final flush, login return, and a powered-off screen are absent. The current menu should not be read as backend completion. | Mostly missing |
| Evidence route (`QA-*` and section 17) | The project has extensive headless tests and the Build 8 complete-gate receipt. The requirements research has a source manifest and a strict rights boundary. | The full 12-step route has not been run at 640×480, 800×600, and 1024×768 on one immutable candidate. There is no fresh, one-window-at-a-time visual comparison set with dated italic captions. Keyboard-only, read-only, interrupted-operation, long-label, and corrupt-state evidence is incomplete. | Gate design is sound; end-to-end acceptance missing |

## Work that exists only in sibling branches

These branches are real source, not completion claims. None was rebuilt or
visually accepted during this audit.

| Branch | What it adds | Integration warning |
| --- | --- | --- |
| `feature/boot-startup-sequence` at `5a3ef78` | A much larger boot sequence, sprite work, progress-bar tests, and development records. | It changes 12 files and overlaps the shared Make/test manifests. Reconcile it with `main` rather than replacing the current startup wholesale. |
| `feature/control-panels` at `327982e` | Eight live modules, a module registry/plugin model, atomic preferences, a windowed panel container, and subsystem hooks. | It changes 55 files. It overlaps `include/ospi.h`, session/input/paint/menu/volume code, and several runtime files. This should become the desktop preference foundation before Font Book is wired as a module. |
| `feature/extensions-manager` at `7f61f70` | Extension scanning, state, boot/runtime hooks, and an Extensions control-panel module. | It independently carries 32 of the same files as `feature/control-panels`. The branches are not in an ancestor relationship. A raw merge risks choosing two versions of the same copied framework. Port the extension delta onto the selected Control Panels base. |
| `feature/font-book` | The isolated branch assigned to the current Font Book work. | It began at `a14d464`. Expect conflicts in Makefile/core manifests, `ospi.h`, session, input, paint, menu, volume, typeface, and Appearance/Control Panels integration. Keep its parser/catalog/registry/UI modules narrow so they can be ported. |
| `feature/storage-installers` at `5e2b3b1` | Disposable image volumes, archive install work, application probing, and storage wiring. | It overlaps Control Panels in core manifests, `ospi.h`, install, and native launch. Treat this as storage behavior, not as Finder multi-volume UI completion. |
| `feature/memory-integration` at `2140db7` | Runtime memory/heap/resource work plus several compatibility managers. | It overlaps Control Panels and Extensions in runtime dispatch and memory/system files. Keep it out of the first desktop-shell merge unless a panel depends on a specific stable API. |
| `feature/bluetooth-stack` at `537e274` | A large Bluetooth/PAN stack with its own tests and plans. | It is not desktop networking acceptance. Integrate only after the Network panel has a capability model that can show unavailable hardware honestly. |

The nested branches all descend from the `feature/font-book` starting commit,
but Control Panels and Extensions do not descend from one another. That detail
matters. Their shared files are parallel copies, not a safe linear series.

## Integration order I recommend

### 0. Freeze the evidence baseline

First checkpoint the unrelated dirty dashboard work or move it to its owning
worktree. Record the exact main commit, dirty-tree hash, and active worktree
owners. Do not use the Build 8 receipt to bless a newly assembled desktop.

### 1. Land the desktop foundations with the smallest conflict surface

Reconcile the boot branch first because its production overlap is comparatively
small. Then run its focused tests and a real cold-start/failure/retry capture.
Keep startup sound listening separate.

Next choose `feature/control-panels` as the one panel framework. Rebase or port
it onto current main. Do not merge the copied Control Panels framework from the
Extensions branch a second time.

### 2. Port Font Book onto that panel foundation

Keep Font Book's scanner, safe installer, activation registry, metadata parser,
and preview renderer independent of the control-panel container. Register the
utility after the Control Panels APIs are stable. The resulting desktop needs
both launch paths: a Finder-visible utility and a Control Panels/Appearance
entry where appropriate.

Use only redistributable fonts with source, version, license, hash, and intended
coverage recorded. Never add Apple's Chicago, Charcoal, Geneva, Monaco, or
Apple-owned suitcases. Deactivation must never remove the last usable UI face.

### 3. Finish the Finder contract before adding more services

Implement the missing disclosure-row model, real View Options, scrollbar thumb
drag/page regions, collision decisions, and cancellable file-operation progress.
Add a disposable second-volume model and resource/Finder-metadata fixtures.
This closes more daily desktop gaps than another decorative panel would.

Correct the top-level menu layout for the 9.2.2 target. Add a real Window menu
between View and Special, while keeping application switching at the right.
Remove or visibly explain lifecycle/device commands that have no backend.

### 4. Add Control Strip and coherent preferences

Build Control Strip modules against the same capability/state interfaces used
by Control Panels. Sound, display, networking, power, and media must reflect the
actual backend. No dead icons. Port Extensions Manager only after rebasing its
delta onto this shared framework.

### 5. Add login, lifecycle, accessibility, and i18n deliberately

User isolation changes every preference path. Design it before sprinkling user
IDs through existing files. Then implement login/logout and real session-end
arbitration together.

Create one keyboard-focus model for menus, Finder, panels, dialogs, and
utilities. Add locale formatting and Unicode filename tests at the volume
boundary. Treat shaping, emoji, classic guest encodings, and host filenames as
separate capabilities.

### 6. Run the actual desktop acceptance route

On a fresh copied volume, validate 640×480, 800×600, and 1024×768 from cold boot
through shutdown. Use one immutable candidate. Record headless tests, packaged
launch, real-window visuals, input, listening, performance, device behavior,
and Ian's acceptance separately.

For every published screenshot, capture one real window at a time and add an
italic caption that says exactly what it shows and when it was captured.

## The next concrete desktop milestone

I would call the next milestone **Desktop Foundation 1**, not “Mac OS 9
complete.” Its exit criteria should be narrow and testable:

1. the richer boot sequence is reconciled and visually accepted at three
   logical resolutions;
2. one Control Panels framework is integrated, with no copied second framework;
3. Font Book is registered there and in Applications, with licensed-font
   provenance and safe activation/install tests;
4. the 9.2.2 menu order includes a real Window menu;
5. list disclosure rows, View Options, scrollbar thumb/page input, and file
   progress/cancel are implemented;
6. a disposable second volume proves copy/move/eject semantics and metadata
   preservation;
7. the exact candidate passes `BUILDFLEET_LOCAL=1 make test` and a separate
   real-window route without borrowing Build 8's receipt.

That milestone would make the desktop feel materially more complete. It would
still leave users/login, Control Strip, lifecycle, accessibility, and the full
end-to-end acceptance route open. That is the honest boundary.
