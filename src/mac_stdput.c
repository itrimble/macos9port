#include "mac_dialog_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pack3 selector 1, SFPutFile ($A9EA): the "save as" dialog, as a native modal in the style of the alert and Standard File ones.
 *
 * SFPutFile(where: Point; prompt: Str255; origName: Str255; dlgHook: ProcPtr; VAR reply: SFReply). Stack from SP, Pascal order:
 * selector2, reply4, dlgHook4, origName4, prompt4, where4 (22 bytes). The guest is stopped inside the trap; it rewinds its PC and
 * yields each slice, like the Alert and Standard File dialogs, so with no input a run just stays in the dialog.
 *
 * This is a reduced dialog of our own, not the system one: the prompt, "Save in: OS Pi Data", one editable name (1 to 31 characters,
 * printable ASCII without ':'; Backspace deletes), a Save button (default, dimmed while the name is empty) and a Cancel button. There
 * is no file list, no Eject or Drive and no "replace existing file?" alert: a name that already exists is returned as it is and the
 * guest's own Create/Open decides what happens. Return and Enter save, Escape and Command-period cancel. A dialog hook halts. origName
 * longer than 31 characters is cut to 31 and ':' in it becomes '-'. SFReply: good, copy = 0, fType = 0, vRefNum = $FFFF (the one
 * volume), version = 0, fName = the name. */
typedef struct MacPutState {
    uint32_t sp, pc, reply;
    int left, top, width, height;
    char prompt[64], name[32]; unsigned nlen;
    uint8_t *backdrop;
    int pressed, dirty, drawn;
} MacPutState;
enum { W = 300, H = 140, SAVE = 1, CANCEL = 2 };
static const int BTN[3][4] = { {0,0,0,0}, {200, 100, 280, 120}, {110, 100, 190, 120} };

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }

void mac_stdput_destroy(MacRuntime *rt) {
    if (!rt->stdput) return;
    free(rt->stdput->backdrop); free(rt->stdput); rt->stdput = NULL;
}
static int hit(const MacPutState *s, int x, int y) {
    for (int i = SAVE; i <= CANCEL; i++)
        if (x >= s->left + BTN[i][0] && x < s->left + BTN[i][2] && y >= s->top + BTN[i][1] && y < s->top + BTN[i][3]) return i;
    return 0;
}
static int enabled(const MacPutState *s, int item) { return item == CANCEL || (item == SAVE && s->nlen > 0); }

static void draw(MacRuntime *rt, const MacPutState *s) {
    MacPen pen; mac_pen_init(&pen, rt, s->left, s->top, s->width, s->height);
    mac_pen_frame(&pen);
    mac_pen_wrap(&pen, s->left + 16, s->top + 12, s->left + W - 16, s->top + 40, s->prompt, (unsigned)strlen(s->prompt));
    mac_pen_text(&pen, s->left + 16, s->top + 46, "Save in: OS Pi Data", 19, pen.dark, 0);
    mac_pen_box(&pen, s->left + 16, s->top + 62, s->left + W - 16, s->top + 80, pen.dark);
    mac_pen_fill(&pen, s->left + 17, s->top + 63, s->left + W - 17, s->top + 79, pen.light);
    mac_pen_text(&pen, s->left + 20, s->top + 67, s->name, s->nlen, pen.dark, 0);
    mac_pen_fill(&pen, s->left + 20 + (int)s->nlen * 8, s->top + 66, s->left + 22 + (int)s->nlen * 8, s->top + 77, pen.dark);   /* the insertion bar */
    mac_pen_button(&pen, s->left + BTN[SAVE][0], s->top + BTN[SAVE][1], s->left + BTN[SAVE][2], s->top + BTN[SAVE][3], (const uint8_t *)"Save", 4, enabled(s, SAVE), s->pressed == SAVE, 1);
    mac_pen_button(&pen, s->left + BTN[CANCEL][0], s->top + BTN[CANCEL][1], s->left + BTN[CANCEL][2], s->top + BTN[CANCEL][3], (const uint8_t *)"Cancel", 6, 1, s->pressed == CANCEL, 0);
}

static int begin(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    if (!valid(rt, sp, 22)) return fail(rt, "SFPutFile stack is invalid.");
    uint32_t reply = rd(rt, sp + 2, 4), hook = rd(rt, sp + 6, 4), orig = rd(rt, sp + 10, 4), prompt = rd(rt, sp + 14, 4), where = rd(rt, sp + 18, 4);
    if (hook) return fail(rt, "SFPutFile dialog hooks are unsupported.");
    if (!valid(rt, reply, 74)) return fail(rt, "SFPutFile reply record is invalid.");
    if (!valid(rt, prompt, 1) || !valid(rt, prompt, (size_t)rd(rt, prompt, 1) + 1)) return fail(rt, "SFPutFile prompt is invalid.");
    if (!valid(rt, orig, 1) || !valid(rt, orig, (size_t)rd(rt, orig, 1) + 1)) return fail(rt, "SFPutFile origName is invalid.");
    if (rt->screen_depth != 8 || rt->screen_width != 640 || rt->screen_height != 480 || rt->screen_row_bytes != 640 || !valid(rt, rt->screen_base, 640 * 480))
        return fail(rt, "SFPutFile requires the initialized 640x480 indexed display.");
    MacPutState *s = calloc(1, sizeof *s);
    if (!s) return fail(rt, "SFPutFile could not allocate its state.");
    s->backdrop = malloc((size_t)W * H);
    if (!s->backdrop) { free(s); return fail(rt, "SFPutFile could not allocate its backdrop."); }
    rt->stdput = s;
    s->sp = sp; s->pc = pc; s->reply = reply; s->width = W; s->height = H;
    int top = (int16_t)(where >> 16), left = (int16_t)where;
    if (top + H > 480) top = 480 - H;
    if (left + W > 640) left = 640 - W;
    s->top = top < 0 ? 0 : top; s->left = left < 0 ? 0 : left;
    unsigned n = rd(rt, prompt, 1); if (n > sizeof s->prompt - 1) n = sizeof s->prompt - 1;
    memcpy(s->prompt, mac_cpu_ram(rt->cpu) + prompt + 1, n);
    n = rd(rt, orig, 1); if (n > 31) n = 31;
    for (unsigned i = 0; i < n; i++) { char c = (char)mac_cpu_ram(rt->cpu)[orig + 1 + i]; s->name[i] = (c == ':' || c < 32 || c > 126) ? '-' : c; }
    s->nlen = n;
    return 0;
}

enum { PENDING = 0, DONE_SAVE = 1, DONE_CANCEL = 2 };
static int handle(MacPutState *s, const MacEvent *e) {
    if (e->what == 3 || e->what == 5) {
        unsigned ch = e->message & 0xff;
        if (ch == 13 || ch == 3) return s->nlen ? DONE_SAVE : PENDING;
        if (ch == 27 || ((e->modifiers & 0x100) && ch == '.')) return DONE_CANCEL;
        if (ch == 8 || ch == 0x7f) { if (s->nlen) s->nlen--; }
        else if (ch >= 32 && ch < 127 && ch != ':' && !(e->modifiers & 0x100) && s->nlen < 31) s->name[s->nlen++] = (char)ch;
        s->name[s->nlen] = 0; s->dirty = 1;
        return PENDING;
    }
    int item = hit(s, e->x, e->y);
    if (e->what == 1) { if (item && enabled(s, item)) { s->pressed = item; s->dirty = 1; } return PENDING; }
    int pressed = s->pressed; s->pressed = 0;
    if (pressed) s->dirty = 1;
    return pressed && item == pressed && enabled(s, item) ? (item == SAVE ? DONE_SAVE : DONE_CANCEL) : PENDING;
}

static int step(MacRuntime *rt, MacPutState *s) {
    MacPen pen; mac_pen_init(&pen, rt, s->left, s->top, s->width, s->height);
    if (!s->drawn) { mac_pen_save(&pen, s->backdrop); s->drawn = 1; s->dirty = 1; }
    int outcome = PENDING; MacEvent e;
    while (outcome == PENDING && mac_dialog_next_event(rt, &e)) outcome = handle(s, &e);
    if (outcome == PENDING) {
        if (s->dirty) { mac_pen_restore(&pen, s->backdrop); draw(rt, s); s->dirty = 0; }
        mac_cpu_set(rt->cpu, MAC_PC, s->pc); mac_cpu_yield(rt->cpu);
        return 1;
    }
    mac_pen_restore(&pen, s->backdrop);
    uint8_t *ram = mac_cpu_ram(rt->cpu) + s->reply;
    memset(ram, 0, 74);
    if (outcome == DONE_SAVE) { ram[0] = 1; mac_cpu_write(rt->cpu, s->reply + 6, 2, 0xffff); ram[10] = (uint8_t)s->nlen; memcpy(ram + 11, s->name, s->nlen); }
    uint32_t sp = s->sp;
    mac_stdput_destroy(rt);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 22);
    return 1;
}

int mac_stdput_service(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    MacPutState *s = rt->stdput;
    if (s) {
        if (pc != s->pc || sp != s->sp) return fail(rt, "SFPutFile called again while its dialog is open.");
        return step(rt, s);
    }
    if (rt->stdfile) return fail(rt, "SFPutFile called while a Standard File dialog is open.");
    if (begin(rt, sp, pc)) return -1;
    return step(rt, rt->stdput);
}
