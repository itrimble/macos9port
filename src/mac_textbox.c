#include "mac_text.h"
#include <string.h>

/* TextBox(text: Ptr; length: LONGINT; box: Rect; just: INTEGER) ($A9CE): the text word-wrapped in the rectangle with the current port's font, size and face,
 * left (0 or 2, systemDefault), centred (1) or right (-1) aligned, one line height (ascent + descent + leading) apart, the first baseline one ascent below the
 * top. A line ends at a carriage return or before the word that would pass the right edge; a word wider than the box is split. Like the real call (which is
 * TextEdit's), it clips to the box, leaves the pen where it was and draws nothing for an empty text. Semantics and spacing are from memory, not measured. */
enum { MAX_TEXT = 32767 };
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ok(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static int width(MacRuntime *rt, const MacTextFace *f, uint32_t at, int n) {
    int w = 0;
    for (int i = 0; i < n; i++) w += mac_text_advance(f, mac_cpu_ram(rt->cpu)[at + (uint32_t)i]);
    return w;
}

int mac_textbox_service(MacRuntime *rt, uint16_t op) {
    if (op != 0xa9ce) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7), port = rt->current_port;
    if ((sp & 1) || !ok(rt, sp, 14)) return fail(rt, "TextBox has an invalid stack.");
    int just = (int16_t)mac_cpu_read(cpu, sp, 2);
    uint32_t box = mac_cpu_read(cpu, sp + 2, 4), text = mac_cpu_read(cpu, sp + 10, 4);
    int32_t length = (int32_t)mac_cpu_read(cpu, sp + 6, 4);
    if (port < 0x400 || (port & 1) || !ok(rt, port, 108)) return fail(rt, "TextBox needs a current port.");
    if (!ok(rt, box, 8)) return fail(rt, "TextBox rectangle is outside guest RAM.");
    if (length < 0 || length > MAX_TEXT || (length && !ok(rt, text, (size_t)length))) return fail(rt, "TextBox text is outside guest RAM or too long.");
    MacTextFace f;
    if (mac_text_face(rt, (int16_t)mac_cpu_read(cpu, port + 68, 2), (int16_t)mac_cpu_read(cpu, port + 74, 2), mac_cpu_read(cpu, port + 70, 1), 0, &f)) return -1;
    int r[4];
    for (int i = 0; i < 4; i++) r[i] = (int16_t)mac_cpu_read(cpu, box + (uint32_t)i * 2, 2);   /* top, left, bottom, right */
    int line = f.s.ascent + f.s.descent + f.s.leading, span = r[3] - r[1], y = r[0] + f.s.ascent;
    if (line < 1) line = 1;
    int hidden = (int16_t)mac_cpu_read(cpu, port + 66, 2) < 0;
    uint32_t p = text, end = text + (uint32_t)length;
    while (p < end && span > 0 && y - f.s.ascent < r[2] && !hidden) {
        const uint8_t *ram = mac_cpu_ram(cpu);
        uint32_t q = p, lastspace = 0; int w = 0;
        while (q < end && ram[q] != 13) {                                       /* grow the line one character at a time */
            int a = mac_text_advance(&f, ram[q]);
            if (w + a > span && q > p) break;
            if (ram[q] == ' ') lastspace = q;
            w += a; q++;
        }
        uint32_t next = q;
        if (q < end && ram[q] != 13 && lastspace > p) { q = lastspace; next = lastspace + 1; w = width(rt, &f, p, (int)(q - p)); }   /* wrap before the word */
        else if (q < end && ram[q] == 13) next = q + 1;
        int tw = width(rt, &f, p, (int)(q - p));                                /* trailing spaces count: a line is as wide as its text */
        int x = just == 1 ? r[1] + (span - tw) / 2 : just == -1 ? r[3] - tw : r[1];
        if (q > p && mac_text_draw_with(rt, port, &f, -1, x, y, p, (int)(q - p), r)) return -1;
        (void)w; p = next; y += line;
    }
    mac_cpu_set(cpu, MAC_A7, sp + 14);
    return 1;
}
