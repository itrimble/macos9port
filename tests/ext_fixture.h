#ifndef EXT_FIXTURE_H
#define EXT_FIXTURE_H
/* Hand-assembled INIT fixtures. No Apple bytes: every instruction is authored here. */
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void fx16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void fx32(uint8_t *p, uint32_t v) { fx16(p, (uint16_t)(v >> 16)); fx16(p + 2, (uint16_t)v); }

/* One INIT resource (id 128) with `code` as its bytes; returns the fork size. */
static size_t fx_fork(uint8_t *out, size_t cap, const uint8_t *code, size_t n) {
    enum { DATA = 256 };
    size_t datalen = 4 + n, map = DATA + ((datalen + 3) & ~3u) + 4, maplen = 50;
    if (map + maplen > cap) return 0;
    memset(out, 0, map + maplen);
    fx32(out, DATA); fx32(out + 4, (uint32_t)map); fx32(out + 8, (uint32_t)datalen); fx32(out + 12, (uint32_t)maplen);
    fx32(out + DATA, (uint32_t)n);
    memcpy(out + DATA + 4, code, n);
    fx16(out + map + 24, 28); fx16(out + map + 26, (uint16_t)maplen);
    fx16(out + map + 28, 0); fx32(out + map + 30, 0x494e4954u); /* INIT */
    fx16(out + map + 34, 0); fx16(out + map + 36, 10);
    fx16(out + map + 38, 128); fx16(out + map + 40, 0xffff);
    return map + maplen;
}

static int fx_write(const char *path, const uint8_t *code, size_t n) {
    uint8_t buf[1024];
    size_t size = fx_fork(buf, sizeof buf, code, n);
    FILE *f = size ? fopen(path, "wb") : NULL;
    if (!f) return -1;
    int ok = fwrite(buf, 1, size, f) == size;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static __attribute__((unused)) int fx_write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(text, f);
    return fclose(f);
}

/* ---- INIT programs ---- */
static const uint8_t FX_OK[] = {0x4e, 0x75};                                  /* RTS */
static const uint8_t FX_LOOP[] = {0x60, 0xfe};                                /* BRA.S * */
static const uint8_t FX_HALT[] = {0xa8, 0x9d, 0x4e, 0x75};                    /* unsupported trap */
/* LEA 12(PC),A0 ; MOVE.W #$a9a0,D0 ; SetTrapAddress ; MOVEQ #0,D0 ; RTS ; handler: RTS */
static const uint8_t FX_PATCH[] = {0x41, 0xfa, 0x00, 0x0c, 0x30, 0x3c, 0xa9, 0xa0, 0xa0, 0x47,
                                   0x70, 0x00, 0x4e, 0x75, 0x4e, 0x75};
/* Patches $a9a0, then tries LoadSeg ($a9f0), which is protected: the extension must roll back. */
static const uint8_t FX_PATCH_PROTECTED[] = {0x41, 0xfa, 0x00, 0x14, 0x30, 0x3c, 0xa9, 0xa0, 0xa0, 0x47,
                                             0x30, 0x3c, 0xa9, 0xf0, 0xa0, 0x47, 0x70, 0x00, 0x4e, 0x75,
                                             0x4e, 0x75};
/* Leaves junk in D0 (MOVEQ #1) like real INITs do; must still load. */
static const uint8_t FX_JUNK_D0[] = {0x70, 0x01, 0x4e, 0x75};
/* MOVE.W #1,-(SP) ; LoadSeg ; RTS: segment loading is refused inside an INIT. */
static const uint8_t FX_LOADSEG[] = {0x3f, 0x3c, 0x00, 0x01, 0xa9, 0xf0, 0x4e, 0x75};
/* MOVE.L #'test',D0 ; SUBA.L A0,A0 ; NewGestalt ; RTS */
static const uint8_t FX_GESTALT[] = {0x20, 0x3c, 't', 'e', 's', 't', 0x91, 0xc8, 0xa3, 0xad, 0x4e, 0x75};
#endif
