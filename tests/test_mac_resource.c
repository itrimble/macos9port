#include "mac_resource.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; \
} } while (0)

enum { DATA = 256, MAP = 280, TYPES = MAP + 28, REFS = TYPES + 18,
       NAMES = REFS + 36, END = NAMES + 5 };

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Hand-authored resource layout, not bytes from any application or Apple file. */
static void fixture(uint8_t *p) {
    memset(p, 0, END);
    put32(p, DATA);
    put32(p + 4, MAP);
    put32(p + 8, 24);
    put32(p + 12, END - MAP);
    put32(p + DATA, 3);
    memcpy(p + DATA + 4, "abc", 3);
    put32(p + DATA + 7, 0);
    put32(p + DATA + 11, 2);
    memcpy(p + DATA + 15, "xy", 2);
    put16(p + MAP + 24, 28);
    put16(p + MAP + 26, NAMES - MAP);
    put16(p + TYPES, 1); /* two types */
    memcpy(p + TYPES + 2, "CODE", 4);
    put16(p + TYPES + 6, 1); /* two resources */
    put16(p + TYPES + 8, 18);
    memcpy(p + TYPES + 10, "STR ", 4);
    put16(p + TYPES + 14, 0);
    put16(p + TYPES + 16, 42);
    put16(p + REFS, 0);
    put16(p + REFS + 2, 0);
    p[REFS + 4] = 0x24;
    put16(p + REFS + 12, 1);
    put16(p + REFS + 14, 0xffff);
    p[REFS + 19] = 7;
    put16(p + REFS + 24, 0xff80); /* negative ID */
    put16(p + REFS + 26, 0xffff);
    p[REFS + 31] = 11;
    p[NAMES] = 4;
    memcpy(p + NAMES + 1, "Demo", 4);
}

static void invalid(const uint8_t *p, size_t size) {
    MacResourceFork f;
    char err[120];
    memset(&f, 0xa5, sizeof f);
    CHECK(mac_resource_open(&f, p, size, err, sizeof err) == -1);
    CHECK(err[0] != 0);
    CHECK(f.bytes == NULL);
    CHECK(mac_resource_count(&f) == 0);
}

static void malformed(void) {
    uint8_t p[END];
    fixture(p);
    for (size_t n = 0; n < END; n++) {
        invalid(p, n);
    }
    fixture(p); put32(p, 0xfffffff0); invalid(p, END);
    fixture(p); put32(p + 4, 0xfffffff0); invalid(p, END);
    fixture(p); put32(p + 8, 0xffffffff); invalid(p, END);
    fixture(p); put32(p + 12, 0xffffffff); invalid(p, END);
    fixture(p); put32(p + 12, 27); invalid(p, END);
    fixture(p); put32(p, 0); invalid(p, END);
    fixture(p); put32(p, MAP - 1); invalid(p, END); /* overlapping regions */
    fixture(p); put16(p + MAP + 24, 27); invalid(p, END);
    fixture(p); put16(p + MAP + 24, 0xffff); invalid(p, END);
    fixture(p); put16(p + MAP + 26, 0xffff); invalid(p, END);
    fixture(p); put16(p + MAP + 26, 28); invalid(p, END);
    fixture(p); put16(p + TYPES, 0xfffe); invalid(p, END);
    fixture(p); put16(p + TYPES + 6, 0xfffe); invalid(p, END);
    fixture(p); put16(p + TYPES + 8, 0xffff); invalid(p, END);
    fixture(p); put16(p + TYPES + 8, 0); invalid(p, END);
    fixture(p); put16(p + TYPES + 16, 18); invalid(p, END); /* ref overlap */
    fixture(p); p[REFS + 5] = 0xff; invalid(p, END);
    fixture(p); p[REFS + 7] = 23; invalid(p, END);
    fixture(p); put32(p + DATA, 0xffffffff); invalid(p, END);
    fixture(p); put32(p + DATA, 21); invalid(p, END);
    fixture(p); put16(p + REFS + 2, 5); invalid(p, END);
    fixture(p); p[NAMES] = 255; invalid(p, END);
}

static void mutations(void) {
    uint8_t p[END];
    uint32_t seed = 0x73656564;
    for (unsigned i = 0; i < 20000; i++) {
        fixture(p);
        for (unsigned j = 0; j < 4; j++) {
            seed = seed * 1664525u + 1013904223u;
            size_t offset = seed % END;
            seed = seed * 1664525u + 1013904223u;
            p[offset] = (uint8_t)(seed >> 24);
        }
        MacResourceFork f;
        if (mac_resource_open(&f, p, END, NULL, 0) == 0) {
            for (size_t j = 0; j < mac_resource_count(&f); j++) {
                MacResource r;
                CHECK(mac_resource_at(&f, j, &r) == 0);
                CHECK(r.data >= p && r.data <= p + END);
                CHECK(r.size <= (size_t)(p + END - r.data));
                CHECK(mac_resource_find(&f, r.type, r.id, &r) == 0);
            }
        }
    }
}

static void containers(void) {
    uint8_t p[50 + END];
    MacResourceFork f;
    MacResource r;
    memset(p, 0, sizeof p);
    put32(p, 0x00051607);
    put32(p + 4, 0x00020000);
    put16(p + 24, 1);
    put32(p + 26, 2);
    put32(p + 30, 50);
    put32(p + 34, END);
    fixture(p + 50);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == 0);
    CHECK(mac_resource_count(&f) == 3);
    CHECK(mac_resource_at(&f, 0, &r) == 0 && r.size == 3);
    CHECK(mac_resource_open_container(&f, p + 50, END, NULL, 0) == 0);
    for (size_t n = 0; n < sizeof p; n++) {
        CHECK(mac_resource_open_container(&f, p, n, NULL, 0) == -1);
    }
    put32(p, 0x00051600);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == 0);
    put32(p + 4, 0x00010000);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == -1);
    put32(p + 4, 0x00020000);
    put16(p + 24, 0xffff);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == -1);
    put16(p + 24, 1);
    put32(p + 26, 9);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == -1);
    put32(p + 26, 2);
    put32(p + 30, 25);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == -1);
    put32(p + 30, 50);
    put32(p + 34, 0xffffffff);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == -1);
    put32(p + 34, END);
    put16(p + 24, 2);
    memcpy(p + 38, p + 26, 12);
    CHECK(mac_resource_open_container(&f, p, sizeof p, NULL, 0) == -1);
}

int main(void) {
    uint8_t p[END];
    MacResourceFork f;
    MacResource r;
    char err[120];
    fixture(p);
    CHECK(mac_resource_open(&f, p, END, err, sizeof err) == 0);
    CHECK(err[0] == 0);
    CHECK(mac_resource_count(&f) == 3);
    CHECK(mac_resource_at(&f, 0, &r) == 0);
    CHECK(r.type == MAC_RESOURCE_TYPE('C', 'O', 'D', 'E'));
    CHECK(r.id == 0 && r.attributes == 0x24);
    CHECK(r.size == 3 && memcmp(r.data, "abc", 3) == 0);
    CHECK(r.name_size == 4 && memcmp(r.name, "Demo", 4) == 0);
    CHECK(mac_resource_at(&f, 1, &r) == 0);
    CHECK(r.id == 1 && r.size == 0 && r.name == NULL && r.name_size == 0);
    CHECK(mac_resource_at(&f, 2, &r) == 0);
    CHECK(r.id == -128 && r.size == 2 && memcmp(r.data, "xy", 2) == 0);
    CHECK(mac_resource_find(&f, MAC_RESOURCE_TYPE('S', 'T', 'R', ' '), -128, &r) == 0);
    CHECK(r.id == -128);
    CHECK(mac_resource_find(&f, MAC_RESOURCE_TYPE('S', 'T', 'R', ' '), 128, &r) == 1);
    CHECK(r.data == NULL && r.size == 0);
    CHECK(mac_resource_at(&f, 3, &r) == 1);
    CHECK(mac_resource_at(&f, SIZE_MAX, &r) == 1);
    CHECK(mac_resource_at(NULL, 0, &r) == -1);
    CHECK(mac_resource_at(&f, 0, NULL) == -1);
    CHECK(mac_resource_find(NULL, 0, 0, &r) == -1);
    CHECK(mac_resource_find(&f, 0, 0, NULL) == -1);
    CHECK(mac_resource_count(NULL) == 0);
    CHECK(mac_resource_open(NULL, p, END, NULL, 0) == -1);
    invalid(NULL, END);
    fixture(p);
    put16(p + TYPES, 0xffff); /* empty fork */
    put16(p + MAP + 26, 30);
    put32(p + 12, 30);
    CHECK(mac_resource_open(&f, p, MAP + 30, NULL, 0) == 0);
    CHECK(mac_resource_count(&f) == 0);
    CHECK(mac_resource_at(&f, 0, &r) == 1);
    malformed();
    mutations();
    containers();
    if (fails) {
        fprintf(stderr, "%d resource check(s) failed\n", fails);
        return 1;
    }
    puts("resource checks ok (including 20000 deterministic mutations)");
    return 0;
}
