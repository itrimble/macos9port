#include "mac_cfm.h"

#include <stdio.h>

/* PEF relocation instructions: a stream of 16-bit words interpreted against a
 * small machine state. Every relocation adds a value to the 32-bit word at the
 * current position inside the section being relocated. Two-word instructions
 * carry the low sixteen bits of their operand in the second word. */

typedef struct {
    MacCfmFragment *f;
    uint8_t *base;       /* host pointer to the section being relocated */
    uint32_t size, pos;  /* section size and current offset */
    uint32_t import;     /* next import for the import-run forms */
    uint32_t sect_c, sect_d;
} Reloc;

#define RELOC_STEP_LIMIT (UINT64_C(1) << 26) /* bounds the work a hostile repeat can demand */

static int bad(Reloc *r, const char *message) {
    snprintf(r->f->error, sizeof r->f->error, "%s", message);
    return -1;
}

static int add(Reloc *r, uint32_t delta) {
    if (r->pos > r->size || r->size - r->pos < 4) return bad(r, "PEF relocation lands outside its section.");
    uint8_t *p = r->base + r->pos;
    uint32_t v = (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]) + delta;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
    r->pos += 4;
    return 0;
}

static int skip(Reloc *r, uint64_t bytes) {
    if (r->pos > r->size || bytes > r->size - r->pos) return bad(r, "PEF relocation position leaves its section.");
    r->pos += (uint32_t)bytes;
    return 0;
}

/* Amount added for a reference to section `index`: where it was placed minus where it was linked. */
static int section_delta(Reloc *r, uint32_t index, uint32_t *delta) {
    if (index >= r->f->pef.section_count || !r->f->section_address[index])
        return bad(r, "PEF relocation refers to a section that is not in memory.");
    *delta = r->f->section_address[index] - r->f->pef.sections[index].default_address;
    return 0;
}

static int add_import(Reloc *r, uint32_t index) {
    if (index >= r->f->import_count) return bad(r, "PEF relocation refers to an import that does not exist.");
    r->import = index + 1;
    return add(r, r->f->imports[index].address);
}

static int run(Reloc *r, unsigned sub, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        int rc;
        switch (sub) {
        case 0: rc = add(r, r->sect_c); break;                                           /* by section C */
        case 1: rc = add(r, r->sect_d); break;                                           /* by section D */
        case 2: rc = add(r, r->sect_c) || add(r, r->sect_d) || skip(r, 4); break;        /* 12-byte transition vector */
        case 3: rc = add(r, r->sect_c) || add(r, r->sect_d); break;                      /* 8-byte transition vector */
        case 4: rc = add(r, r->sect_d) || skip(r, 4); break;                             /* 8-byte vtable entry */
        case 5: rc = add_import(r, r->import); break;                                    /* import run */
        default: return bad(r, "Unknown PEF run relocation.");
        }
        if (rc) return -1;
    }
    return 0;
}

static int indexed(Reloc *r, unsigned sub, uint32_t index) {
    uint32_t delta;
    switch (sub) {
    case 0: return add_import(r, index);                                                 /* by import */
    case 1: if (section_delta(r, index, &delta)) return -1; r->sect_c = delta; return 0; /* set section C */
    case 2: if (section_delta(r, index, &delta)) return -1; r->sect_d = delta; return 0; /* set section D */
    case 3: return section_delta(r, index, &delta) ? -1 : add(r, delta);                 /* by section */
    default: return bad(r, "Unknown PEF indexed relocation.");
    }
}

int mac_cfm_relocate(MacCfmFragment *f, const MacPefRelocations *in) {
    Reloc r = {f, NULL, 0, 0, 0, 0, 0};
    if (in->section >= f->pef.section_count || !f->section_address[in->section])
        return bad(&r, "PEF relocations target a section that is not in memory.");
    r.size = f->pef.sections[in->section].total_size;
    r.base = ppc_mem_span(f->cpu, f->section_address[in->section], r.size);
    if (!r.base) return bad(&r, "PEF relocations target a section outside guest RAM.");
    /* Until an instruction says otherwise, section C is the first section and section D the second. */
    for (unsigned i = 0; i < 2 && i < f->pef.section_count; i++)
        if (f->section_address[i])
            *(i ? &r.sect_d : &r.sect_c) = f->section_address[i] - f->pef.sections[i].default_address;
    uint32_t i = 0, repeat_at = UINT32_MAX, repeat_left = 0;
    uint64_t steps = 0;
    while (i < in->count) {
        uint32_t w = ((uint32_t)in->words[2 * i] << 8) | in->words[2 * i + 1], low = 0;
        int two = (w >> 13) == 5; /* 101x xxxx: every large form takes a second word */
        if (++steps > RELOC_STEP_LIMIT) return bad(&r, "PEF relocation stream does not terminate in bounded work.");
        if (two) {
            if (i + 1 >= in->count) return bad(&r, "PEF relocation stream ends inside a two-word instruction.");
            low = ((uint32_t)in->words[2 * i + 2] << 8) | in->words[2 * i + 3];
        }
        uint32_t length = two ? 2u : 1u, blocks = 0, times = 0;
        int rc = 0, is_repeat = 0;
        if ((w >> 14) == 0) rc = skip(&r, 4u * ((w >> 6) & 0xffu)) || run(&r, 1, w & 0x3fu);  /* by section D with skip */
        else if ((w >> 13) == 2) rc = run(&r, (w >> 9) & 0xfu, (w & 0x1ffu) + 1u);
        else if ((w >> 13) == 3) rc = indexed(&r, (w >> 9) & 0xfu, w & 0x1ffu);
        else if ((w >> 12) == 8) rc = skip(&r, (w & 0xfffu) + 1u);                           /* increment position */
        else if ((w >> 12) == 9) { is_repeat = 1; blocks = ((w >> 8) & 0xfu) + 1u; times = (w & 0xffu) + 1u; }
        else if ((w >> 10) == 0x28) {                                                        /* set position */
            r.pos = ((w & 0x3ffu) << 16) | low;
            if (r.pos > r.size) rc = bad(&r, "PEF relocation position leaves its section.");
        } else if ((w >> 10) == 0x29) rc = add_import(&r, ((w & 0x3ffu) << 16) | low);        /* large by import */
        else if ((w >> 10) == 0x2c) { is_repeat = 1; blocks = ((w >> 6) & 0xfu) + 1u; times = ((w & 0x3fu) << 16) | low; }
        else if ((w >> 10) == 0x2d) {                                                        /* large set or by section */
            static const uint8_t map[3] = {3, 1, 2}; /* by section, set C, set D */
            unsigned sub = (w >> 6) & 0xfu;
            rc = sub < 3 ? indexed(&r, map[sub], ((w & 0x3fu) << 16) | low) : bad(&r, "Unknown PEF large relocation.");
        } else rc = bad(&r, "Unknown PEF relocation instruction.");
        if (rc) return -1;
        if (!is_repeat) { i += length; continue; }
        /* Repeat: run the preceding `blocks` words `times` more times. The counter belongs to this instruction. */
        if (repeat_at != i) {
            if (repeat_at != UINT32_MAX) return bad(&r, "Nested PEF relocation repeats are not supported.");
            if (blocks > i) return bad(&r, "PEF relocation repeat reaches before the start of the stream.");
            repeat_at = i;
            repeat_left = times;
        }
        if (repeat_left) { repeat_left--; i -= blocks; }
        else { repeat_at = UINT32_MAX; i += length; }
    }
    return 0;
}
