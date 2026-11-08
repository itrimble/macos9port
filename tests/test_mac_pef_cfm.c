#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

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

typedef struct {
    uint32_t arch;
    uint32_t format_version;
    uint16_t section_count;
    uint16_t inst_section_count;
    uint32_t code_offset;
    uint32_t code_size;
} PefContainerInfo;

static int pef_validate_container(const uint8_t *buf, size_t len, PefContainerInfo *out) {
    if (!buf || !out || len < 40) return -1;
    if (get32(buf + 0) != FOURCC('J', 'o', 'y', '!')) return -1;
    if (get32(buf + 4) != FOURCC('p', 'e', 'f', 'f')) return -1;
    uint32_t arch = get32(buf + 8);
    if (arch != FOURCC('p', 'w', 'p', 'c') && arch != FOURCC('m', '6', '8', 'k')) return -1;
    uint32_t ver = get32(buf + 12);
    if (ver != 1) return -1;

    uint16_t sec_cnt = get16(buf + 32);
    uint16_t inst_cnt = get16(buf + 34);
    if (sec_cnt == 0 || sec_cnt > 16 || inst_cnt > sec_cnt) return -1;
    if (40u + (size_t)sec_cnt * 28u > len) return -1;

    memset(out, 0, sizeof(*out));
    out->arch = arch;
    out->format_version = ver;
    out->section_count = sec_cnt;
    out->inst_section_count = inst_cnt;

    for (uint16_t i = 0; i < sec_cnt; i++) {
        const uint8_t *sh = buf + 40u + (size_t)i * 28u;
        uint32_t total_sz = get32(sh + 8);
        uint32_t unpk_sz = get32(sh + 12);
        uint32_t pack_sz = get32(sh + 16);
        uint32_t off = get32(sh + 20);
        uint8_t kind = sh[24];
        if (unpk_sz > total_sz || pack_sz > unpk_sz) return -1;
        if ((uint64_t)off + pack_sz > len) return -1;
        if (kind == 0) { /* Code section */
            out->code_offset = off;
            out->code_size = pack_sz;
        }
    }
    return 0;
}

typedef struct {
    uint32_t arch;
    uint8_t usage;
    uint8_t where;
    uint32_t offset;
    uint32_t length;
    char name[64];
} CfragMemberInfo;

static int cfrag_parse_first_member(const uint8_t *buf, size_t len, CfragMemberInfo *out) {
    if (!buf || !out || len < 32 + 42) return -1;
    uint16_t count = get16(buf + 30);
    if (count == 0) return -1;
    const uint8_t *m = buf + 32;
    uint16_t rec_len = get16(m + 8);
    if (rec_len < 42 || 32u + rec_len > len) return -1;

    memset(out, 0, sizeof(*out));
    out->arch = get32(m + 0);
    out->usage = m[26];
    out->where = m[27];
    out->offset = get32(m + 28);
    out->length = get32(m + 32);
    uint8_t nlen = m[41];
    if (42u + nlen > rec_len || nlen >= sizeof(out->name)) return -1;
    memcpy(out->name, m + 42, nlen);
    out->name[nlen] = '\0';
    return 0;
}

typedef struct {
    uint16_t trap_word;
    uint8_t version;
    uint8_t isa;
    uint32_t proc_info;
    uint32_t proc_ptr;
} MixedModeUpp;

static int mixed_mode_validate_upp(const uint8_t *buf, size_t len, MixedModeUpp *out) {
    if (!buf || !out || len < 20) return -1;
    uint16_t trap = get16(buf + 0);
    uint8_t ver = buf[2];
    if (trap != 0xAAFEu || ver != 7) return -1;
    out->trap_word = trap;
    out->version = ver;
    out->proc_info = get32(buf + 8);
    out->proc_ptr = get32(buf + 12);
    out->isa = buf[16];
    if (out->isa > 1 || out->proc_ptr == 0) return -1;
    return 0;
}

static int pef_check_launch_gate(const PefContainerInfo *pef, int ppc_backend_enabled,
                                 char *err, size_t err_cap) {
    if (!pef) return -1;
    if (pef->arch == FOURCC('p', 'w', 'p', 'c') && !ppc_backend_enabled) {
        if (err && err_cap > 0) {
            snprintf(err, err_cap,
                     "PowerPC PEF container requires PPC backend (68k-only runtime)");
        }
        return -1;
    }
    return 0;
}

static void test_pef_cfrag_and_mixed_mode(void) {
    uint8_t pef[128];
    memset(pef, 0, sizeof(pef));
    memcpy(pef + 0, "Joy!peffpwpc", 12);
    put32(pef + 12, 1);  /* formatVersion = 1 */
    put16(pef + 32, 1);  /* 1 section */
    put16(pef + 34, 1);  /* 1 instantiated section */

    uint8_t *sh = pef + 40;
    put32(sh + 8, 32);   /* totalSize */
    put32(sh + 12, 32);  /* unpackedSize */
    put32(sh + 16, 32);  /* packedSize */
    put32(sh + 20, 68);  /* containerOffset */
    sh[24] = 0;          /* code section */

    PefContainerInfo info;
    CHECK(pef_validate_container(pef, sizeof(pef), &info) == 0);
    CHECK(info.arch == FOURCC('p', 'w', 'p', 'c'));
    CHECK(info.code_offset == 68 && info.code_size == 32);

    /* R06/R07 Gate: PowerPC PEF must fail closed with clear diagnostic when PPC backend is off. */
    char err[128] = {0};
    CHECK(pef_check_launch_gate(&info, 0, err, sizeof(err)) == -1);
    CHECK(strstr(err, "PowerPC PEF container requires PPC backend") != NULL);
    CHECK(pef_check_launch_gate(&info, 1, err, sizeof(err)) == 0);

    /* Corrupt section offset + size exceeding container must fail. */
    put32(sh + 16, 999);
    CHECK(pef_validate_container(pef, sizeof(pef), &info) == -1);

    /* Synthetic 'cfrag' #0 resource check. */
    uint8_t cfrag[96];
    memset(cfrag, 0, sizeof(cfrag));
    put16(cfrag + 30, 1); /* 1 member */
    memcpy(cfrag + 32, "pwpc", 4);
    put16(cfrag + 32 + 8, 52); /* member record length */
    cfrag[32 + 26] = 0;        /* kApplicationCFrag */
    cfrag[32 + 27] = 1;        /* kDataForkCFragLocator */
    put32(cfrag + 32 + 28, 0);
    put32(cfrag + 32 + 32, 128);
    cfrag[32 + 41] = 9;
    memcpy(cfrag + 32 + 42, "QuickTime", 9);

    CfragMemberInfo mem;
    CHECK(cfrag_parse_first_member(cfrag, sizeof(cfrag), &mem) == 0);
    CHECK(mem.arch == FOURCC('p', 'w', 'p', 'c') && strcmp(mem.name, "QuickTime") == 0);
    CHECK(mem.where == 1 && mem.length == 128);

    /* Mixed-Mode UniversalProcPtr (0xAAFE, version 7) check. */
    uint8_t upp_bytes[20] = {0};
    put16(upp_bytes + 0, 0xAAFEu);
    upp_bytes[2] = 7;
    put32(upp_bytes + 8, 0x000003F0u);
    put32(upp_bytes + 12, 0x00102000u);
    upp_bytes[16] = 1; /* kPowerPCISA */

    MixedModeUpp upp;
    CHECK(mixed_mode_validate_upp(upp_bytes, sizeof(upp_bytes), &upp) == 0);
    CHECK(upp.trap_word == 0xAAFEu && upp.isa == 1 && upp.proc_ptr == 0x00102000u);
    upp_bytes[2] = 6; /* wrong version */
    CHECK(mixed_mode_validate_upp(upp_bytes, sizeof(upp_bytes), &upp) == -1);
}

int main(void) {
    test_pef_cfrag_and_mixed_mode();

    if (failures > 0) {
        fprintf(stderr, "test_mac_pef_cfm: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_pef_cfm: all %d checks passed\n", checks);
    return 0;
}
