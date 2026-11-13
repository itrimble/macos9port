#include "mac_runtime.h"
#include "mac_handles.h"
#include <stdio.h>
#include <string.h>

/* International Utilities (Pack6, $A9ED). The selector is a word on top of the stack, the arguments below it in Pascal order. Selector numbers (checked against Apple's public TextUtils.h and Script.h):
 * 0 IUDateString, 2 IUTimeString, 6 IUGetIntl (GetIntlResource), $E IUDatePString, $10 IUTimePString. Every other selector (including the string comparisons) halts by name.
 *
 * Resources: a program's own 'itl0'/'itl1' (id 0) are used when its resource fork carries them. Otherwise this runtime BUILDS a US English pair at run time from the documented field layout
 * (IntlResources.h, Inside Macintosh: Text): Intl0Rec, 32 bytes: decimalPt 0, thousSep 1, listSep 2, currSym 3..5, currFmt 6, dateOrder 7, shrtDateFmt 8, dateSep 9, timeCycle 10, timeFmt 11,
 * mornStr 12..15, eveStr 16..19, timeSep 20, time suffixes 21..28, metricSys 29, intl0Vers 30; Intl1Rec, 332 bytes: days[7] and months[12] as Str15 from 0, suppressDay 304, lngDateFmt 305,
 * dayLeading0 306, abbrLen 307, st0..st4 308..327, intl1Vers 328, localRtn 330. The values are OS999's own (US separators, month-day-year, 12-hour clock, English names), made by the code below,
 * not bytes of any Apple System resource. The handle is created once and handed out again, like a resource in the system heap. IUGetIntl for an id without a resource halts by name.
 * Dates: the argument is seconds since 1 January 1904 as the program holds it (local time); the short date is built from itl0 (order, separator, leading zeros, century), the time from itl0
 * (cycle, separator, leading zeros, AM/PM strings); long and abbreviated dates use itl1's names with the US layout and are produced only for the built-in itl1. */
enum { ITL0 = 0x69746c30, ITL1 = 0x69746c31, I0 = 32, I1 = 332 };
static uint32_t rd(MacRuntime *r, uint32_t a, unsigned n) { return mac_cpu_read(r->cpu, a, n); }
static void wr(MacRuntime *r, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, a, n, v); }
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ram_ok(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static const char *const day_names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const month_names[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

static uint32_t builtin(MacRuntime *rt, int id) {
    if (rt->intl_handle[id]) return rt->intl_handle[id];
    uint32_t h = mac_handle_new(rt, id ? I1 : I0), p, n;
    if (!h || !mac_handle_block(rt, h, &p, &n) || n < (uint32_t)(id ? I1 : I0)) { fail(rt, "International Utilities could not allocate a resource."); return 0; }
    uint8_t *m = mac_cpu_ram(rt->cpu) + p;
    if (id == 0) {
        m[0] = '.'; m[1] = ','; m[2] = ';'; m[3] = '$'; m[4] = 0; m[5] = 0;
        m[6] = 0x10 | 0x80;                       /* currency symbol leads; leading zero in the units digit */
        m[7] = 0; m[8] = 0x80;                    /* month-day-year; the century is shown, no leading zeros */
        m[9] = '/'; m[10] = 0xff; m[11] = 0x40 | 0x20;   /* 12-hour clock, minutes and seconds with a leading zero */
        memcpy(m + 12, "\3 AM", 4); memcpy(m + 16, "\3 PM", 4);
        m[20] = ':'; m[29] = 0;                   /* inches, not metric */
        m[30] = 0; m[31] = 1;                     /* region verUS, version 1 */
    } else {
        for (int i = 0; i < 7; i++) { m[16 * i] = (uint8_t)strlen(day_names[i]); memcpy(m + 16 * i + 1, day_names[i], strlen(day_names[i])); }
        for (int i = 0; i < 12; i++) { m[112 + 16 * i] = (uint8_t)strlen(month_names[i]); memcpy(m + 112 + 16 * i + 1, month_names[i], strlen(month_names[i])); }
        m[304] = 0; m[305] = 0; m[306] = 0; m[307] = 3;
        memcpy(m + 308 + 4, "\2, ", 4); memcpy(m + 308 + 8, "\1 ", 4);   /* separators of the long date: ", " and " " */
        m[329] = 1;
    }
    rt->intl_handle[id] = h;
    return h;
}
/* The handle IUGetIntl(id) answers: the program's own resource, else the built-in one (ids 0 and 1), else 0 after halting. */
static uint32_t intl_resource(MacRuntime *rt, int id) {
    if (id == 0 || id == 1) {
        uint32_t own = mac_runtime_resource(rt, id ? ITL1 : ITL0, 0);
        if (own) return own;
        return builtin(rt, id);
    }
    char w[96]; snprintf(w, sizeof w, "IUGetIntl(%d): no itl%d resource in the program and none built in (only itl0 and itl1 are).", id, id);
    fail(rt, w); return 0;
}
static int get_block(MacRuntime *rt, uint32_t h, unsigned min, uint32_t *p) { uint32_t n; return mac_handle_block(rt, h, p, &n) && n >= min && ram_ok(rt, *p, n); }
static void civil(uint32_t secs, int *y, int *mo, int *d, int *wd, int *hh, int *mi, int *ss) {
    uint32_t days = secs / 86400u, rem = secs % 86400u;
    *hh = (int)(rem / 3600); *mi = (int)(rem % 3600 / 60); *ss = (int)(rem % 60); *wd = (int)((days + 5) % 7);   /* 1 January 1904 was a Friday */
    int64_t z = (int64_t)days - 24107 + 719468;   /* 1904-01-01 is 24107 days before 1970-01-01; 719468 days separate 1970-01-01 from 0000-03-01 */
    int64_t era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *mo = (int)(mp < 10 ? mp + 3 : mp - 9); *y = (int)(yy + (*mo <= 2));
}
static int put_pstr(MacRuntime *rt, uint32_t at, const char *s) {
    size_t n = strlen(s);
    if (n > 255 || !ram_ok(rt, at, n + 1)) return fail(rt, "An International Utilities result pointer is outside guest RAM.");
    wr(rt, at, 1, (uint32_t)n); for (size_t i = 0; i < n; i++) wr(rt, at + 1 + (uint32_t)i, 1, (uint8_t)s[i]);
    return 0;
}
static void add2(char *out, size_t cap, int v, int lead) { char t[16]; snprintf(t, sizeof t, lead ? "%02d" : "%d", v); strncat(out, t, cap - strlen(out) - 1); }
/* form: 0 short date, 1 long date, 2 abbreviated date. `h0` is the itl0 handle (short dates) and `h1` the itl1 handle (long and abbreviated ones), 0 meaning the default resources: IUDatePString's
 * handle is the one the form needs. */
static int date_string(MacRuntime *rt, uint32_t secs, int form, uint32_t h0, uint32_t h1, char *out, size_t cap) {
    uint32_t p0, p1; int y, mo, d, wd, hh, mi, ss;
    if (!h0) { h0 = intl_resource(rt, 0); if (!h0) return -1; }
    if (!get_block(rt, h0, I0, &p0)) return fail(rt, "IUDateString: the itl0 handle is not a live resource.");
    civil(secs, &y, &mo, &d, &wd, &hh, &mi, &ss); out[0] = 0;
    if (form == 0) {
        unsigned order = rd(rt, p0 + 7, 1), fmt = rd(rt, p0 + 8, 1); char sep[2] = {(char)rd(rt, p0 + 9, 1), 0};
        if (order > 5) return fail(rt, "IUDateString: itl0 names a date order that does not exist.");
        static const char *const ord[6] = {"mdy", "dmy", "ymd", "myd", "dym", "ydm"};
        for (int i = 0; i < 3; i++) {
            if (i) strncat(out, sep, cap - strlen(out) - 1);
            switch (ord[order][i]) {
            case 'm': add2(out, cap, mo, (fmt & 64) != 0); break;
            case 'd': add2(out, cap, d, (fmt & 32) != 0); break;
            default: if (fmt & 128) add2(out, cap, y, 0); else add2(out, cap, y % 100, 1); break;
            }
        }
        return 0;
    }
    if (!h1) { h1 = intl_resource(rt, 1); if (!h1) return -1; }
    if (h1 != rt->intl_handle[1]) return fail(rt, "IUDateString: long and abbreviated dates are produced only for this runtime's built-in itl1.");
    if (!get_block(rt, h1, I1, &p1)) return fail(rt, "IUDateString: the itl1 handle is not a live resource.");
    char dn[16] = {0}, mn[16] = {0}; unsigned abbr = rd(rt, p1 + 307, 1);
    unsigned ln = rd(rt, p1 + 16 * (unsigned)wd, 1), lm = rd(rt, p1 + 112 + 16 * (unsigned)(mo - 1), 1);
    for (unsigned i = 0; i < ln && i < 15; i++) dn[i] = (char)rd(rt, p1 + 16 * (unsigned)wd + 1 + i, 1);
    for (unsigned i = 0; i < lm && i < 15; i++) mn[i] = (char)rd(rt, p1 + 112 + 16 * (unsigned)(mo - 1) + 1 + i, 1);
    if (form == 2) { if (strlen(dn) > abbr) dn[abbr] = 0; if (strlen(mn) > abbr) mn[abbr] = 0; }
    snprintf(out, cap, "%s, %s %d, %d", dn, mn, d, y);
    return 0;
}
static int time_string(MacRuntime *rt, uint32_t secs, int want_secs, uint32_t h0, char *out, size_t cap) {
    uint32_t p0; int y, mo, d, wd, hh, mi, ss;
    if (!h0) { h0 = intl_resource(rt, 0); if (!h0) return -1; }
    if (!get_block(rt, h0, I0, &p0)) return fail(rt, "IUTimeString: the itl0 handle is not a live resource.");
    civil(secs, &y, &mo, &d, &wd, &hh, &mi, &ss); out[0] = 0;
    unsigned cycle = rd(rt, p0 + 10, 1), fmt = rd(rt, p0 + 11, 1); char sep[2] = {(char)rd(rt, p0 + 20, 1), 0}; int shown = hh;
    if (cycle == 255) shown = hh % 12 == 0 ? 12 : hh % 12; else if (cycle == 1) shown = hh % 12;
    add2(out, cap, shown, (fmt & 128) != 0); strncat(out, sep, cap - strlen(out) - 1); add2(out, cap, mi, (fmt & 64) != 0);
    if (want_secs) { strncat(out, sep, cap - strlen(out) - 1); add2(out, cap, ss, (fmt & 32) != 0); }
    if (cycle != 0) {                                  /* a 12-hour or zero-based cycle shows AM or PM */
        uint32_t s = hh < 12 ? p0 + 12 : p0 + 16; unsigned n = rd(rt, s, 1); char t[8] = {0};
        for (unsigned i = 0; i < n && i < 3; i++) t[i] = (char)rd(rt, s + 1 + i, 1);
        strncat(out, t, cap - strlen(out) - 1);
    }
    return 0;
}
int mac_intl_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9ed) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7); size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 24) return fail(rt, "International Utilities call has an invalid stack.");
    unsigned sel = rd(rt, sp, 2); uint32_t A = sp + 2; char text[128];
    switch (sel) {
    case 6: {                                                                          /* IUGetIntl(theID: INTEGER): Handle */
        uint32_t h = intl_resource(rt, (int16_t)rd(rt, A, 2)); if (!h) return -1;
        wr(rt, A + 2, 4, h); mac_cpu_set(rt->cpu, MAC_A7, A + 2); return 1; }
    case 0: case 0xe: {                                                                /* IUDateString(dateTime, longFlag, VAR result); IUDatePString(.., intlHandle) */
        int p = sel == 0xe; uint32_t h = p ? rd(rt, A, 4) : 0, res = rd(rt, A + (p ? 4 : 0), 4); int form = (int16_t)rd(rt, A + (p ? 8 : 4), 2); uint32_t secs = rd(rt, A + (p ? 10 : 6), 4);
        if (form < 0 || form > 2) return fail(rt, "IUDateString: a date form other than shortDate, longDate and abbrevDate.");
        if (date_string(rt, secs, form, form == 0 ? h : 0, form == 0 ? 0 : h, text, sizeof text) || put_pstr(rt, res, text)) return -1;
        mac_cpu_set(rt->cpu, MAC_A7, A + (p ? 14 : 10)); return 1; }
    case 2: case 0x10: {                                                               /* IUTimeString(dateTime, wantSeconds, VAR result); IUTimePString(.., intlHandle) */
        int p = sel == 0x10; uint32_t h = p ? rd(rt, A, 4) : 0, res = rd(rt, A + (p ? 4 : 0), 4); int ws = rd(rt, A + (p ? 8 : 4), 1) != 0; uint32_t secs = rd(rt, A + (p ? 10 : 6), 4);
        if (time_string(rt, secs, ws, h, text, sizeof text) || put_pstr(rt, res, text)) return -1;
        mac_cpu_set(rt->cpu, MAC_A7, A + (p ? 14 : 10)); return 1; }
    default: { char w[120]; snprintf(w, sizeof w, "International Utilities selector $%04X is unsupported (IUGetIntl, IUDateString, IUTimeString and their P forms are implemented).", sel); return fail(rt, w); }
    }
}
