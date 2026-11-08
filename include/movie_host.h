#ifndef OS999_MOVIE_HOST_H
#define OS999_MOVIE_HOST_H
#include <stddef.h>
#include <stdint.h>
/* Optional modern native decoder. Core/headless raw movies need no FFmpeg. */
typedef struct {
    void *(*open)(const char *, char *, size_t);
    void (*close)(void *);
    void (*play)(void *);
    void (*pause)(void *);
    int (*seek)(void *, uint32_t);
    int (*tick)(void *, uint32_t);
    int (*state)(const void *);
    uint32_t (*time)(const void *);
    uint32_t (*duration)(const void *);
    const uint32_t *(*pixels)(const void *, int *, int *);
    void (*volume)(void *, float);
    float (*get_volume)(const void *);
} MovieHostBackend;
void movie_host_install(const MovieHostBackend *backend);
const MovieHostBackend *movie_host_backend(void);
#endif
