#ifndef OS999_FRAME_DUMP_H
#define OS999_FRAME_DUMP_H
#include <stddef.h>
#include <stdint.h>

/* Developer test hook (OS999_ENABLE_TEST_HOOKS builds only, see main.c):
 * OSPI_DUMP=<file.ppm> OSPI_DUMP_AT=20000,80000 writes the desktop framebuffer
 * (everything OS999 presents, including embedded browser frames) at each listed
 * time in milliseconds since launch, to <file>-1.ppm, <file>-2.ppm, ... */

/* Writes a binary PPM (P6) of a w*h 0xAARRGGBB framebuffer; 0 on success. */
int frame_dump_write(const char *path, const uint32_t *fb, int w, int h);

/* 1 once `elapsed_ms` has reached the *next-th entry of the comma list `times`
 * (and advances *next); 0 when not yet due or the list is exhausted/invalid. */
int frame_dump_due(const char *times, int *next, uint32_t elapsed_ms);

/* "<stem>-<index>.ppm" where <stem> is `base` without a trailing ".ppm"; 0 on success. */
int frame_dump_name(const char *base, int index, char *out, size_t n);

/* Number of distinct pixel values in the rectangle (capped at `cap`); 1 means a flat,
 * blank area. Rectangle is clipped to the framebuffer. */
int frame_dump_distinct(const uint32_t *fb, int w, int h, int x, int y, int rw, int rh, int cap);

/* Summary of a 4-bytes-per-pixel image sampled on a 48x27 grid (channel order does not matter:
 * the first three bytes of each pixel are used). On macOS the browser's frames are presented as
 * GPU surfaces that never enter the desktop framebuffer, so the test hook samples the surface. */
typedef struct {
    int sampled;     /* grid points read */
    int distinct;    /* distinct 5-bit-per-channel colours among them (1 = flat/blank) */
    int mean;        /* mean of the three channels, 0..255 */
    int min, max;    /* darkest and brightest sampled channel average */
} SurfaceStats;

/* 0 on success; -1 for a null/empty image. */
int frame_surface_stats(const uint8_t *px, int w, int h, size_t stride, SurfaceStats *out);
#endif
