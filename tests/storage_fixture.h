#ifndef OS999_STORAGE_FIXTURE_H
#define OS999_STORAGE_FIXTURE_H
#include "st_test.h"
#include "st_int.h"
static void fixture_fork(StMacFile *f, const char *type) {
    uint8_t b[326] = {0};
    st_put32(b, 256); st_put32(b + 4, 276); st_put32(b + 8, 20); st_put32(b + 12, 50);
    st_put32(b + 256, 16);
    st_put16(b + 276 + 24, 28); st_put16(b + 276 + 26, 50);
    memcpy(b + 306, type, 4); st_put16(b + 312, 10); st_put16(b + 316, 0xffff);
    CHECK(st_bytes_append(&f->rsrc, b, sizeof b) == 0);
}
static void fixture_write(const char *path, const StBytes *b) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    CHECK(fwrite(b->p, 1, b->n, f) == b->n);
    CHECK(fclose(f) == 0);
}
static void fixture_udif(const StBytes *raw, StBytes *dmg) {
    uint8_t table[284] = {0}, trailer[512] = {0};
    memcpy(table, "mish", 4);
    st_put32(table + 200, 2);
    st_put32(table + 204, 1); /* raw chunk */
    st_put64(table + 220, raw->n / 512);
    st_put64(table + 236, raw->n);
    st_put32(table + 244, 0xffffffffu);
    char b64[384];
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < sizeof table; i += 3) {
        uint32_t x = (uint32_t)table[i] << 16;
        if (i + 1 < sizeof table) x |= (uint32_t)table[i + 1] << 8;
        if (i + 2 < sizeof table) x |= table[i + 2];
        b64[o++] = alphabet[x >> 18]; b64[o++] = alphabet[(x >> 12) & 63];
        b64[o++] = i + 1 < sizeof table ? alphabet[(x >> 6) & 63] : '=';
        b64[o++] = i + 2 < sizeof table ? alphabet[x & 63] : '=';
    }
    b64[o] = 0;
    char xml[512];
    int n = snprintf(xml, sizeof xml, "<plist><key>blkx</key><array><dict><data>%s</data></dict></array></plist>", b64);
    CHECK(n > 0 && (size_t)n < sizeof xml);
    memcpy(trailer, "koly", 4);
    st_put64(trailer + 216, raw->n); st_put64(trailer + 224, (size_t)n);
    st_put64(trailer + 492, raw->n / 512);
    CHECK(!st_bytes_append(dmg, raw->p, raw->n));
    CHECK(!st_bytes_append(dmg, xml, (size_t)n));
    CHECK(!st_bytes_append(dmg, trailer, sizeof trailer));
}
static void both32(uint8_t *p, uint32_t v) { st_putle32(p, v); st_put32(p + 4, v); }
static size_t iso_record(uint8_t *p, uint32_t block, uint32_t size, int dir, const char *name, size_t len) {
    size_t n = 33 + len + !(len & 1);
    memset(p, 0, n); p[0] = (uint8_t)n;
    both32(p + 2, block); both32(p + 10, size);
    p[25] = dir ? 2 : 0; st_putle16(p + 28, 1); st_put16(p + 30, 1);
    p[32] = (uint8_t)len; memcpy(p + 33, name, len);
    return n;
}
static void fixture_iso(StBytes *iso) {
    CHECK(!st_bytes_reserve(iso, 24 * 2048));
    iso->n = 24 * 2048; memset(iso->p, 0, iso->n);
    uint8_t *p = iso->p + 16 * 2048;
    p[0] = 1; memcpy(p + 1, "CD001", 5); p[6] = 1;
    memset(p + 40, ' ', 32); memcpy(p + 40, "TEST DISC", 9);
    both32(p + 80, 24); st_putle16(p + 128, 2048); st_put16(p + 130, 2048);
    iso_record(p + 156, 20, 2048, 1, "", 1);
    p = iso->p + 17 * 2048; p[0] = 255; memcpy(p + 1, "CD001", 5); p[6] = 1;
    p = iso->p + 20 * 2048;
    size_t at = iso_record(p, 20, 2048, 1, "", 1);
    at += iso_record(p + at, 20, 2048, 1, "\1", 1);
    iso_record(p + at, 21, 5, 0, "HELLO.TXT;1", 11);
    memcpy(iso->p + 21 * 2048, "hello", 5);
}
#endif
