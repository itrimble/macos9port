#include "mac_runtime.h"

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

static void test_toolbox_manager_initializers(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return;
    rt.heap_next = 0x100000;
    rt.heap_limit = 0x700000;
    mac_cpu_set(rt.cpu, MAC_A5, 0x800000);
    mac_cpu_set(rt.cpu, MAC_A7, 0xF00000);

    /* InitGraf (0xA86E) sets up A5 QuickDraw globals before InitWindows. */
    mac_cpu_write(rt.cpu, 0xF00000 - 4, 4, 0x7FFFFC);
    mac_cpu_set(rt.cpu, MAC_A7, 0xF00000 - 4);
    CHECK(mac_quickdraw_service(&rt, 0xA86E) == 1);

    /* InitFonts (0xA8FE), InitWindows (0xA912), InitMenus (0xA930). */
    CHECK(mac_toolbox_service(&rt, 0xA8FE) == 1 && (rt.manager_flags & 1) != 0);
    CHECK(mac_toolbox_service(&rt, 0xA912) == 1 && (rt.manager_flags & 2) != 0);
    CHECK(rt.wm_port != 0 && mac_cpu_read(rt.cpu, 0x09DE, 4) == rt.wm_port);
    CHECK(mac_cpu_read(rt.cpu, 0x09DA, 2) == 1 && mac_cpu_read(rt.cpu, 0x09DC, 2) == 1);

    CHECK(mac_toolbox_service(&rt, 0xA930) == 1 && (rt.manager_flags & 4) != 0);
    CHECK(rt.menu_bar != 0 && mac_cpu_read(rt.cpu, 0x0A1C, 4) == rt.menu_bar);
    CHECK(mac_cpu_read(rt.cpu, 0x0BAA, 2) == 20);

    /* InitDialogs (0xA97B) pops ResumeProc; TEInit (0xA9CC); GetWMgrPort (0xA910); SystemTask (0xA9B4). */
    uint32_t sp = mac_cpu_get(rt.cpu, MAC_A7);
    mac_cpu_write(rt.cpu, sp - 4, 4, 0x12345678u);
    mac_cpu_set(rt.cpu, MAC_A7, sp - 4);
    CHECK(mac_toolbox_service(&rt, 0xA97B) == 1 && (rt.manager_flags & 8) != 0);
    CHECK(rt.dialog_resume == 0x12345678u && mac_cpu_get(rt.cpu, MAC_A7) == sp);

    CHECK(mac_toolbox_service(&rt, 0xA9CC) == 1 && (rt.manager_flags & 16) != 0);
    CHECK(mac_toolbox_service(&rt, 0xA9B4) == 1);

    mac_cpu_write(rt.cpu, sp - 4, 4, 0x1800);
    mac_cpu_set(rt.cpu, MAC_A7, sp - 4);
    CHECK(mac_toolbox_service(&rt, 0xA910) == 1);
    CHECK(mac_cpu_read(rt.cpu, 0x1800, 4) == rt.wm_port);

    mac_cpu_destroy(rt.cpu);
}

typedef struct {
    int16_t top, left, bottom, right;
    uint8_t type;
    char text[256];
} DitlItem;

static int parse_ditl_and_expand(const uint8_t *ditl, size_t len,
                                 const uint8_t params[4][256],
                                 DitlItem *items, int max_items) {
    if (!ditl || len < 2) return -1;
    int count = (int)(((uint16_t)ditl[0] << 8) | ditl[1]) + 1;
    if (count <= 0 || count > max_items) return -1;
    size_t pos = 2;
    for (int i = 0; i < count; i++) {
        if (pos + 14 > len) return -1;
        pos += 4; /* placeholder handle */
        items[i].top = (int16_t)(((uint16_t)ditl[pos] << 8) | ditl[pos + 1]);
        items[i].left = (int16_t)(((uint16_t)ditl[pos + 2] << 8) | ditl[pos + 3]);
        items[i].bottom = (int16_t)(((uint16_t)ditl[pos + 4] << 8) | ditl[pos + 5]);
        items[i].right = (int16_t)(((uint16_t)ditl[pos + 6] << 8) | ditl[pos + 7]);
        items[i].type = ditl[pos + 8];
        uint8_t dlen = ditl[pos + 9];
        pos += 10;
        if (pos + dlen > len) return -1;

        size_t out = 0;
        for (uint8_t k = 0; k < dlen && out + 1 < sizeof(items[i].text); k++) {
            if (ditl[pos + k] == '^' && k + 1 < dlen &&
                ditl[pos + k + 1] >= '0' && ditl[pos + k + 1] <= '3') {
                int idx = ditl[pos + k + 1] - '0';
                uint8_t plen = (uint8_t)params[idx][0];
                for (uint8_t p = 0; p < plen && out + 1 < sizeof(items[i].text); p++) {
                    items[i].text[out++] = (char)params[idx][1 + p];
                }
                k++;
            } else {
                items[i].text[out++] = (char)ditl[pos + k];
            }
        }
        items[i].text[out] = '\0';
        pos += dlen + (dlen & 1u);
    }
    return count;
}

static int ditl_hit_test(const DitlItem *items, int count, int x, int y) {
    for (int i = 0; i < count; i++) {
        if (items[i].type & 0x80u) continue; /* itemDisable */
        if (x >= items[i].left && x < items[i].right &&
            y >= items[i].top && y < items[i].bottom) {
            return i + 1; /* 1-based dialog item number */
        }
    }
    return 0;
}

static void test_ditl_paramtext_and_hit_testing(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(32768);
    CHECK(rt.cpu != NULL);
    mac_cpu_set(rt.cpu, MAC_A7, 0x4000);

    const char *pstr[4] = { "Glider PRO", "System 9", "256 Colors", "" };
    for (int i = 0; i < 4; i++) {
        uint32_t addr = 0x1000u + (uint32_t)i * 64u;
        size_t n = strlen(pstr[i]);
        mac_cpu_write(rt.cpu, addr, 1, (uint32_t)n);
        memcpy(mac_cpu_ram(rt.cpu) + addr + 1, pstr[i], n);
        mac_cpu_write(rt.cpu, 0x4000u + (uint32_t)(3 - i) * 4u, 4, addr);
    }
    CHECK(mac_dialog_service(&rt, 0xA98B) == 1);

    /* Synthetic 2-item DITL: Item 1 = OK button (4), Item 2 = disabled statText (0x88) with ^0 / ^1. */
    uint8_t ditl[64];
    memset(ditl, 0, sizeof(ditl));
    ditl[0] = 0; ditl[1] = 1; /* 2 items minus 1 */
    /* Item 1: rect (100, 200, 120, 260), type=4 (btnCtrl), len=2 ("OK") */
    size_t p = 2 + 4;
    ditl[p + 0] = 0; ditl[p + 1] = 100;
    ditl[p + 2] = 0; ditl[p + 3] = 200;
    ditl[p + 4] = 0; ditl[p + 5] = 120;
    ditl[p + 6] = 1; ditl[p + 7] = 4; /* 260 */
    ditl[p + 8] = 4; ditl[p + 9] = 2;
    memcpy(ditl + p + 10, "OK", 2);
    p += 12;
    /* Item 2: rect (20, 20, 60, 260), type=0x88 (itemDisable|statText), "Run ^0 on ^1" (12B) */
    p += 4;
    ditl[p + 0] = 0; ditl[p + 1] = 20;
    ditl[p + 2] = 0; ditl[p + 3] = 20;
    ditl[p + 4] = 0; ditl[p + 5] = 60;
    ditl[p + 6] = 1; ditl[p + 7] = 4;
    ditl[p + 8] = 0x88; ditl[p + 9] = 12;
    memcpy(ditl + p + 10, "Run ^0 on ^1", 12);
    p += 22;

    DitlItem items[4];
    int n = parse_ditl_and_expand(ditl, p, rt.dialog_params, items, 4);
    CHECK(n == 2);
    CHECK(strcmp(items[0].text, "OK") == 0);
    CHECK(strcmp(items[1].text, "Run Glider PRO on System 9") == 0);
    CHECK(ditl_hit_test(items, n, 220, 110) == 1);
    CHECK(ditl_hit_test(items, n, 40, 40) == 0); /* Disabled static text is not hit. */

    mac_cpu_destroy(rt.cpu);
}

typedef struct {
    int16_t top, left, bottom, right;
    uint8_t visible;
    uint8_t hilite;
    int16_t value, min_val, max_val;
} SimControl;

typedef struct {
    char buf[256];
    int16_t len;
    int16_t sel_start, sel_end;
} SimTERec;

static void control_set_value(SimControl *c, int16_t v) {
    if (v < c->min_val) v = c->min_val;
    if (v > c->max_val) v = c->max_val;
    c->value = v;
}

static int control_test_hit(const SimControl *c, int x, int y) {
    if (!c->visible || c->hilite == 255) return 0;
    return (x >= c->left && x < c->right && y >= c->top && y < c->bottom) ? 10 : 0;
}

static void te_set_select(SimTERec *te, int16_t start, int16_t end) {
    if (start < 0) start = 0;
    if (end > te->len) end = te->len;
    if (start > end) start = end;
    te->sel_start = start;
    te->sel_end = end;
}

static void te_delete(SimTERec *te) {
    if (te->sel_start < te->sel_end) {
        int16_t del = te->sel_end - te->sel_start;
        memmove(te->buf + te->sel_start, te->buf + te->sel_end, (size_t)(te->len - te->sel_end + 1));
        te->len -= del;
        te->sel_end = te->sel_start;
    } else if (te->sel_start > 0) {
        memmove(te->buf + te->sel_start - 1, te->buf + te->sel_start, (size_t)(te->len - te->sel_start + 1));
        te->sel_start--;
        te->sel_end = te->sel_start;
        te->len--;
    }
}

static void te_key(SimTERec *te, char ch) {
    if (ch == '\b') { te_delete(te); return; }
    if (te->sel_start < te->sel_end) te_delete(te);
    if (te->len + 1 >= (int16_t)sizeof(te->buf)) return;
    memmove(te->buf + te->sel_start + 1, te->buf + te->sel_start, (size_t)(te->len - te->sel_start + 1));
    te->buf[te->sel_start] = ch;
    te->sel_start++;
    te->sel_end = te->sel_start;
    te->len++;
}

static void test_control_and_textedit_state(void) {
    SimControl btn = { .top = 10, .left = 20, .bottom = 30, .right = 100,
                       .visible = 1, .hilite = 0, .value = 0, .min_val = 0, .max_val = 100 };
    control_set_value(&btn, 150);
    CHECK(btn.value == 100);
    control_set_value(&btn, -20);
    CHECK(btn.value == 0);
    CHECK(control_test_hit(&btn, 50, 20) == 10);
    btn.hilite = 255;
    CHECK(control_test_hit(&btn, 50, 20) == 0);

    SimTERec te = {0};
    const char *init = "Player One";
    for (size_t i = 0; init[i]; i++) te_key(&te, init[i]);
    CHECK(te.len == 10 && strcmp(te.buf, "Player One") == 0);

    /* Select "One" (7..10) and replace with "Two". */
    te_set_select(&te, 7, 99);
    CHECK(te.sel_start == 7 && te.sel_end == 10);
    te_key(&te, 'T');
    te_key(&te, 'w');
    te_key(&te, 'o');
    CHECK(te.len == 10 && strcmp(te.buf, "Player Two") == 0);
    te_key(&te, '\b');
    CHECK(te.len == 9 && strcmp(te.buf, "Player Tw") == 0);
}

int main(void) {
    test_toolbox_manager_initializers();
    test_ditl_paramtext_and_hit_testing();
    test_control_and_textedit_state();

    if (failures > 0) {
        fprintf(stderr, "test_mac_controls_textedit: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_controls_textedit: all %d checks passed\n", checks);
    return 0;
}
