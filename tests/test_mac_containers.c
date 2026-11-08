#include "mac_resource.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

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

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Build a minimal valid classic resource fork containing one 'TEST' #128 resource. */
static size_t build_minimal_fork(uint8_t *buf, size_t cap, const char *payload) {
    size_t pay_len = strlen(payload);
    size_t data_off = 256;
    size_t data_len = 4 + pay_len;
    size_t map_off = data_off + data_len;
    size_t map_len = 28 + 2 + 8 + 12;
    if (map_off + map_len > cap) return 0;

    memset(buf, 0, map_off + map_len);
    put32(buf + 0, (uint32_t)data_off);
    put32(buf + 4, (uint32_t)map_off);
    put32(buf + 8, (uint32_t)data_len);
    put32(buf + 12, (uint32_t)map_len);

    put32(buf + data_off, (uint32_t)pay_len);
    memcpy(buf + data_off + 4, payload, pay_len);

    uint8_t *map = buf + map_off;
    put16(map + 24, 28);
    put16(map + 26, (uint16_t)map_len);
    put16(map + 28, 0); /* 1 type minus 1 */
    memcpy(map + 30, "TEST", 4);
    put16(map + 34, 0); /* 1 resource minus 1 */
    put16(map + 36, 10); /* ref list offset from type list */

    uint8_t *ref = map + 38;
    put16(ref + 0, 128);
    put16(ref + 2, 0xFFFFu);
    ref[4] = 0; /* attributes + 24-bit data offset 0 */
    ref[5] = 0;
    ref[6] = 0;
    ref[7] = 0;
    return map_off + map_len;
}

static void test_applesingle_appledouble_containers(void) {
    uint8_t fork_bytes[512];
    size_t fork_len = build_minimal_fork(fork_bytes, sizeof(fork_bytes), "OS999-CONTAINER");
    CHECK(fork_len > 0);

    /* AppleDouble (0x00051607) and AppleSingle (0x00051600) with 2 entries:
       Entry 9 (Finder Info, 32 bytes) + Entry 2 (Resource Fork). */
    const uint32_t magics[2] = { 0x00051600u, 0x00051607u };
    for (int m = 0; m < 2; m++) {
        uint8_t container[1024];
        memset(container, 0, sizeof(container));
        put32(container + 0, magics[m]);
        put32(container + 4, 0x00020000u);
        put16(container + 24, 2); /* 2 entries */

        uint32_t finfo_off = 26 + 2 * 12;
        uint32_t rfork_off = finfo_off + 32;
        put32(container + 26, 9);
        put32(container + 30, finfo_off);
        put32(container + 34, 32);
        put32(container + 38, 2);
        put32(container + 42, rfork_off);
        put32(container + 46, (uint32_t)fork_len);
        memcpy(container + rfork_off, fork_bytes, fork_len);

        MacResourceFork rf;
        char err[128] = {0};
        CHECK(mac_resource_open_container(&rf, container, rfork_off + fork_len,
                                          err, sizeof(err)) == 0);
        MacResource res;
        CHECK(mac_resource_find(&rf, MAC_RESOURCE_TYPE('T', 'E', 'S', 'T'), 128, &res) == 0);
        CHECK(res.size == 15 && memcmp(res.data, "OS999-CONTAINER", 15) == 0);

        /* Duplicate entry ID must fail. */
        put32(container + 26, 2);
        CHECK(mac_resource_open_container(&rf, container, rfork_off + fork_len,
                                          err, sizeof(err)) == -1);
        put32(container + 26, 9);

        /* Missing resource fork entry (only entry 9) must fail. */
        put32(container + 38, 1);
        CHECK(mac_resource_open_container(&rf, container, rfork_off + fork_len,
                                          err, sizeof(err)) == -1);
        put32(container + 38, 2);

        /* Out-of-bounds entry length must fail. */
        put32(container + 46, (uint32_t)fork_len + 100u);
        CHECK(mac_resource_open_container(&rf, container, rfork_off + fork_len,
                                          err, sizeof(err)) == -1);
        put32(container + 46, (uint32_t)fork_len);

        /* Unsupported AppleSingle version must fail. */
        put32(container + 4, 0x00010000u);
        CHECK(mac_resource_open_container(&rf, container, rfork_off + fork_len,
                                          err, sizeof(err)) == -1);
    }
}

/* Clean-room CRC-16 CCITT (polynomial 0x1021) for MacBinary II header bytes 0..123. */
static uint16_t macbinary_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

typedef struct {
    char name[64];
    uint32_t type;
    uint32_t creator;
    const uint8_t *data_fork;
    uint32_t data_len;
    const uint8_t *rsrc_fork;
    uint32_t rsrc_len;
} MacBinaryView;

static int macbinary_validate(const uint8_t *buf, size_t len, MacBinaryView *out) {
    if (!buf || !out || len < 128) return -1;
    if (buf[0] != 0 || buf[74] != 0 || buf[82] != 0) return -1;
    uint8_t nlen = buf[1];
    if (nlen == 0 || nlen > 63) return -1;
    if (macbinary_crc16(buf, 124) != get16(buf + 124)) return -1;

    uint32_t dlen = get32(buf + 83);
    uint32_t rlen = get32(buf + 87);
    if (dlen > 0x00FFFFFFu || rlen > 0x00FFFFFFu) return -1;
    size_t dpad = ((size_t)dlen + 127u) & ~127u;
    size_t rpad = ((size_t)rlen + 127u) & ~127u;
    if (128u + dpad + rpad > len) return -1;

    memset(out, 0, sizeof(*out));
    memcpy(out->name, buf + 2, nlen);
    out->name[nlen] = '\0';
    out->type = get32(buf + 65);
    out->creator = get32(buf + 69);
    out->data_fork = buf + 128;
    out->data_len = dlen;
    out->rsrc_fork = buf + 128 + dpad;
    out->rsrc_len = rlen;
    return 0;
}

static void test_macbinary_ii_validator(void) {
    uint8_t fork_bytes[512];
    size_t fork_len = build_minimal_fork(fork_bytes, sizeof(fork_bytes), "MBII-RSRC");
    CHECK(fork_len > 0);

    uint8_t mb[1024];
    memset(mb, 0, sizeof(mb));
    mb[1] = 9;
    memcpy(mb + 2, "GliderPRO", 9);
    memcpy(mb + 65, "APPL", 4);
    memcpy(mb + 69, "GLDR", 4);
    put32(mb + 83, 6); /* 6-byte data fork */
    put32(mb + 87, (uint32_t)fork_len);
    mb[122] = 129; /* MacBinary II version */
    mb[123] = 129;
    put16(mb + 124, macbinary_crc16(mb, 124));

    memcpy(mb + 128, "DATAOK", 6);
    size_t rsrc_off = 128 + 128;
    memcpy(mb + rsrc_off, fork_bytes, fork_len);
    size_t total_len = rsrc_off + ((fork_len + 127u) & ~127u);

    MacBinaryView view;
    CHECK(macbinary_validate(mb, total_len, &view) == 0);
    CHECK(strcmp(view.name, "GliderPRO") == 0);
    CHECK(view.type == MAC_RESOURCE_TYPE('A', 'P', 'P', 'L'));
    CHECK(view.creator == MAC_RESOURCE_TYPE('G', 'L', 'D', 'R'));
    CHECK(view.data_len == 6 && memcmp(view.data_fork, "DATAOK", 6) == 0);

    MacResourceFork rf;
    CHECK(mac_resource_open(&rf, view.rsrc_fork, view.rsrc_len, NULL, 0) == 0);
    MacResource res;
    CHECK(mac_resource_find(&rf, MAC_RESOURCE_TYPE('T', 'E', 'S', 'T'), 128, &res) == 0);
    CHECK(res.size == 9 && memcmp(res.data, "MBII-RSRC", 9) == 0);

    /* Corrupt CRC must be rejected. */
    mb[125] ^= 0xFFu;
    CHECK(macbinary_validate(mb, total_len, &view) == -1);
    mb[125] ^= 0xFFu;

    /* Invalid filename length > 63 must be rejected. */
    mb[1] = 64;
    put16(mb + 124, macbinary_crc16(mb, 124));
    CHECK(macbinary_validate(mb, total_len, &view) == -1);
}

/* Clean-room Disk Copy 4.2 checksum: 32-bit add + 1-bit right rotate per 16-bit word. */
static uint32_t dc42_checksum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += get16(data + i);
        sum = (sum >> 1) | ((sum & 1u) << 31);
    }
    return sum;
}

static int dc42_validate(const uint8_t *buf, size_t len) {
    if (!buf || len < 84) return -1;
    uint8_t nlen = buf[0];
    if (nlen == 0 || nlen > 63) return -1;
    if (get16(buf + 82) != 0x0100u) return -1;
    uint32_t data_size = get32(buf + 64);
    uint32_t tag_size = get32(buf + 68);
    if (data_size == 0 || (data_size % 512u) != 0) return -1;
    if (84ull + data_size + tag_size != len) return -1;
    if (dc42_checksum(buf + 84, data_size) != get32(buf + 72)) return -1;
    if (tag_size > 0 && dc42_checksum(buf + 84 + data_size, tag_size) != get32(buf + 76)) return -1;
    return 0;
}

static void test_disk_copy_42_validator(void) {
    uint8_t image[84 + 1024 + 24];
    memset(image, 0, sizeof(image));
    image[0] = 8;
    memcpy(image + 1, "OS999HD1", 8);
    put32(image + 64, 1024); /* 2 sectors */
    put32(image + 68, 24);   /* 12 bytes tag per sector */
    image[80] = 0;           /* GCR 400K encoding */
    image[81] = 0x02;
    put16(image + 82, 0x0100u);

    for (size_t i = 0; i < 1024; i++) image[84 + i] = (uint8_t)((i * 7u + 3u) & 0xFFu);
    for (size_t i = 0; i < 24; i++) image[84 + 1024 + i] = (uint8_t)(0xA0u + i);

    put32(image + 72, dc42_checksum(image + 84, 1024));
    put32(image + 76, dc42_checksum(image + 84 + 1024, 24));

    CHECK(dc42_validate(image, sizeof(image)) == 0);

    /* Corrupt sector byte must fail data checksum. */
    image[100] ^= 0x01u;
    CHECK(dc42_validate(image, sizeof(image)) == -1);
    image[100] ^= 0x01u;

    /* Bad private word must fail. */
    put16(image + 82, 0x0000u);
    CHECK(dc42_validate(image, sizeof(image)) == -1);
}

int main(void) {
    test_applesingle_appledouble_containers();
    test_macbinary_ii_validator();
    test_disk_copy_42_validator();

    if (failures > 0) {
        fprintf(stderr, "test_mac_containers: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_containers: all %d checks passed\n", checks);
    return 0;
}
