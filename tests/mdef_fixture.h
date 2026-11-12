/* A synthetic menu definition procedure for tests, assembled by hand into 68K machine code (no bytes come from a real program), plus MENU
 * resources that name it. Include after tests/fork_builder.h.
 *
 * The procedure logs every call and answers each message in a simple way. It finds its log through the long at $A78 (ApplScratch): a block of
 * 32-byte slots, one per message number. Slot layout: +0 call count, +2 hitPt.v, +4 hitPt.h, +6 menuRect (top, left, bottom, right) on entry,
 * +14 theMenu, +18 whichItem on entry. Its behaviour:
 *   draw (0)   stores the byte $55 at the address in the long at $A7C
 *   choose (1) three 16-pixel items stacked from menuRect.top; sets whichItem to the item under hitPt, or 0 outside the rectangle
 *   size (2)   sets menuWidth 80 and menuHeight 48 in the menu record
 *   pop-up (3) puts menuRect at (hitPt.v - 16 * (whichItem - 1), hitPt.h) with the size above
 * It then pops its 18 bytes of arguments the way a Pascal routine does. */
#ifndef TESTS_MDEF_FIXTURE_H
#define TESTS_MDEF_FIXTURE_H
#include <stdint.h>
#include <string.h>
#define MDEF_ITEM_H 16
#define MDEF_W 80
#define MDEF_H 48
#define MDEF_SLOT(log, message) ((log) + 32u * (unsigned)(message))

typedef struct { uint16_t w[160]; unsigned n, label[8], fx_at[16]; int fx_lab[16]; unsigned nfx; } MdefAsm;
static void ma_w(MdefAsm *a, unsigned w) { a->w[a->n++] = (uint16_t)w; }
static void ma_br(MdefAsm *a, unsigned cond, int label) { a->fx_at[a->nfx] = a->n; a->fx_lab[a->nfx++] = label; ma_w(a, cond << 8); }
static void ma_here(MdefAsm *a, int label) { a->label[label] = a->n; }
enum { L_DRAW, L_CHOOSE, L_SIZE, L_POPUP, L_NONE, L_RET };
/* Writes the code to `out` (big-endian); returns its length, or 0 if a branch does not fit. */
static size_t mdef_image(uint8_t *out) {
    MdefAsm a; memset(&a, 0, sizeof a);
    ma_w(&a, 0x302F); ma_w(&a, 20);                      /* MOVE.W 20(A7),D0        message */
    ma_w(&a, 0xEB48);                                    /* LSL.W #5,D0 */
    ma_w(&a, 0x2078); ma_w(&a, 0x0A78);                  /* MOVEA.L $A78.W,A0       the log */
    ma_w(&a, 0xD0C0);                            /* ADDA.W D0,A0 */
    ma_w(&a, 0x5250);                                    /* ADDQ.W #1,(A0) */
    ma_w(&a, 0x316F); ma_w(&a, 8); ma_w(&a, 2);          /* MOVE.W 8(A7),2(A0)      hitPt.v */
    ma_w(&a, 0x316F); ma_w(&a, 10); ma_w(&a, 4);         /* MOVE.W 10(A7),4(A0)     hitPt.h */
    ma_w(&a, 0x226F); ma_w(&a, 12);                      /* MOVEA.L 12(A7),A1       menuRect */
    ma_w(&a, 0x2159); ma_w(&a, 6);                       /* MOVE.L (A1)+,6(A0) */
    ma_w(&a, 0x2159); ma_w(&a, 10);                      /* MOVE.L (A1)+,10(A0) */
    ma_w(&a, 0x216F); ma_w(&a, 16); ma_w(&a, 14);        /* MOVE.L 16(A7),14(A0)    theMenu */
    ma_w(&a, 0x226F); ma_w(&a, 4);                       /* MOVEA.L 4(A7),A1        whichItem */
    ma_w(&a, 0x3151); ma_w(&a, 18);                      /* MOVE.W (A1),18(A0) */
    ma_w(&a, 0x302F); ma_w(&a, 20);                      /* MOVE.W 20(A7),D0 */
    ma_br(&a, 0x67, L_DRAW);                             /* BEQ draw */
    ma_w(&a, 0x5340); ma_br(&a, 0x67, L_CHOOSE);         /* SUBQ.W #1,D0; BEQ choose */
    ma_w(&a, 0x5340); ma_br(&a, 0x67, L_SIZE);
    ma_w(&a, 0x5340); ma_br(&a, 0x67, L_POPUP);
    ma_here(&a, L_RET);                                  /* any other message: nothing but the log */
    ma_w(&a, 0x205F);                                    /* MOVEA.L (A7)+,A0 */
    ma_w(&a, 0x4FEF); ma_w(&a, 18);                      /* LEA 18(A7),A7 */
    ma_w(&a, 0x4ED0);                                    /* JMP (A0) */
    ma_here(&a, L_DRAW);
    ma_w(&a, 0x2278); ma_w(&a, 0x0A7C);                  /* MOVEA.L $A7C.W,A1 */
    ma_w(&a, 0x12BC); ma_w(&a, 0x0055);                  /* MOVE.B #$55,(A1) */
    ma_br(&a, 0x60, L_RET);
    ma_here(&a, L_SIZE);
    ma_w(&a, 0x206F); ma_w(&a, 16);                      /* MOVEA.L 16(A7),A0       the handle */
    ma_w(&a, 0x2050);                            /* MOVEA.L (A0),A0         the record */
    ma_w(&a, 0x317C); ma_w(&a, MDEF_W); ma_w(&a, 2);     /* MOVE.W #80,2(A0) */
    ma_w(&a, 0x317C); ma_w(&a, MDEF_H); ma_w(&a, 4);     /* MOVE.W #48,4(A0) */
    ma_br(&a, 0x60, L_RET);
    ma_here(&a, L_POPUP);
    ma_w(&a, 0x206F); ma_w(&a, 12);                      /* MOVEA.L 12(A7),A0 */
    ma_w(&a, 0x226F); ma_w(&a, 4);                       /* MOVEA.L 4(A7),A1 */
    ma_w(&a, 0x3011);                                    /* MOVE.W (A1),D0          the item */
    ma_w(&a, 0x5340);                                    /* SUBQ.W #1,D0 */
    ma_w(&a, 0xE948);                                    /* LSL.W #4,D0 */
    ma_w(&a, 0x322F); ma_w(&a, 8);                       /* MOVE.W 8(A7),D1         hitPt.v */
    ma_w(&a, 0x9240);                                    /* SUB.W D0,D1 */
    ma_w(&a, 0x3081);                                    /* MOVE.W D1,(A0)          top */
    ma_w(&a, 0x0641); ma_w(&a, MDEF_H);                  /* ADDI.W #48,D1 */
    ma_w(&a, 0x3141); ma_w(&a, 4);                       /* MOVE.W D1,4(A0)         bottom */
    ma_w(&a, 0x322F); ma_w(&a, 10);                      /* MOVE.W 10(A7),D1        hitPt.h */
    ma_w(&a, 0x3141); ma_w(&a, 2);                       /* MOVE.W D1,2(A0)         left */
    ma_w(&a, 0x0641); ma_w(&a, MDEF_W);                  /* ADDI.W #80,D1 */
    ma_w(&a, 0x3141); ma_w(&a, 6);                       /* MOVE.W D1,6(A0)         right */
    ma_br(&a, 0x60, L_RET);
    ma_here(&a, L_CHOOSE);
    ma_w(&a, 0x206F); ma_w(&a, 12);                      /* MOVEA.L 12(A7),A0       menuRect */
    ma_w(&a, 0x226F); ma_w(&a, 4);                       /* MOVEA.L 4(A7),A1        whichItem */
    ma_w(&a, 0x7200);                                    /* MOVEQ #0,D1             answer: none */
    ma_w(&a, 0x302F); ma_w(&a, 8);                       /* MOVE.W 8(A7),D0         hitPt.v */
    ma_w(&a, 0x9050);                                    /* SUB.W (A0),D0           minus top */
    ma_br(&a, 0x6D, L_NONE);                             /* BLT none */
    ma_w(&a, 0x3428); ma_w(&a, 4);                       /* MOVE.W 4(A0),D2 */
    ma_w(&a, 0x9450);                                    /* SUB.W (A0),D2           height */
    ma_w(&a, 0xB042);                                    /* CMP.W D2,D0 */
    ma_br(&a, 0x6C, L_NONE);                             /* BGE none */
    ma_w(&a, 0x342F); ma_w(&a, 10);                      /* MOVE.W 10(A7),D2        hitPt.h */
    ma_w(&a, 0xB468); ma_w(&a, 2);                       /* CMP.W 2(A0),D2          left */
    ma_br(&a, 0x6D, L_NONE);
    ma_w(&a, 0xB468); ma_w(&a, 6);                       /* CMP.W 6(A0),D2          right */
    ma_br(&a, 0x6C, L_NONE);
    ma_w(&a, 0xE848);                                    /* LSR.W #4,D0 */
    ma_w(&a, 0x3200);                                    /* MOVE.W D0,D1 */
    ma_w(&a, 0x5241);                                    /* ADDQ.W #1,D1 */
    ma_here(&a, L_NONE);
    ma_w(&a, 0x3281);                                    /* MOVE.W D1,(A1) */
    ma_w(&a, 0x205F); ma_w(&a, 0x4FEF); ma_w(&a, 18); ma_w(&a, 0x4ED0);   /* the return sequence again, to stay within a short branch */
    for (unsigned i = 0; i < a.nfx; i++) {
        int d = ((int)a.label[a.fx_lab[i]] - (int)(a.fx_at[i] + 1)) * 2;
        if (d == 0 || d > 127 || d < -128) return 0;
        a.w[a.fx_at[i]] |= (uint16_t)(d & 0xff);
    }
    for (unsigned i = 0; i < a.n; i++) { out[2 * i] = (uint8_t)(a.w[i] >> 8); out[2 * i + 1] = (uint8_t)a.w[i]; }
    return 2u * a.n;
}
/* A MENU resource: id, the MDEF it names (0 for the standard one), title, then items "text" with an optional command key. */
static size_t mdef_menu_res(uint8_t *o, int id, int mdef, const char *title, const char *const *items, const char *keys) {
    memset(o, 0, 256);
    fb_put16(o, (unsigned)id); fb_put16(o + 6, (unsigned)mdef); memset(o + 10, 0xff, 4);
    size_t p = 14; o[p++] = (uint8_t)strlen(title); memcpy(o + p, title, strlen(title)); p += strlen(title);
    for (unsigned i = 0; items[i]; i++) {
        o[p++] = (uint8_t)strlen(items[i]); memcpy(o + p, items[i], strlen(items[i])); p += strlen(items[i]);
        o[p++] = 0; o[p++] = keys && keys[i] != ' ' ? (uint8_t)keys[i] : 0; o[p++] = 0; o[p++] = 0;
    }
    o[p++] = 0;
    return p;
}
#endif
