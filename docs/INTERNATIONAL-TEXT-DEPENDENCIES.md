# International text dependencies

Verified on 7 November 2026. No package was installed or upgraded for this
work; all three installed versions matched the current official release.

| Component | Version | Role | License and packaging note |
| --- | --- | --- | --- |
| HarfBuzz | 15.1.0 | OpenType shaping and glyph positioning | MIT. The local Homebrew build pulls a broad dependency closure; do not copy `/opt/homebrew` paths into a release. |
| FriBidi | 1.0.18 | Unicode embedding levels, brackets, and visual run order | Library headers are LGPL-2.1-or-later; installed data reports Unicode 14. A distributed binary needs an explicit compliance plan. |
| libunibreak | 8.0 | UAX #14 line-break opportunities | zlib license; Unicode 17 / UAX #14 revision 55. |

Official release sources:

- <https://github.com/harfbuzz/harfbuzz/releases/tag/15.1.0>
- <https://github.com/fribidi/fribidi/releases/tag/v1.0.18>
- <https://github.com/adah1972/libunibreak/releases/tag/libunibreak_8_0>

The production packaging preference is a minimal pinned HarfBuzz build with
FreeType enabled and GLib, Cairo, ICU, and Graphite disabled; a permissive
Unicode-17 bidi library such as SheenBidi 3.0.0; and libunibreak 8.0. The
current branch uses already-installed FriBidi so the feature is testable now.
Switching libraries is a separate compatibility and packaging change, not a
reason to overstate current results.
