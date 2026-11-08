#ifndef OS999_QUICKTIME_DECODE_H
#define OS999_QUICKTIME_DECODE_H
#include <stddef.h>
#include <stdint.h>
typedef struct QtDecoder QtDecoder;
QtDecoder *qt_decode_open(const char *path, char *error, size_t n);
void qt_decode_close(QtDecoder *decoder);
/* 1 frame, 0 EOF, 2 bounded-work yield, -1 error. */
int qt_decode_next(QtDecoder *decoder, double *pts);
int qt_decode_seek(QtDecoder *decoder, double seconds);
const uint32_t *qt_decode_pixels(const QtDecoder *decoder, int *w, int *h);
double qt_decode_duration(const QtDecoder *decoder);
int qt_decode_hardware(const QtDecoder *decoder);
const char *qt_decode_codec(const QtDecoder *decoder);
#endif
