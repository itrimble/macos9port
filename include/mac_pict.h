#ifndef MAC_PICT_H
#define MAC_PICT_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint16_t rgb[256][3]; uint8_t allowed[256]; } MacPictPalette;
typedef struct { unsigned width,height; uint8_t *pixels,*covered; } MacPictImage;
/* Independent bounded PICTv2 indexed playback. Output is owned host memory;
 * no guest destination is modified until complete successful validation. */
int mac_pict_decode(const uint8_t *data,size_t size,unsigned width,unsigned height,
                    const MacPictPalette *palette,MacPictImage *out,char *error,size_t n);
void mac_pict_destroy(MacPictImage *image);
#endif
