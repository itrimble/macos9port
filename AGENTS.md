# Repository Guidelines

## Project Structure & Module Organization

OS Pi is a clean-room, classic-Mac-style userspace desktop, not an operating system or emulator. `src/` contains rendering, input, sessions, volume management, archive installation, and external Systemless launching. Public interfaces live in `include/`; `src/session_int.h` holds session internals. Headless checks live in `tests/test_ospi.c`. `fonts/` contains ChicagoFLF and its license; `macos/Info.plist` supplies app metadata. Generated binaries and bundles belong in `build/`.

## Build, Test, and Development Commands

Use a C11 compiler, SDL2, and `pkg-config`; install FreeType for outline fonts. Classic-program launching requires the separately installed `systemless` executable.

- `make`: build `build/ospi`.
- `make test`: compile and run headless checks without SDL.
- `make app`: create and ad-hoc sign `build/OS Pi.app` on macOS. Quit the running app first; this target replaces its bundle.
- `make run`: build and open the macOS app, or launch Linux with `./volume`.
- `./build/ospi --volume ./volume`: launch against an explicit development volume.

Read `~/projects/CLAUDE.md` and this repository's `CLAUDE.md` before working. Follow their fleet scheduling requirements for resource-intensive builds; check tool versions against official sources before use.

## Coding Style & Naming Conventions

Match existing C: four-space indentation, same-line opening braces, `snake_case` functions and variables, PascalCase typedefs, and uppercase constants such as `OSPI_PATH`. Keep every file below 500 lines. Validate paths and external input at boundaries. The Makefile enables `-Wall -Wextra`; no dedicated formatter or linter is configured.

## Testing Guidelines

Extend the existing `CHECK`-based C harness in `tests/test_ospi.c`; use descriptive `snake_case` helpers. Run `make test` after code changes and verify the build before committing. CI runs headless checks on macOS with FreeType. No numerical coverage threshold is configured. Report GUI interactions, classic-program compatibility, and Linux/device execution separately from headless results.

## Commit & Pull Request Guidelines

History uses short imperative subjects, such as “Install Garden archives and open them with Systemless.” Keep changes focused. PRs should explain behavior, validation commands, and unverified platforms; include real window screenshots for visual changes, with italic captions identifying capture time. Commit or push only when requested; never add `Co-Authored-By` trailers.

## Clean-Room & Data Boundaries

Never commit secrets, `.env` files, build outputs, runtime volumes, or Garden downloads. Do not vendor Apple ROMs, system files, proprietary fonts, copied documentation, or Systemless source. Preserve unrelated changes and user data.
