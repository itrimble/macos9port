/* Keyboard skins. Each one recalls an era of desk and portable keyboards
 * through color, cap shape and switch sound only: no logos, no trade dress
 * text, no copied artwork. */
#include "kbd.h"
#include "kbd_skin.h"

static const KbdSkin skins[] = {
    { "Platinum", "Light gray caps on a platinum case, soft rubber-dome feel.",
      0xFFCFCFCC, 0xFFBDBDBA, 0xFF8E8E8B,
      0xFFE4E4E1, 0xFFD3D3D0, 0xFFB4B4B1, 0xFF8F8F8C,
      0xFFD8D8D5, 0xFFC6C6C3, 0xFF34343A, 0xFF4A4A50, 0xFF5BB46A,
      0xFFC3C3C0, 0xFFB5B5B2, 0xFF8A8A87, 0xFFD0D0CD, 0xFFB8B8B5,
      1.0f, 0.16f, TEX_PINSTRIPE, KBD_SW_DOME, {0} },
    { "Desk Beige", "Warm beige caps, the deep tactile clack of a heavy desk board.",
      0xFFD9D1BE, 0xFFC9C0AA, 0xFF9A907A,
      0xFFEDE6D3, 0xFFDDD5C0, 0xFFC0B6A0, 0xFF99907C,
      0xFFDCD3BE, 0xFFC9BFA8, 0xFF3E3A34, 0xFF4E4940, 0xFF52C060,
      0xFFCDC4AE, 0xFFC0B7A1, 0xFF928974, 0xFFD8CFBA, 0xFFBDB4A0,
      1.0f, 0.13f, TEX_NONE, KBD_SW_ALPS, {0} },
    { "Original 128", "Squarer caps and a bright, hollow mechanical click.",
      0xFFD2C8B0, 0xFFC2B79D, 0xFF8F846C,
      0xFFF0E9D6, 0xFFE3DBC5, 0xFFC9BFA7, 0xFFA0967E,
      0xFFE6DEC8, 0xFFD6CDB5, 0xFF2E2B27, 0xFF3A3631, 0xFF4FAE5C,
      0xFFCBC1A9, 0xFFBDB39A, 0xFF8C8169, 0xFFDDD4BD, 0xFFC3B9A1,
      1.0f, 0.08f, TEX_NONE, KBD_SW_MITSUMI, {0} },
    { "Portable Black", "Low black caps on a dark speckled deck, quiet laptop travel.",
      0xFF3B3B3F, 0xFF2C2C30, 0xFF141416,
      0xFF2E2E31, 0xFF232326, 0xFF161618, 0xFF0B0B0C,
      0xFF2B2B2E, 0xFF202023, 0xFFE6E6E6, 0xFFBDBDBD, 0xFF7FD38A,
      0xFF343438, 0xFF2A2A2E, 0xFF101012, 0xFF3A3A3E, 0xFF2A2A2D,
      1.0f, 0.12f, TEX_SPECKLE, KBD_SW_SCISSOR, {0} },
    { "Teal Translucent", "See-through teal caps over a frosted case; the domes show.",
      0xFF4FA9B2, 0xFF2F7D88, 0xFF1D5A63,
      0xFFBDEBEE, 0xFF8ED3D9, 0xFF5FB4BD, 0xFF3C8E98,
      0xFFAEE2E6, 0xFF80CAD1, 0xFF0D3A40, 0xFF0D3A40, 0xFFFFFFFF,
      0xFF7CC6CD, 0xFF5AAFB7, 0xFF2C7680, 0xFF9AD8DE, 0xFF6BBDC5,
      0.72f, 0.2f, TEX_DOMES, KBD_SW_DOME, {0} },
    { "Graphite", "Smoky translucent caps on a graphite case.",
      0xFF6C7076, 0xFF53575D, 0xFF34373B,
      0xFFC9CDD2, 0xFFA9AEB4, 0xFF878C92, 0xFF62666C,
      0xFFBCC0C5, 0xFF9DA2A8, 0xFF1C1F23, 0xFF1C1F23, 0xFFE8EEF4,
      0xFF8C9096, 0xFF757A80, 0xFF44474C, 0xFFA3A7AD, 0xFF85898F,
      0.76f, 0.2f, TEX_DOMES, KBD_SW_DOME, {0} },
    { "Five Flavors", "White caps tinted row by row in five fruit colors.",
      0xFFE9E9EC, 0xFFD7D7DB, 0xFFA2A2A8,
      0xFFF7F7F8, 0xFFE6E6E9, 0xFFC8C8CD, 0xFFA4A4AA,
      0xFFF0F0F2, 0xFFDDDDE1, 0xFF2A2A30, 0xFF3A3A40, 0xFF39A34C,
      0xFFDCDCE0, 0xFFCDCDD2, 0xFF9C9CA3, 0xFFE8E8EB, 0xFFCFCFD4,
      0.94f, 0.2f, TEX_DOMES, KBD_SW_DOME,
      { 0xFF8C6BB1, 0xFFE86A73, 0xFFF2A14A, 0xFF9CCB5A, 0xFF4C86C8, 0xFF8C6BB1 } },
    { "Midnight", "Dark platinum with warm legends for typing in bed.",
      0xFF2A2C31, 0xFF1F2125, 0xFF0F1012,
      0xFF41444C, 0xFF363940, 0xFF26282D, 0xFF17181B,
      0xFF383B42, 0xFF2E3137, 0xFFF0E3C0, 0xFFCFC3A3, 0xFFFFB347,
      0xFF33363C, 0xFF2A2C32, 0xFF121315, 0xFF3D4047, 0xFF2D3035,
      1.0f, 0.16f, TEX_NONE, KBD_SW_ALPS, {0} },
};

int kbd_skin_count(void) { return (int)(sizeof skins / sizeof skins[0]); }

const KbdSkin *kbd_skin_get(int i) {
    int n = kbd_skin_count();
    return &skins[((i % n) + n) % n];
}

const char *kbd_skin_name(int i) { return kbd_skin_get(i)->name; }
int kbd_skin_sound(int i) { return kbd_skin_get(i)->sound; }
