# OS999 system font pack

Verified and downloaded on 7 November 2026. These are redistributable Noto
fonts from the official Noto/Google Fonts repositories. No Apple font, system
file, logo, or dependency-tree font was copied into this directory.

The pack is deliberately one regular face per writing system, plus a variable
monochrome emoji face and a ten-face CJK collection. Its 15 font files occupy
25,470,492 bytes (24.29 MiB). Each upstream license is stored beside the font.

## What this pack does and does not prove

These files provide glyphs. This branch also supplies HarfBuzz shaping,
FriBidi run ordering, libunibreak opportunities, and ordered per-cluster font
fallback when those optional libraries are present. That implementation is
covered by focused automated tests, but the files and tests do not, by
themselves, prove a complete international text stack.

- Human-language visual review remains required for Arabic joining, Indic
  conjuncts and reordering, Thai mark placement, mixed-direction paragraphs,
  and language-sensitive CJK forms.
- A successful font scan and fallback choice are not proof that mixed-script
  text is typographically or linguistically correct.
- The CJK collection contains proportional and monospaced JP, KR, Simplified
  Chinese, Traditional Chinese, and Hong Kong faces. The application must use
  the correct face index for locale-sensitive glyph forms.
- Noto Emoji is monochrome. It supports a broad current emoji repertoire, but
  color rendering, variation-sequence handling, ZWJ composition, flags, skin
  tones, and fallback must be tested separately.
- This is not every Noto script. Arabic, Hebrew, Thai, major modern Indic
  scripts, CJK, symbols, emoji, Latin, Greek, and Cyrillic are represented.
  Additional scripts should be optional packs until OS999 has fallback,
  shaping, localization, and package-size policy.

## Selected files

| File | Bytes | SHA-256 | Intended coverage |
| --- | ---: | --- | --- |
| `NotoSans-Regular.ttf` | 825,628 | `f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5` | Latin, Greek, Cyrillic and common punctuation |
| `NotoSansArabic-Regular.ttf` | 271,652 | `7ed3fe069312aceac454f17cf613a30f95271d6ed7ce58005ed4d016bd3823d7` | Arabic-script languages; shaping still required |
| `NotoSansHebrew-Regular.ttf` | 66,156 | `671951828bd5c95db818e5bb12dcea2d0c0dda00311888522be061ee6835125e` | Hebrew; bidi layout still required |
| `NotoSansDevanagari-Regular.ttf` | 277,680 | `573e525d9f4923d34645fe752475419f45f8dec5a8e43f3e83c5c5f43d1f2ff9` | Devanagari languages; shaping still required |
| `NotoSansBengali-Regular.ttf` | 179,596 | `67e3584988627749e701ef833e9cb1964cd17343867294cb833d3ffe2e23279f` | Bengali and Assamese; shaping still required |
| `NotoSansGujarati-Regular.ttf` | 232,560 | `30ecc9188d6615bb45601daa746add60c463766324921461052443e1516dab0f` | Gujarati; shaping still required |
| `NotoSansGurmukhi-Regular.ttf` | 93,984 | `7b2c1cfa0bc744623c7ce49eccfef440ec8419596d995bdeee973cd43f46ab00` | Gurmukhi/Punjabi; shaping still required |
| `NotoSansKannada-Regular.ttf` | 214,140 | `81b587d0f850db999c64cddf823be2a2648f6244a647fa0a3fe4eea83ff01de8` | Kannada; shaping still required |
| `NotoSansMalayalam-Regular.ttf` | 150,284 | `d18b5c10d85bba3d3d89775484bcfa731112f501ac070793ddbda5a36992520f` | Malayalam; shaping still required |
| `NotoSansTamil-Regular.ttf` | 115,856 | `0afbc221964b6048c6d771c525be474d21b288a621dce0fafedd695cc5c98e4e` | Tamil; shaping still required |
| `NotoSansTelugu-Regular.ttf` | 269,636 | `e0595bcf47b907b2afb77a34ae64c3e8351f56452c66983660172c6b9ea15576` | Telugu; shaping still required |
| `NotoSansThai-Regular.ttf` | 72,812 | `2101faa471942570d506a394df7da555d15246c7814c7522c63d4fa85b0d8d88` | Thai; mark positioning and breaking still require tests |
| `NotoSansSymbols2-Regular.ttf` | 1,233,128 | `3ce38effdb615dd929c8b0f52768dfa2cd21f206e3824bc7df61e4074b41ae52` | Technical, UI, dingbat, and historic symbols |
| `NotoEmoji-Variable.ttf` | 1,982,596 | `de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551` | Monochrome emoji, weights 300 through 700 |
| `NotoSansCJK-Regular.ttc` | 19,484,784 | `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a` | JP/KR/SC/TC/HK proportional and monospaced CJK faces |

`fc-scan` 2.18.3 successfully read every file and reported the expected family.
That is metadata/FreeType evidence only; it is not shaping or visual acceptance.

## Official upstream releases

All release URLs below were opened through the GitHub API and checked against
the official repositories. The selected `.ttf` files are the `full/ttf`
Regular builds inside each release archive.

| Family | Release/tag | Commit | Published | Official source |
| --- | --- | --- | --- | --- |
| Noto Sans | `NotoSans-v2.015` | `c4a321e123e4d4ff315f57f4e0adf294fe3a95be` | 2024-11-20 | <https://github.com/notofonts/latin-greek-cyrillic/releases/tag/NotoSans-v2.015> |
| Noto Sans Arabic | `NotoSansArabic-v2.013` | `1b2b7e5c6ce3ab4d50681c854892325530084c35` | 2025-10-15 | <https://github.com/notofonts/arabic/releases/tag/NotoSansArabic-v2.013> |
| Noto Sans Hebrew | `NotoSansHebrew-v3.001` | `036f3206f67caac235cf8546a7751d3440771a7e` | 2024-06-03 | <https://github.com/notofonts/hebrew/releases/tag/NotoSansHebrew-v3.001> |
| Noto Sans Devanagari | `NotoSansDevanagari-v2.007` | `e123d230c160ebe949d731cc19017cdb354180d1` | 2026-09-09 | <https://github.com/notofonts/devanagari/releases/tag/NotoSansDevanagari-v2.007> |
| Noto Sans Bengali | `NotoSansBengali-v3.011` | `85d80394cbbbb798ca0a41c983902e6cf77be3a3` | 2026-01-09 | <https://github.com/notofonts/bengali/releases/tag/NotoSansBengali-v3.011> |
| Noto Sans Gujarati | `NotoSansGujarati-v2.106` | `17e99472719c29aa46534e4690e84443bcc69eab` | 2023-10-26 | <https://github.com/notofonts/gujarati/releases/tag/NotoSansGujarati-v2.106> |
| Noto Sans Gurmukhi | `NotoSansGurmukhi-v2.004` | `231c5c6e9c622c4ef3792ead7a91045af0ee610a` | 2023-03-27 | <https://github.com/notofonts/gurmukhi/releases/tag/NotoSansGurmukhi-v2.004> |
| Noto Sans Kannada | `NotoSansKannada-v2.006` | `7eeac8993f890e3d8568ba909ae67f554113ea92` | 2024-09-19 | <https://github.com/notofonts/kannada/releases/tag/NotoSansKannada-v2.006> |
| Noto Sans Malayalam | `NotoSansMalayalam-v2.104` | `0fd65e553a6af3dc1c09ed39dfe8933e01c17b32` | 2023-03-27 | <https://github.com/notofonts/malayalam/releases/tag/NotoSansMalayalam-v2.104> |
| Noto Sans Tamil | `NotoSansTamil-v2.004` | `f34a08d1ae3fa810581f63410296d971bdcd62dc` | 2023-03-27 | <https://github.com/notofonts/tamil/releases/tag/NotoSansTamil-v2.004> |
| Noto Sans Telugu | `NotoSansTelugu-v2.005` | `e97c3409a8347d68cccd06a82a68b418c315ee0c` | 2023-09-28 | <https://github.com/notofonts/telugu/releases/tag/NotoSansTelugu-v2.005> |
| Noto Sans Thai | `NotoSansThai-v2.002` | `f8b482c158650260bba5d5edba9da3e8bb7185b4` | 2023-09-26 | <https://github.com/notofonts/thai/releases/tag/NotoSansThai-v2.002> |
| Noto Sans Symbols 2 | `NotoSansSymbols2-v2.008` | `25b00f0d3f40873a005287514ba2a48558655314` | 2023-10-13 | <https://github.com/notofonts/symbols/releases/tag/NotoSansSymbols2-v2.008> |
| Noto Sans CJK | `Sans2.004` | `523d033d6cb47f4a80c58a35753646f5c3608a78` | 2022-01-27 | <https://github.com/notofonts/noto-cjk/releases/tag/Sans2.004> |
| Noto Emoji | Google Fonts `main` snapshot | `86aced91de05cd4f96a06838124cca45be337b2b` | checked 2026-11-07 | <https://github.com/google/fonts/tree/main/ofl/notoemoji> |

Noto Emoji was downloaded from the immutable Google Fonts commit above:
<https://raw.githubusercontent.com/google/fonts/86aced91de05cd4f96a06838124cca45be337b2b/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf>.
The CJK collection was downloaded from the immutable release commit:
<https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTC/NotoSansCJK-Regular.ttc>.

## Release archive verification

The temporary release archives were hashed before extraction and then removed.
These hashes allow the exact upstream inputs to be reacquired and checked.

| Archive | SHA-256 |
| --- | --- |
| `NotoSans-v2.015.zip` | `0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5` |
| `NotoSansArabic-v2.013.zip` | `1301aceaea84c501cf2e6dcfb3182e2328c8eae5725817fcb239672bda7154f1` |
| `NotoSansHebrew-v3.001.zip` | `df0a71814b4e63644cf40fcc4529111b61266b7a2dafbe95068b29a7520cc3cb` |
| `NotoSansDevanagari-v2.007.zip` | `820c7da45b1e63562cb41c0a8cac5d9a4202312043a3a040ed1325857ef469b1` |
| `NotoSansBengali-v3.011.zip` | `2aca24bf71665e66c32e14862ee58857dd8b8b07b6321ec7c96f3121ad6683b1` |
| `NotoSansGujarati-v2.106.zip` | `a4ff1dd03a4998ba08ee7ee43be2aca7594a884982049e48c25ccc48991474ed` |
| `NotoSansGurmukhi-v2.004.zip` | `a55ee5ee831ce3a4bb73bdc46bd0a02db6eb9445c370a0afbc529379ed8cea52` |
| `NotoSansKannada-v2.006.zip` | `902b1b92018f7c96862d68d0a39ef03b978203cd4c3f55d6760f597657b51c44` |
| `NotoSansMalayalam-v2.104.zip` | `2ebd31e79f2893025d659def7784e0ec3557e7ff9ac105adcc82d35782913bf2` |
| `NotoSansTamil-v2.004.zip` | `f8284e0f200a7f29a439b4ec88280d864b2b31f8479111c5b658ba6da38b3005` |
| `NotoSansTelugu-v2.005.zip` | `3553e00ca341dc06f4a143c604dd93a1342553169b5a06dc8b0ff50ab6eba0a2` |
| `NotoSansThai-v2.002.zip` | `af889cc673fc714060ce5e4e088fbad32aa4c0571a19958efeaff128a22da485` |
| `NotoSansSymbols2-v2.008.zip` | `346c930bbe8eb946701a05c54e9c11a2094dee1d93c387bf1771c0a3e335688f` |

## License map

All files are distributed under the SIL Open Font License 1.1. The exact
license text shipped by each upstream release is retained separately:

- `LICENSE-NotoSans-OFL.txt`
- `LICENSE-NotoSansArabic-OFL.txt`
- `LICENSE-NotoSansHebrew-OFL.txt`
- `LICENSE-NotoSansDevanagari-OFL.txt`
- `LICENSE-NotoSansBengali-OFL.txt`
- `LICENSE-NotoSansGujarati-OFL.txt`
- `LICENSE-NotoSansGurmukhi-OFL.txt`
- `LICENSE-NotoSansKannada-OFL.txt`
- `LICENSE-NotoSansMalayalam-OFL.txt`
- `LICENSE-NotoSansTamil-OFL.txt`
- `LICENSE-NotoSansTelugu-OFL.txt`
- `LICENSE-NotoSansThai-OFL.txt`
- `LICENSE-NotoSansSymbols2-OFL.txt`
- `LICENSE-NotoSansCJK-OFL.txt`
- `LICENSE-NotoEmoji-OFL.txt`

The license files are not interchangeable placeholders: they retain each
release's copyright and Reserved Font Name declarations.

## Next engineering gates

1. Add deterministic screenshot review by fluent readers for every represented
   script. Test mixed-direction text and combining marks, not isolated glyphs.
2. Add locale selection for the ten CJK faces and broader script/language
   preferences for the ordered fallback resolver.
3. Test color emoji and full variation-selector, ZWJ, flag, and skin-tone
   behavior; the bundled Noto Emoji face is monochrome.
4. Decide whether these fonts ship in the base bundle or as installable packs.
   The CJK collection is 76% of the current pack by itself.
5. Produce a relocatable dependency package and complete the FriBidi LGPL
   distribution plan before calling the macOS application redistributable.
6. Add optional packs only from official tagged sources, with the same hash,
   license, rendering, and human-language review gates.
