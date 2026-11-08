#ifndef MAC_QDCOLOR_H
#define MAC_QDCOLOR_H
#include <stdint.h>
/* QuickDraw's eight colour constants (blackColor 33, whiteColor 30, redColor 205, greenColor 341, blueColor 409, cyanColor 273, magentaColor 137,
 * yellowColor 69; values from my memory of Inside Macintosh), as the pure 8-bit-per-channel colours they stand for. The real RGB values a device
 * uses differ slightly. Returns 1 and fills rgb[3] (0-255), or 0 when the long is not one of the eight. */
int mac_qd_color_constant(uint32_t color, unsigned rgb[3]);
#endif
