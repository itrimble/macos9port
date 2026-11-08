# Recursive repository inventory — 6 November 2026

> Historical record copied from main on 7 November 2026. Revisions, paths,
> process observations and acceptance statements below describe the dated audit.
> See [the master tracker](OS999-MASTER-TRACKER.md) for subsequent integration work.

Snapshot captured at 2026-11-06T14:02:19-06:00.

Filesystem traversal included hidden and Git-ignored source directories and nested
worktrees. It did not follow symlinks. Generated build/dependency/cache directories
and runtime volumes were pruned; `.env` files were not read. File counts are
inventory counts, not code quality, feature completion, or test coverage.

## Directory inventory

| Root entry | Files inventoried recursively |
| --- | ---: |
| `(root files)` | 8 |
| `.claude` | 1720 |
| `.github` | 1 |
| `.ralph` | 353 |
| `assets` | 1499 |
| `browser` | 21 |
| `build resources from ian` | 16 |
| `docs` | 4 |
| `fonts` | 2 |
| `include` | 37 |
| `ios_android` | 1848 |
| `keyboard` | 42 |
| `macos` | 1 |
| `quake` | 1413 |
| `scripts` | 5 |
| `src` | 94 |
| `terminal` | 1707 |
| `tests` | 88 |
| `videosupport` | 1710 |

Pruned directory names: `.cxx`, `.git`, `.gradle`, `__pycache__`, `build`.

## Documentation inventory

These paths were discovered recursively. Historical runtime logs, copied guides,
third-party compiler descriptions, and supplied reference prose retain their
original meaning; discovery does not endorse their claims or linked sources.
The accompanying status document identifies evidence actually used.

- `.claude/worktrees/l04-writable-files/AGENTS.md`
- `.claude/worktrees/l04-writable-files/CLAUDE.md`
- `.claude/worktrees/l04-writable-files/README.md`
- `.claude/worktrees/l04-writable-files/assets/README.md`
- `.claude/worktrees/l04-writable-files/assets/icons/README.md`
- `.claude/worktrees/l04-writable-files/docs/DEVELOPMENT-CHECKLIST.md`
- `.claude/worktrees/l04-writable-files/docs/RUNTIME-LOOP-2026-11-04-PART2.md`
- `.claude/worktrees/l04-writable-files/docs/RUNTIME-LOOP-2026-11-04-PART3.md`
- `.claude/worktrees/l04-writable-files/docs/RUNTIME-LOOP-2026-11-04.md`
- `.claude/worktrees/l04-writable-files/handover.md`
- `.ralph/acceptance.md`
- `.ralph/progress.md`
- `.ralph/state.md`
- `AGENTS.md`
- `CLAUDE.md`
- `README.md`
- `assets/README.md`
- `assets/icons/README.md`
- `browser/README.md`
- `build resources from ian/Work on code for the terminal integration, here is.md`
- `build resources from ian/codex-handoff-themes.md`
- `build resources from ian/what file types did macos 9 support ? be extensive.md`
- `docs/DEVELOPMENT-CHECKLIST.md`
- `docs/RUNTIME-LOOP-2026-11-04-PART2.md`
- `docs/RUNTIME-LOOP-2026-11-04-PART3.md`
- `docs/RUNTIME-LOOP-2026-11-04.md`
- `handover.md`
- `ios_android/AGENTS.md`
- `ios_android/CLAUDE.md`
- `ios_android/README.md`
- `ios_android/assets/README.md`
- `ios_android/assets/icons/README.md`
- `ios_android/docs/DEVELOPMENT-CHECKLIST.md`
- `ios_android/docs/MOBILE.md`
- `ios_android/docs/RUNTIME-LOOP-2026-11-04-PART2.md`
- `ios_android/docs/RUNTIME-LOOP-2026-11-04-PART3.md`
- `ios_android/docs/RUNTIME-LOOP-2026-11-04.md`
- `ios_android/handover.md`
- `ios_android/mobile/README.md`
- `keyboard/README.md`
- `keyboard/docs/HAPTICS.md`
- `keyboard/docs/INTEGRATION.md`
- `quake/lcc/src/alpha.md`
- `quake/lcc/src/dagcheck.md`
- `quake/lcc/src/mips.md`
- `quake/lcc/src/sparc.md`
- `quake/lcc/src/x86.md`
- `quake/lcc/src/x86linux.md`
- `terminal/AGENTS.md`
- `terminal/CLAUDE.md`
- `terminal/README.md`
- `terminal/assets/README.md`
- `terminal/assets/icons/README.md`
- `terminal/docs/DEVELOPMENT-CHECKLIST.md`
- `terminal/docs/RUNTIME-LOOP-2026-11-04-PART2.md`
- `terminal/docs/RUNTIME-LOOP-2026-11-04-PART3.md`
- `terminal/docs/RUNTIME-LOOP-2026-11-04.md`
- `terminal/handover.md`
- `videosupport/AGENTS.md`
- `videosupport/CLAUDE.md`
- `videosupport/README.md`
- `videosupport/assets/README.md`
- `videosupport/assets/icons/README.md`
- `videosupport/docs/DEVELOPMENT-CHECKLIST.md`
- `videosupport/docs/RUNTIME-LOOP-2026-11-04-PART2.md`
- `videosupport/docs/RUNTIME-LOOP-2026-11-04-PART3.md`
- `videosupport/docs/RUNTIME-LOOP-2026-11-04.md`
- `videosupport/handover.md`

## Registered worktrees

Status counts include staged, unstaged, and untracked entries from Git; untracked
directories may be summarized as one entry. Dirty files can overlap changes
already integrated elsewhere. An ancestor HEAD does not make a dirty tree obsolete.
Do not delete or reset these worktrees based on this table.

| Path | Branch or detached | HEAD | Status entries |
| --- | --- | --- | ---: |
| `/Users/ian/projects/macos9port` | `main` | `68f61ff` | 38 |
| `/private/tmp/os999-appearance-panel` | `detached` | `1c2fbad` | 4 |
| `/private/tmp/os999-boot-worktree` | `detached` | `dce5e51` | 49 |
| `/private/tmp/os999-commands-worktree` | `detached` | `38b5b15` | 4 |
| `/private/tmp/os999-context-controls` | `codex/os999-context-controls` | `1c2fbad` | 19 |
| `/private/tmp/os999-context-worktree` | `detached` | `1c2fbad` | 3 |
| `/private/tmp/os999-folder-navigation` | `detached` | `9f719c2` | 4 |
| `/private/tmp/os999-folder-view` | `detached` | `38b5b15` | 8 |
| `/private/tmp/os999-icon-controls` | `codex/os999-icon-controls` | `7974847` | 13 |
| `/private/tmp/os999-icon-map-worktree` | `detached` | `7974847` | 3 |
| `/private/tmp/os999-local-themes` | `codex/os999-local-themes` | `18be4c5` | 16 |
| `/private/tmp/os999-menu-actions` | `codex/os999-menu-actions` | `38b5b15` | 30 |
| `/private/tmp/os999-menu-followup` | `codex/os999-menu-followup` | `9f719c2` | 15 |
| `/private/tmp/os999-menu-navigation` | `detached` | `38b5b15` | 3 |
| `/private/tmp/os999-panel-context` | `detached` | `7974847` | 4 |
| `/private/tmp/os999-test-suites` | `test/os999-test-suites` | `dfcba3b` | 0 |
| `/private/tmp/os999-theme-panel` | `detached` | `18be4c5` | 5 |
| `/private/tmp/os999-theme-worktree` | `detached` | `18be4c5` | 3 |
| `/private/tmp/os999-wallpaper-integration` | `codex/os999-wallpaper-integration` | `5f18aa8` | 14 |
| `/private/tmp/ospi-cpu-loop` | `detached` | `552a235` | 58 |
| `/private/tmp/ospi-desktop-loop` | `detached` | `552a235` | 55 |
| `/private/tmp/ospi-resource-loop` | `detached` | `552a235` | 46 |
| `/private/tmp/ospi-sit-agent-1793922253` | `detached` | `06b71a4` | 9 |
| `/Users/ian/projects/macos9port-browser-embed` | `browser-embed` | `8d6a6f4` | 1 |
| `/Users/ian/projects/macos9port-browser-security` | `codex/browser-network-integration` | `8d6a6f4` | 35 |
| `/Users/ian/projects/macos9port-desktop-loop` | `detached` | `68f61ff` | 30 |
| `/Users/ian/projects/macos9port-networking` | `codex/modern-networking` | `38b5b15` | 12 |
| `/Users/ian/projects/macos9port-sound` | `os999-sound` | `9fb1b54` | 4 |
| `/Users/ian/projects/macos9port-storage` | `os999-storage` | `d7de608` | 0 |
| `/Users/ian/projects/macos9port-v1.1` | `release/1.1` | `7583642` | 0 |
| `/Users/ian/projects/macos9port/.claude/worktrees/l04-writable-files` | `worktree-l04-writable-files (locked)` | `68f61ff` | 6 |
| `/Users/ian/projects/macos9port/ios_android` | `os999-mobile` | `38b5b15` | 3 |
| `/Users/ian/projects/macos9port/terminal` | `os999-terminal` | `38b5b15` | 16 |
| `/Users/ian/projects/macos9port/videosupport` | `videosupport` | `38b5b15` | 17 |
