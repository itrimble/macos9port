#include "mac_mdef.h"
#include "mac_menu_int.h"
#include <stdlib.h>
#include <string.h>

/* Custom menu definition procedures: see mac_mdef.h. Message numbers and the parameter order are the public classic Menu Manager interface. */
#define MDEF_TYPE UINT32_C(0x4d444546)
#define MAX_SIZE_MENUS 32
struct MacMdef {
    uint32_t scratch;                          /* guest memory: menuRect (8 bytes), whichItem (2) */
    uint32_t cport;                            /* a colour port covering the screen, made once colour QuickDraw is in use (the menus' port then) */
    uint32_t saved_port; int port_changed;
    MacGuestDone user; void *uctx;
    uint32_t menus[MAX_SIZE_MENUS]; unsigned count, next, pop; uint32_t sp, result_at, result;   /* a size job */
    uint32_t cur_menu;
};
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static void wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }
void mac_mdef_destroy(MacRuntime *rt) { free(rt->mdef); rt->mdef = NULL; }

uint32_t mac_mdef_load(MacRuntime *rt, unsigned id) {
    uint32_t h = mac_runtime_resource(rt, MDEF_TYPE, (int16_t)id);
    if (!h) return 0;
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *m = &rt->handles[i];
        if (m->handle != h) continue;
        if (!m->resource || m->type != MDEF_TYPE || m->size < 4 || m->size > 0x100000) return 0;
        m->loaded_code = 1;
        return h;
    }
    return 0;
}
uint32_t mac_mdef_code(MacRuntime *rt, uint32_t handle) {
    if (!handle || (handle & 1)) return 0;
    for (size_t i = 0; i < rt->handle_count; i++) {
        const MacHandle *m = &rt->handles[i];
        if (m->handle != handle) continue;
        if (!m->resource || m->type != MDEF_TYPE || !m->loaded_code || m->size < 4) return 0;
        uint32_t code = rd(rt, handle, 4);
        return code == m->pointer && !(code & 1) ? code : 0;
    }
    return 0;
}
uint32_t mac_mdef_of_menu(MacRuntime *rt, const MacHandle *menu) {
    uint32_t h = rd(rt, menu->pointer + 6, 4);
    return h ? mac_mdef_code(rt, h) ? h : 0 : 0;
}

static MacMdef *state(MacRuntime *rt) {
    if (!rt->mdef) rt->mdef = calloc(1, sizeof *rt->mdef);
    if (!rt->mdef) { fail(rt, "Menu definition state could not be allocated."); return NULL; }
    if (!rt->mdef->scratch) {
        rt->mdef->scratch = mac_runtime_alloc(rt, 16);
        if (!rt->mdef->scratch) { fail(rt, "No memory for the menu definition's parameter block."); return NULL; }
    }
    return rt->mdef;
}
/* The port a definition draws in. With colour QuickDraw in use the Window Manager's port is a colour port, and definitions written for it call RGBForeColor
 * and the like, so one covering the screen is opened the first time it is wanted; before that, or without colour, it is the plain Window Manager port. */
static uint32_t menu_port(MacRuntime *rt, MacMdef *st) {
    if (st->cport) return st->cport;
    if (!rt->screen_pixmap || !rt->qd_globals) return rt->wm_port;
    uint32_t port = mac_runtime_alloc(rt, 108), sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!port || sp < 8) return 0;
    wr(rt, sp - 4, 4, port); mac_cpu_set(rt->cpu, MAC_A7, sp - 4);
    int rc = mac_color_port_service(rt, 0xaa00);
    mac_cpu_set(rt->cpu, MAC_A7, sp);
    if (rc != 1) return 0;
    st->cport = port;
    return port;
}
static int mdef_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; MacMdef *st = rt->mdef; (void)ctx; (void)result;
    if (!st) return -1;
    if (st->port_changed) {
        rt->current_port = st->saved_port;
        if (rt->qd_globals) wr(rt, rt->qd_globals, 4, st->saved_port);
        st->port_changed = 0;
    }
    return st->user(rt, st->uctx, 0);
}
int mac_mdef_send(MacRuntime *rt, uint32_t menu, int message, const int16_t rect[4], int hit_v, int hit_h, int item, MacGuestDone done, void *ctx) {
    MacHandle *m = mac_menu_record(rt, menu, NULL);
    if (!m) return fail(rt, "A menu definition was asked to act on a menu that is not a valid menu record.");
    uint32_t mh = mac_mdef_of_menu(rt, m);
    if (!mh) return fail(rt, "A menu's definition procedure handle is not a live MDEF resource.");
    MacMdef *st = state(rt); if (!st) return -1;
    if (rt->guest.active) return fail(rt, "A menu definition message was sent while another guest call is running (unsupported).");
    for (int i = 0; i < 4; i++) wr(rt, st->scratch + 2u * (unsigned)i, 2, (uint16_t)rect[i]);
    wr(rt, st->scratch + 8, 2, (uint16_t)item);
    st->user = done; st->uctx = ctx; st->cur_menu = menu;
    uint32_t port = menu_port(rt, st);
    if (mac_cpu_halted(rt->cpu)) return -1;
    if (port && !st->port_changed) {
        st->saved_port = rt->current_port; st->port_changed = 1;
        rt->current_port = port;
        if (rt->qd_globals) wr(rt, rt->qd_globals, 4, port);
    }
    uint8_t a[18];                              /* lowest address first: whichItem, hitPt, menuRect, theMenu, message */
    uint32_t p_item = st->scratch + 8, p_rect = st->scratch;
    a[0] = (uint8_t)(p_item >> 24); a[1] = (uint8_t)(p_item >> 16); a[2] = (uint8_t)(p_item >> 8); a[3] = (uint8_t)p_item;
    a[4] = (uint8_t)((unsigned)hit_v >> 8); a[5] = (uint8_t)hit_v; a[6] = (uint8_t)((unsigned)hit_h >> 8); a[7] = (uint8_t)hit_h;
    a[8] = (uint8_t)(p_rect >> 24); a[9] = (uint8_t)(p_rect >> 16); a[10] = (uint8_t)(p_rect >> 8); a[11] = (uint8_t)p_rect;
    a[12] = (uint8_t)(menu >> 24); a[13] = (uint8_t)(menu >> 16); a[14] = (uint8_t)(menu >> 8); a[15] = (uint8_t)menu;
    a[16] = (uint8_t)((unsigned)message >> 8); a[17] = (uint8_t)message;
    int rc = mac_guest_call(rt, mac_mdef_code(rt, mh), a, 18, 0, mdef_done, NULL);
    if (rc < 0 && st->port_changed) { rt->current_port = st->saved_port; st->port_changed = 0; }
    return rc;
}
void mac_mdef_out(MacRuntime *rt, int16_t rect[4], int *item) {
    MacMdef *st = rt->mdef;
    for (int i = 0; i < 4; i++) rect[i] = st ? (int16_t)rd(rt, st->scratch + 2u * (unsigned)i, 2) : 0;
    *item = st ? (int16_t)rd(rt, st->scratch + 8, 2) : 0;
}

static int size_done(void *vrt, void *ctx, uint32_t result);
static int size_next(MacRuntime *rt) {
    MacMdef *st = rt->mdef;
    if (st->next < st->count) {
        static const int16_t none[4] = {0, 0, 0, 0};
        return mac_mdef_send(rt, st->menus[st->next], MDEF_SIZE, none, 0, 0, 0, size_done, NULL);
    }
    if (st->result_at) wr(rt, st->result_at, 4, st->result);
    mac_cpu_set(rt->cpu, MAC_A7, st->sp + st->pop);
    return 0;
}
static int size_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; MacMdef *st = rt->mdef; (void)ctx; (void)result;
    MacHandle *m = mac_menu_record(rt, st->menus[st->next], NULL);
    if (!m) return fail(rt, "A menu definition left the menu record invalid after the size message.");
    int w = (int16_t)rd(rt, m->pointer + 2, 2), h = (int16_t)rd(rt, m->pointer + 4, 2);
    if (w < 0 || h < 0) return fail(rt, "A menu definition set a negative menu width or height in answer to the size message.");
    st->next++;
    return size_next(rt);
}
int mac_mdef_size_menus(MacRuntime *rt, const uint32_t *menus, unsigned count, uint32_t sp, unsigned pop, uint32_t result_at, uint32_t result) {
    MacMdef *st = state(rt); if (!st) return -1;
    if (count > MAX_SIZE_MENUS) return fail(rt, "Too many custom menus to size in one call.");
    memcpy(st->menus, menus, count * sizeof *menus);
    st->count = count; st->next = 0; st->sp = sp; st->pop = pop; st->result_at = result_at; st->result = result;
    int rc = size_next(rt);
    return rc < 0 ? -1 : 1;
}
