#include "mac_stdfile_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pack3 ($A9EA): Standard File SFGetFile (selector 2; SFPutFile, selector 1, is in mac_stdput.c) and SFPGetFile (selector 4), as a native modal dialog.
 *
 * Stack, Pascal order (selector word at SP, then the last parameter):
 *   sel 2: reply4 dlgHook4 typeList4 numTypes2 fileFilter4 prompt4 where4                    (28 bytes with the selector)
 *   sel 4: filterProc4 dialogID2 reply4 dlgHook4 typeList4 numTypes2 fileFilter4 prompt4 where4 (34 bytes)
 * The guest is stopped inside the trap while the dialog is open: the trap rewinds its PC and ends the CPU slice (as
 * _Delay does), so each re-execution handles the queued mouse and key events, redraws if something changed, and either
 * finishes or waits again. With no input the dialog simply stays open: a headless run proves only that it was reached.
 * A file-filter routine (TRUE hides the file) is called once per candidate through mac_guest_call before the dialog opens.
 * Not supported, and halting with a message: dialog hooks, dialog filter procedures, the SFPPutFile and Standard File selectors,
 * a list that does not fit 1024 files, Eject and Drive (the single volume cannot do either, so they are drawn dimmed).
 * Buttons act on mouse-up inside the button; scroll arrows and page regions act once per click (no auto-repeat, no thumb
 * drag); the icon item is not drawn. */
enum { SELECTOR_GET = 2, SELECTOR_PGET = 4, SELECTOR_SGET = 6, DLOG_TYPE = 0x444c4f47, DITL_TYPE = 0x4449544c };

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }

void mac_stdfile_destroy(MacRuntime *rt) {
    MacSfState *s = rt->stdfile;
    if (!s) return;
    mac_sflist_free(&s->list); free(s->all); free(s->hide); free(s->backdrop); free(s);
    rt->stdfile = NULL;
}

/* A layout of our own with the standard item numbers, used when no dialog template is named (SFGetFile, or dialogID -4000). */
static const uint8_t TEXT_OPEN[] = "Open", TEXT_CANCEL[] = "Cancel", TEXT_EJECT[] = "Eject", TEXT_DRIVE[] = "Drive";
static void builtin_layout(MacSfState *s) {
    static const struct { int16_t t, l, b, r; uint8_t kind; const uint8_t *text; uint8_t n; } rows[] = {
        { 138, 256, 156, 336, MAC_ITEM_BUTTON, TEXT_OPEN, 4 },   { 1152, 59, 1232, 77, MAC_ITEM_BUTTON, TEXT_OPEN, 0 },
        { 163, 256, 181, 336, MAC_ITEM_BUTTON, TEXT_CANCEL, 6 }, { 39, 232, 59, 347, MAC_ITEM_USER, NULL, 0 },
        { 68, 256, 86, 336, MAC_ITEM_BUTTON, TEXT_EJECT, 5 },    { 93, 256, 111, 336, MAC_ITEM_BUTTON, TEXT_DRIVE, 5 },
        { 39, 12, 185, 230, MAC_ITEM_USER, NULL, 0 },            { 39, 229, 185, 246, MAC_ITEM_USER, NULL, 0 },
        { 124, 252, 125, 340, MAC_ITEM_USER, NULL, 0 },          { 1044, 20, 1145, 116, MAC_ITEM_STATIC, NULL, 0 },
        { 189, 12, 206, 247, MAC_ITEM_STATIC, NULL, 0 },
    };
    s->ditl.count = sizeof rows / sizeof *rows;
    for (unsigned i = 0; i < s->ditl.count; i++) {
        MacDitlItem *it = &s->ditl.items[i];
        it->top = rows[i].t; it->left = rows[i].l; it->bottom = rows[i].b; it->right = rows[i].r;
        it->kind = rows[i].kind; it->data = rows[i].text; it->length = rows[i].n;
    }
    s->width = 348; s->height = 208;
}

static int load_layout(MacRuntime *rt, MacSfState *s, int16_t dialog_id, int custom) {
    if (!custom) { builtin_layout(s); return 0; }
    MacResource dlog, ditl; MacDlog g; const char *why = NULL;
    if (mac_resource_find(&rt->fork, DLOG_TYPE, dialog_id, &dlog)) return fail(rt, "Standard File dialog template (DLOG) not found.");
    if (mac_dlog_parse(dlog.data, dlog.size, &g, &why)) return fail(rt, why);
    if (mac_resource_find(&rt->fork, DITL_TYPE, g.items_id, &ditl)) return fail(rt, "Standard File dialog item list (DITL) not found.");
    if (mac_ditl_parse(ditl.data, ditl.size, &s->ditl, &why)) return fail(rt, why);
    s->width = g.right - g.left; s->height = g.bottom - g.top;
    if (s->ditl.count < SF_PROMPT - 1 || s->ditl.items[SF_OPEN - 1].kind != MAC_ITEM_BUTTON ||
        s->ditl.items[SF_CANCEL - 1].kind != MAC_ITEM_BUTTON || s->ditl.items[SF_LIST - 1].kind != MAC_ITEM_USER ||
        s->ditl.items[SF_SCROLL - 1].kind != MAC_ITEM_USER)
        return fail(rt, "Custom Standard File dialog lacks the required Open, Cancel, list and scroll items.");
    return 0;
}

static int visible(const MacSfState *s, const MacFileEntry *e) {
    if (e->flags & 0x4000) return 0;                 /* invisible Finder flag */
    if (strlen(e->name) > 63) return 0;              /* cannot fit SFReply's Str63 */
    if (s->num_types < 0) return 1;
    for (int i = 0; i < s->num_types; i++) if (e->type == s->types[i]) return 1;
    return 0;                                        /* includes files whose type was never set */
}

static int finish_list(MacRuntime *rt, MacSfState *s) {
    MacFileEntry *shown = malloc((size_t)(s->all_count ? s->all_count : 1) * sizeof *shown);
    if (!shown) return fail(rt, "Standard File could not allocate its list.");
    unsigned n = 0;
    for (unsigned i = 0; i < s->all_count; i++) if (!s->hide[i]) shown[n++] = s->all[i];
    int rc = mac_sflist_set(&s->list, shown, n);
    free(shown);
    if (rc) return fail(rt, "Standard File could not allocate its list.");
    s->stage = SF_STAGE_RUN; s->dirty = 1;
    return 0;
}

static int filter_done(void *rtp, void *ctx, uint32_t result);
/* Offer the next candidate to the guest filter, or finish the list and re-execute the trap to open the dialog. */
static int advance(MacRuntime *rt, MacSfState *s) {
    if (s->next >= s->all_count) {
        if (finish_list(rt, s)) return -1;
        mac_cpu_set(rt->cpu, MAC_PC, s->trap_pc);
        return 0;
    }
    const MacFileEntry *e = &s->all[s->next];
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    uint32_t name = s->pb + 112;
    memset(ram + s->pb, 0, 176);
    unsigned n = (unsigned)strlen(e->name);
    ram[name] = (uint8_t)n; memcpy(ram + name + 1, e->name, n);
    wr(rt, s->pb + 18, 4, name);                     /* ioNamePtr */
    wr(rt, s->pb + 22, 2, 0xffff);                   /* ioVRefNum */
    wr(rt, s->pb + 32, 4, e->type); wr(rt, s->pb + 36, 4, e->creator); wr(rt, s->pb + 40, 2, e->flags); /* ioFlFndrInfo */
    uint8_t arg[4] = { (uint8_t)(s->pb >> 24), (uint8_t)(s->pb >> 16), (uint8_t)(s->pb >> 8), (uint8_t)s->pb };
    return mac_guest_call(rt, s->filter, arg, 4, 2, filter_done, s);
}
static int filter_done(void *rtp, void *ctx, uint32_t result) {
    MacSfState *s = ctx;
    s->hide[s->next++] = (result & 0xff00) != 0;     /* a Boolean result sits in the high byte of its word */
    return advance(rtp, s);
}

static int begin(MacRuntime *rt, uint32_t sp, uint32_t pc, unsigned selector) {
    uint32_t a = sp + 2, reply, hook, list, filter, prompt, where, filter_proc = 0, pop;
    int16_t dialog_id = -4000, num;
    if (selector == SELECTOR_SGET) {      /* StandardGetFile(fileFilter, numTypes, typeList, VAR reply): no prompt, hook or position */
        if (!valid(rt, sp, 16)) return fail(rt, "Standard File stack is invalid.");
        reply = rd(rt, a, 4); list = rd(rt, a + 4, 4); num = (int16_t)rd(rt, a + 8, 2); filter = rd(rt, a + 10, 4);
        hook = 0; prompt = 0; where = (uint32_t)((((480 - 208) / 2) << 16) | ((640 - 348) / 2)); pop = 16;
        if (filter) return fail(rt, "StandardGetFile file filter procedures are unsupported.");
    } else {
    if (selector == SELECTOR_PGET) { filter_proc = rd(rt, a, 4); dialog_id = (int16_t)rd(rt, a + 4, 2); a += 6; pop = 34; } else pop = 28;
    reply = rd(rt, a, 4); hook = rd(rt, a + 4, 4); list = rd(rt, a + 8, 4); num = (int16_t)rd(rt, a + 12, 2);
    filter = rd(rt, a + 14, 4); prompt = rd(rt, a + 18, 4); where = rd(rt, a + 22, 4);
    }
    if (!valid(rt, sp, pop)) return fail(rt, "Standard File stack is invalid.");
    if (hook) return fail(rt, "Standard File dialog hooks are unsupported.");
    if (filter_proc) return fail(rt, "Standard File dialog filter procedures are unsupported.");
    if (num < -1 || num > 4) return fail(rt, "Standard File numTypes must be -1 or 0 to 4.");
    if (!valid(rt, reply, selector == SELECTOR_SGET ? 88 : 74)) return fail(rt, "Standard File reply record is invalid.");
    if (prompt && (!valid(rt, prompt, 1) || !valid(rt, prompt, (size_t)rd(rt, prompt, 1) + 1))) return fail(rt, "Standard File prompt is invalid.");
    if (num > 0 && !valid(rt, list, (size_t)num * 4)) return fail(rt, "Standard File type list is invalid.");
    if (filter && (filter & 1)) return fail(rt, "Standard File file filter address is invalid.");
    if (rt->screen_depth != 8 || rt->screen_width != 640 || rt->screen_height != 480 || rt->screen_row_bytes != 640 ||
        !valid(rt, rt->screen_base, 640 * 480))
        return fail(rt, "Standard File requires the initialized 640x480 indexed display.");
    MacSfState *s = calloc(1, sizeof *s);
    if (!s) return fail(rt, "Standard File could not allocate its state.");
    rt->stdfile = s;
    s->selector = selector; s->trap_pc = pc; s->sp = sp; s->pop_bytes = pop; s->reply = reply; s->filter = filter;
    s->num_types = num;
    for (int i = 0; i < num; i++) s->types[i] = rd(rt, list + (uint32_t)i * 4, 4);
    mac_sflist_init(&s->list, 0);
    if (load_layout(rt, s, dialog_id, selector == SELECTOR_PGET && dialog_id != -4000)) { mac_stdfile_destroy(rt); return -1; }
    if (s->width > 640 || s->height > 480 || s->width < 64 || s->height < 64) { mac_stdfile_destroy(rt); return fail(rt, "Standard File dialog size is unsupported."); }
    int top = (int16_t)(where >> 16), left = (int16_t)where;
    if (top + s->height > 480) top = 480 - s->height;
    if (left + s->width > 640) left = 640 - s->width;
    s->top = top < 0 ? 0 : top; s->left = left < 0 ? 0 : left;
    const MacDitlItem *l = &s->ditl.items[SF_LIST - 1];
    unsigned rows = l->bottom > l->top ? (unsigned)(l->bottom - l->top - 1) / SF_ROW : 0;
    if (!rows) { mac_stdfile_destroy(rt); return fail(rt, "Standard File list item has no room for rows."); }
    s->list.rows = rows;
    unsigned n = prompt ? (unsigned)rd(rt, prompt, 1) : 0; if (n > sizeof s->prompt - 1) n = sizeof s->prompt - 1;
    if (prompt) memcpy(s->prompt, mac_cpu_ram(rt->cpu) + prompt + 1, n);
    memcpy(s->volume, "OS999 HD", 9);
    MacFileEntry *entries = malloc(SF_MAX_FILES * sizeof *entries);
    s->all = malloc(SF_MAX_FILES * sizeof *s->all);
    s->hide = calloc(SF_MAX_FILES, 1);
    s->backdrop = malloc((size_t)s->width * (size_t)s->height);
    unsigned count = 0;
    int error = entries && s->all && s->hide && s->backdrop ? mac_files_entries(rt, entries, SF_MAX_FILES, &count) : -108;
    if (error) {
        free(entries); mac_stdfile_destroy(rt);
        char msg[96]; snprintf(msg, sizeof msg, "Standard File could not list the volume (error %d).", error);
        return fail(rt, msg);
    }
    for (unsigned i = 0; i < count; i++) if (visible(s, &entries[i])) s->all[s->all_count++] = entries[i];
    free(entries);
    if (filter) {
        if (!rt->stdfile_pb) rt->stdfile_pb = mac_runtime_alloc(rt, 176);
        if (!rt->stdfile_pb) { mac_stdfile_destroy(rt); return fail(rt, "Standard File could not allocate a filter parameter block."); }
        s->pb = rt->stdfile_pb;
    }
    if (!filter) { /* nothing to ask the guest: the whole candidate list is shown */
        if (finish_list(rt, s)) return -1;
        return 1;
    }
    s->stage = SF_STAGE_FILTER;
    int rc = advance(rt, s);
    return rc < 0 ? -1 : 1;
}

enum { PENDING = 0, OPEN = 1, CANCEL = 2 };
static uint32_t double_time(MacRuntime *rt) { uint32_t t = rd(rt, 0x2f0, 4); return t && t < 600 ? t : 32; }

static int handle(MacRuntime *rt, MacSfState *s, const MacEvent *e) {
    MacSfList *l = &s->list;
    if (e->what == 3 || e->what == 5) {
        unsigned ch = e->message & 0xff;
        if (ch == 13 || ch == 3) return l->selected >= 0 ? OPEN : PENDING;
        if (ch == 27 || ((e->modifiers & 0x100) && ch == '.')) return CANCEL;
        if (ch == 0x1e) mac_sflist_move(l, -1);
        else if (ch == 0x1f) mac_sflist_move(l, 1);
        else if (ch >= 32 && ch < 127 && !(e->modifiers & 0x100)) mac_sflist_type(l, (char)ch, rt->ticks);
        s->dirty = 1;
        return PENDING;
    }
    unsigned row = 0, item = mac_stdfile_hit(s, e->x, e->y, &row);
    if (e->what == 1) {
        if (item == SF_LIST) {
            if (mac_sflist_click(l, row, rt->ticks, double_time(rt))) return OPEN;
            s->dirty = 1;
        } else if (item == SF_SCROLL) {
            int zone = mac_stdfile_scroll_zone(s, e->x, e->y);
            if (zone == 1) mac_sflist_scroll_to(l, l->top ? l->top - 1 : 0);
            else if (zone == 2) mac_sflist_scroll_to(l, l->top + 1);
            else if (zone == 3) mac_sflist_page(l, -1);
            else if (zone == 4) mac_sflist_page(l, 1);
            s->dirty = 1;
        } else if (item && mac_stdfile_item_enabled(s, item)) { s->pressed = (int)item; s->dirty = 1; }
        return PENDING;
    }
    int pressed = s->pressed; s->pressed = 0;       /* mouse-up */
    if (pressed) s->dirty = 1;
    if (pressed && item == (unsigned)pressed && mac_stdfile_item_enabled(s, item)) return item == SF_OPEN ? OPEN : CANCEL;
    return PENDING;
}

static void write_reply(MacRuntime *rt, const MacSfState *s, int outcome) {
    uint8_t *ram = mac_cpu_ram(rt->cpu) + s->reply;
    int spec = s->selector == SELECTOR_SGET;
    memset(ram, 0, spec ? 88 : 74);
    if (outcome != OPEN) return;
    const MacFileEntry *e = &s->list.entries[s->list.selected];
    unsigned n = (unsigned)strlen(e->name);
    if (spec) {                                      /* StandardFileReply: good, FSSpec {vRefNum -1, parID 2, name}, sfType, no flags */
        ram[0] = 1; wr(rt, s->reply + 2, 4, e->type); wr(rt, s->reply + 6, 2, 0xffff); wr(rt, s->reply + 8, 4, 2);
        ram[12] = (uint8_t)n; memcpy(ram + 13, e->name, n);
        return;
    }
    ram[0] = 1;                                      /* good */
    wr(rt, s->reply + 2, 4, e->type);                /* fType */
    wr(rt, s->reply + 6, 2, 0xffff);                 /* vRefNum: the one volume */
    ram[10] = (uint8_t)n; memcpy(ram + 11, e->name, n);
}

/* One re-execution of the trap with the dialog open. */
static int step(MacRuntime *rt, MacSfState *s) {
    if (!s->drawn) { mac_stdfile_save(rt, s); s->drawn = 1; s->dirty = 1; }
    int outcome = PENDING; MacEvent e;
    while (outcome == PENDING && mac_dialog_next_event(rt, &e)) outcome = handle(rt, s, &e);
    if (outcome == PENDING) {
        if (s->dirty) { mac_stdfile_draw(rt, s); s->dirty = 0; }
        mac_cpu_set(rt->cpu, MAC_PC, s->trap_pc);   /* stay in the dialog: run this trap again next slice */
        mac_cpu_yield(rt->cpu);
        return 1;
    }
    write_reply(rt, s, outcome);
    mac_stdfile_restore(rt, s);
    uint32_t sp = s->sp, pop = s->pop_bytes;
    mac_stdfile_destroy(rt);
    mac_cpu_set(rt->cpu, MAC_A7, sp + pop);
    return 1;
}

int mac_stdfile_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9ea) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), pc = (mac_cpu_get(rt->cpu, MAC_PC) - 2) & mac_cpu_addr_mask(rt->cpu);
    if ((sp & 1) || !valid(rt, sp, 2)) return fail(rt, "Invalid Pack3 stack.");
    unsigned selector = rd(rt, sp, 2);
    if (rt->last_trap_pc >= 0x2000 && rt->last_trap_pc < 0x2000 + 4096 * 8)
        return fail(rt, "Standard File reached through a patched trap vector is unsupported.");
    if (selector == 1 || selector == 5 || rt->stdput) {
        if (rt->stdput && selector != 1 && selector != 5) return fail(rt, "Pack3 called while a Put dialog is open.");
        return mac_stdput_service(rt, sp, pc, selector == 5);
    }
    MacSfState *s = rt->stdfile;
    if (s) {
        if (s->stage != SF_STAGE_RUN || pc != s->trap_pc || sp != s->sp) return fail(rt, "Standard File called again while a dialog is open.");
        return step(rt, s);
    }
    if (selector != SELECTOR_GET && selector != SELECTOR_PGET && selector != SELECTOR_SGET) {
        char msg[160]; snprintf(msg, sizeof msg, "Unsupported Pack3 selector $%04x (only SFPutFile, SFGetFile, SFPGetFile, StandardPutFile and StandardGetFile).", selector);
        return fail(rt, msg);
    }
    int rc = begin(rt, sp, pc, selector);
    if (rc < 0) return -1;
    if (rt->stdfile && rt->stdfile->stage == SF_STAGE_RUN && !rt->guest.active) return step(rt, rt->stdfile); /* no filter: open at once */
    return 1;
}
