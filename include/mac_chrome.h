#ifndef MAC_CHROME_H
#define MAC_CHROME_H
#include <stdint.h>
/* The 8x8 glyph (font8x8 layout: row y, bit x is column x from the left) for a guest character in the runtime's own chrome. Bytes below 128 are the ASCII
 * glyphs; Mac Roman bytes $80 to $FF are the glyphs of the characters they stand for, from ChicagoFLF at 8 pixels, or plain ASCII look-alikes when the
 * font is unavailable (mac_chrome_glyph.c says which). `scratch` holds a rendered glyph; the result may point into it or into the static table. */
const uint8_t *mac_chrome_glyph(unsigned ch, uint8_t scratch[8]);
#endif
