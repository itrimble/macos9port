# Font Book 999: audit, architecture, and evidence

Date: 7 November 2026
Branch: `feature/font-book`
Baseline: `a14d46422264d56022183a6b6068b0dc52b1fbe5`

## What I found before changing code

I recursively swept `~/projects/macos9port` before editing this worktree. The
scan covered 222,921 filesystem entries outside Git object databases. That
number includes generated builds, dependency trees, and sibling worktrees, so
I separated canonical assets from copies instead of treating every `.ttf` as
ours.

The canonical font inventory was small:

| Path | Face | Bytes | License boundary |
| --- | --- | ---: | --- |
| `fonts/ChicagoFLF.ttf` | ChicagoFLF Regular | 50,472 | Public-domain revival; not Apple's Chicago |
| `fonts/terminal/MesloLGSNerdFontMono-Regular.ttf` | MesloLGS Nerd Font Mono Regular | 2,944,312 | Apache/Nerd Fonts notices beside the files |
| `fonts/terminal/MesloLGSNerdFontMono-Bold.ttf` | MesloLGS Nerd Font Mono Bold | 2,961,420 | Apache/Nerd Fonts notices beside the files |
| `assets/dashboard/plex-0.ttf` | IBM Plex Sans Regular | 205,252 | SIL OFL 1.1 |
| `assets/dashboard/plex-1.ttf` | IBM Plex Sans Medium | 205,292 | SIL OFL 1.1 |
| `assets/dashboard/plex-2.ttf` | IBM Plex Sans SemiBold | 205,284 | SIL OFL 1.1 |
| `assets/dashboard/plex-3.ttf` | IBM Plex Sans Bold | 205,220 | SIL OFL 1.1 |

There was no canonical OpenType file, suitcase, `.dfont`, or font resource
fixture. The two generated `System Folder/Fonts/About Fonts` files were text
placeholders. Nine `.rsrc` files elsewhere were application or game artifacts,
not redistributable fonts. Copies in `build/`, mobile dependency builds,
ZCode, and sibling worktrees were not imported.

The useful architecture already existed:

- `src/typeface.c` used FreeType for TrueType, OpenType, collections, Type 1,
  and `.dfont` files, but it was one-shot, global, and already 499 lines.
- `src/mac_resource.c` already offered bounded raw resource-fork and
  AppleSingle/AppleDouble parsing with enumeration.
- Finder already recognized font file types and already shipped clean-room
  `font_file` and `fonts_folder` icons.
- `src/volume.c` already created the simulated `System Folder/Fonts` folder.
- The current branch had no Font Book stub. The separate Control Panels branch
  has a newer container framework, but merging that unrelated work here would
  break the isolated-worktree rule.

## What this branch builds

Font Book 999 is a real OS999 utility window, not an Appearance modal. It uses
the desktop's ordinary title bar, close, shade, zoom, grow, stacking, and menu
paths. The content is deliberately modern inside classic chrome:

1. Collections: All Fonts, System Folder, OS999 Fonts, Active Fonts, Problems.
2. Font Family: sorted metadata entries, including every face in a collection.
3. Preview: family, style, format, activation state, validation result, and a
   FreeType-rendered live sample.

The seeded launcher lives at `Applications/Utilities/Font Book 999`. The same
utility is available from OS999 → Control Panels → Font Book 999. This is not a
claim that Mac OS 9 shipped Font Book. It did not. This is an OS999 utility that
uses the requested classic desktop language.

The implementation is split so every file stays below 500 lines:

- `font_book_parse.c`: bounded metadata and integrity inspection.
- `font_book_catalog.c`: deterministic recursive discovery and installation.
- `font_book_registry.c`: atomic disabled-font registry.
- `font_book_session.c`: volume/session ownership and renderer filter.
- `font_book_preview.c`: isolated FreeType preview drawing.
- `font_book_window.c`: classic three-pane window and input.
- `typeface_discovery.c`: extracted discovery code that creates room for safe
  renderer reloads without growing `typeface.c` beyond the repository limit.

## Safety model

Font Book never installs fonts into macOS. It mutates only the simulated OS999
volume supplied to the Session.

- Candidates must be regular, non-symlink files between 16 bytes and 64 MiB.
- TrueType/OpenType/collection/Type 1 validation goes through FreeType.
- Raw forks and AppleSingle/AppleDouble containers go through the existing
  bounded resource parser. Font Book recognizes `sfnt`, `FOND`, `FONT`, and
  `NFNT` resources.
- Invalid named font files remain visible in Problems but cannot activate or
  install.
- Installation validates first, refuses an existing destination, copies to a
  unique same-directory temporary file, flushes it, and renames it atomically.
- Activation is file-level. A `.ttc` may expose several families, but disabling
  one entry disables that physical font file consistently.
- The disabled registry is stored at
  `System Folder/Preferences/Font Book 999 Registry`. It is written through a
  temporary file, `fsync`, and rename. Symlinked or non-regular registries fail.
- Font Book refuses to deactivate the last active previewable managed font.
- The desktop typeface reload is transactional: it constructs replacement
  faces first and retains the old face if no usable primary can be loaded.
- Startup branding intentionally retains the bundled public-domain ChicagoFLF
  face even if ordinary desktop activation changes. That keeps the clean-room
  startup identity stable.

## New clean-room coverage pack

`fonts/system/` adds 15 official Noto files, their 15 exact upstream SIL OFL
license files, and a hash-pinned `SOURCE-MANIFEST.md`. The font payload is
25,470,492 bytes (24.29 MiB). It covers Latin/Greek/Cyrillic, Arabic, Hebrew,
major Indic scripts, Thai, symbols, monochrome emoji, and a ten-face JP/KR/SC/
TC/HK CJK collection.

These are coverage inputs, not proof of correct international text. This branch
now adds a bounded layout pipeline using HarfBuzz 15.1.0 for shaping, FriBidi
1.0.18 for paragraph levels and visual run order, libunibreak 8.0 for Unicode
line-break opportunities, and ordered per-cluster font fallback. Finder body
text and dialogs use that pipeline when all capabilities are present. Builds
without one or more libraries report an explicit degraded capability and keep
the previous renderer available.

The distinction still matters. Automated glyph selection and framebuffer
pixels do not establish that Arabic, Hebrew, Indic, Thai, CJK, or emoji text is
typographically or linguistically correct. FriBidi's installed data is Unicode
14, while the current line-break library is Unicode 17. Color emoji,
locale-sensitive CJK selection, full emoji sequence behavior, translation
expansion, and human-language visual review remain open gates.

## Automated evidence

The required focused command is:

```sh
BUILDFLEET_LOCAL=1 make test-fontbook
```

The focused suite covers:

- ChicagoFLF family/style/face metadata and FreeType validation;
- OpenType extension handling;
- a generated raw resource-fork suitcase with an embedded `sfnt` resource;
- malformed font and symlink rejection;
- recursive discovery and ten-face CJK collection enumeration;
- atomic install, collision refusal, and invalid-before-write behavior;
- persisted activate/deactivate state and last-usable-face protection;
- the seeded Utilities launcher, real Font Book window, off-screen three-pane
  paint, keyboard activation, and close behavior.

The international suite separately covers Arabic RTL shaping, mixed Hebrew/LTR
ordering, Devanagari clusters, CJK fallback, optional/mandatory/CJK breaks,
malformed UTF-8, missing-library degradation, unsafe break suppression, and
actual fallback-glyph pixels. It has passed full, partial, no-library, and
sanitizer builds.

At the latest checkpoint, the Font Book suite passed 58 checks with zero
failures and the full-capability text suite passed 48 checks with zero failures.
The complete `BUILDFLEET_LOCAL=1 make test` target also passed after integration;
the optional real-storage fixture suite reported its normal skip because
`OS999_STORAGE_FIXTURES` was unset. The 172 MiB ad-hoc-signed app bundle built,
passed strict code-signature verification, and contains all 15 Noto font files
plus their licenses. Quake III was not packaged because its separate build was
absent.

This remains source, automated, packaging, and off-screen evidence. The app's
text dependencies currently resolve to `/opt/homebrew` and are not yet
relocatable. I have not claimed hands-on visual acceptance, fluent-reader
language quality, or live interaction acceptance.

## Desktop integration order

The broader desktop audit is in `docs/DESKTOP-COMPLIANCE-AUDIT.md`. Its most
important merge finding is blunt: the Control Panels and Extensions branches
overlap 32 files and are not ancestor-related. The safest order is to use the
Control Panels branch as the container foundation, port the Extensions delta
onto it, then register Font Book through that shared module system. The core
catalog/parser APIs here are intentionally independent of either UI branch so
that port is mechanical rather than a rewrite.
