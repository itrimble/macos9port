#ifndef OS999_DESKTOP_FONT_TABLE_H
#define OS999_DESKTOP_FONT_TABLE_H

/* Open-licensed fonts shipped with OS999. Each gets a small marker file in
 * System Folder/Fonts; the marker only activates the bundled preview and is
 * never the font itself. No Apple font appears here.
 *   name    shown in the Finder and the preview window
 *   id      marker body is "OS999-FONT:<id>:1\n"
 *   bundle  path under Contents/Resources/Fonts in the packaged app
 *   dev     path relative to the source tree
 *   sample  UTF-8 glyphs drawn in the preview (no shaping) */
typedef struct { const char *name, *id, *bundle, *dev, *sample; } DesktopFontEntry;

#define DESKTOP_FONT_LATIN "The quick brown fox 0123456789"
static const DesktopFontEntry k_desktop_fonts[] = {
    {"ChicagoFLF", "chicagoflf", "ChicagoFLF.ttf", "fonts/ChicagoFLF.ttf", DESKTOP_FONT_LATIN},
    {"Meslo Nerd Font Regular", "meslo-regular", "Terminal/MesloLGSNerdFontMono-Regular.ttf",
     "fonts/terminal/MesloLGSNerdFontMono-Regular.ttf", DESKTOP_FONT_LATIN},
    {"Meslo Nerd Font Bold", "meslo-bold", "Terminal/MesloLGSNerdFontMono-Bold.ttf",
     "fonts/terminal/MesloLGSNerdFontMono-Bold.ttf", DESKTOP_FONT_LATIN},
    {"Noto Sans", "noto-sans", "System/NotoSans-Regular.ttf", "fonts/system/NotoSans-Regular.ttf",
     DESKTOP_FONT_LATIN " \xCE\x91\xCE\xB2\xCE\xB3 \xD0\x96\xD0\xB4"},
    {"Noto Sans Arabic", "noto-arabic", "System/NotoSansArabic-Regular.ttf",
     "fonts/system/NotoSansArabic-Regular.ttf", "\xD8\xA7\xD8\xA8\xD8\xAA\xD8\xAB\xD8\xAC\xD8\xAD"},
    {"Noto Sans Bengali", "noto-bengali", "System/NotoSansBengali-Regular.ttf",
     "fonts/system/NotoSansBengali-Regular.ttf", "\xE0\xA6\x85\xE0\xA6\x86\xE0\xA6\x87\xE0\xA6\x95\xE0\xA6\x96"},
    {"Noto Sans CJK Collection", "noto-cjk", "System/NotoSansCJK-Regular.ttc",
     "fonts/system/NotoSansCJK-Regular.ttc", "\xE6\xB0\xB4\xE7\x81\xAB\xE3\x81\x82\xE3\x82\xA2\xED\x95\x9C"},
    {"Noto Sans Devanagari", "noto-devanagari", "System/NotoSansDevanagari-Regular.ttf",
     "fonts/system/NotoSansDevanagari-Regular.ttf", "\xE0\xA4\x85\xE0\xA4\x86\xE0\xA4\x95\xE0\xA4\x96"},
    {"Noto Emoji", "noto-emoji", "System/NotoEmoji-Variable.ttf", "fonts/system/NotoEmoji-Variable.ttf",
     "\xE2\x98\x80\xE2\x98\x85\xE2\x99\xA5\xE2\x9C\x93"},
    {"Noto Sans Gujarati", "noto-gujarati", "System/NotoSansGujarati-Regular.ttf",
     "fonts/system/NotoSansGujarati-Regular.ttf", "\xE0\xAA\x85\xE0\xAA\x86\xE0\xAA\x95\xE0\xAA\x96"},
    {"Noto Sans Gurmukhi", "noto-gurmukhi", "System/NotoSansGurmukhi-Regular.ttf",
     "fonts/system/NotoSansGurmukhi-Regular.ttf", "\xE0\xA8\x85\xE0\xA8\x86\xE0\xA8\x95\xE0\xA8\x96"},
    {"Noto Sans Hebrew", "noto-hebrew", "System/NotoSansHebrew-Regular.ttf",
     "fonts/system/NotoSansHebrew-Regular.ttf", "\xD7\x90\xD7\x91\xD7\x92\xD7\x93\xD7\x94"},
    {"Noto Sans Kannada", "noto-kannada", "System/NotoSansKannada-Regular.ttf",
     "fonts/system/NotoSansKannada-Regular.ttf", "\xE0\xB2\x85\xE0\xB2\x86\xE0\xB2\x95\xE0\xB2\x96"},
    {"Noto Sans Malayalam", "noto-malayalam", "System/NotoSansMalayalam-Regular.ttf",
     "fonts/system/NotoSansMalayalam-Regular.ttf", "\xE0\xB4\x85\xE0\xB4\x86\xE0\xB4\x95\xE0\xB4\x96"},
    {"Noto Sans Symbols 2", "noto-symbols2", "System/NotoSansSymbols2-Regular.ttf",
     "fonts/system/NotoSansSymbols2-Regular.ttf", "\xE2\x99\x94\xE2\x99\x96\xE2\x9A\x80\xE2\xAC\x9A"},
    {"Noto Sans Tamil", "noto-tamil", "System/NotoSansTamil-Regular.ttf",
     "fonts/system/NotoSansTamil-Regular.ttf", "\xE0\xAE\x85\xE0\xAE\x86\xE0\xAE\x95\xE0\xAE\x99"},
    {"Noto Sans Telugu", "noto-telugu", "System/NotoSansTelugu-Regular.ttf",
     "fonts/system/NotoSansTelugu-Regular.ttf", "\xE0\xB0\x85\xE0\xB0\x86\xE0\xB0\x95\xE0\xB0\x96"},
    {"Noto Sans Thai", "noto-thai", "System/NotoSansThai-Regular.ttf",
     "fonts/system/NotoSansThai-Regular.ttf", "\xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x84\xE0\xB8\x87"},
    {"IBM Plex Sans", "plex-regular", "Dashboard/plex-0.ttf", "assets/dashboard/plex-0.ttf", DESKTOP_FONT_LATIN},
    {"IBM Plex Sans Medium", "plex-medium", "Dashboard/plex-1.ttf", "assets/dashboard/plex-1.ttf", DESKTOP_FONT_LATIN},
    {"IBM Plex Sans SemiBold", "plex-semibold", "Dashboard/plex-2.ttf", "assets/dashboard/plex-2.ttf", DESKTOP_FONT_LATIN},
    {"IBM Plex Sans Bold", "plex-bold", "Dashboard/plex-3.ttf", "assets/dashboard/plex-3.ttf", DESKTOP_FONT_LATIN},
};
#define DESKTOP_FONT_COUNT ((int)(sizeof k_desktop_fonts / sizeof k_desktop_fonts[0]))

#endif
