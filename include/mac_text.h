#ifndef MAC_TEXT_INCLUDED
#define MAC_TEXT_INCLUDED
#include "mac_runtime.h"
#include "mac_font.h"

/* A resolved text face: a bitmap strike from the guest's resources plus the synthesised bold/underline and the whole-pixel space
 * extra. Shared by the QuickDraw text calls and TextEdit. */
typedef struct { MacStrike s; int bold, underline, extra; } MacTextFace;
/* Resolves (family, size, style byte, SpaceExtra Fixed). Halts the CPU by name and returns -1 for an unsupported style, a
 * fractional SpaceExtra, or a font the guest does not supply. */
int mac_text_face(MacRuntime *rt, int family, int size, unsigned face, int32_t extra, MacTextFace *f);
int mac_text_advance(const MacTextFace *f, unsigned ch);
/* Draws count characters at guest address `at` with the pen at (h, v) on `port`, using transfer mode `mode` (0 to 3) and further
 * clipped to clip[4] (top, left, bottom, right) when clip is not NULL. Does not touch the port's pen. 0 ok, -1 halted. */
int mac_text_draw_with(MacRuntime *rt, uint32_t port, const MacTextFace *f, int mode, int h, int v, uint32_t at, int count, const int *clip);
/* Inverts every pixel of the rectangle (top, left, bottom, right) on the port, clipped as for text. 0 ok, -1 halted. */
int mac_text_xor_rect(MacRuntime *rt, uint32_t port, const int rect[4], const int *clip);
/* Fills the rectangle with the port's background colour (bkColor), clipped as for text. */
int mac_text_erase_rect(MacRuntime *rt, uint32_t port, const int rect[4], const int *clip);
#endif
