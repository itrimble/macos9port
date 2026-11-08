#include "text_layout.h"

#include <string.h>

#ifdef OS999_TEXT_UNIBREAK
#include <linebreak.h>
#endif

static int utf8_one(const unsigned char *s, size_t n, uint32_t *cp, size_t *used) {
    if (!n) return -1;
    if (s[0] < 0x80) { *cp = s[0]; *used = 1; return 0; }
    unsigned value, need, minimum;
    if (s[0] >= 0xc2 && s[0] <= 0xdf) { value = s[0] & 31u; need = 1; minimum = 0x80; }
    else if (s[0] >= 0xe0 && s[0] <= 0xef) { value = s[0] & 15u; need = 2; minimum = 0x800; }
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) { value = s[0] & 7u; need = 3; minimum = 0x10000; }
    else return -1;
    if (n <= need) return -1;
    for (unsigned i = 1; i <= need; i++) {
        if ((s[i] & 0xc0u) != 0x80u) return -1;
        value = (value << 6) | (s[i] & 63u);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) return -1;
    *cp = value; *used = need + 1; return 0;
}

#ifndef OS999_TEXT_UNIBREAK
static int cjk(uint32_t cp) {
    return (cp >= 0x2e80u && cp <= 0x9fffu) || (cp >= 0xac00u && cp <= 0xd7afu) ||
           (cp >= 0xf900u && cp <= 0xfaffu) || (cp >= 0x20000u && cp <= 0x323afu);
}
#endif

int text_layout_breaks(const char *utf8, size_t length, const char *language,
                       uint8_t *breaks, size_t capacity) {
    if ((!utf8 && length) || !breaks || capacity < length) return -1;
    memset(breaks, 0, length);
    const unsigned char *s = (const unsigned char *)utf8;
    for (size_t i = 0; i < length;) {
        uint32_t cp; size_t used;
        if (utf8_one(s + i, length - i, &cp, &used)) return -1;
        i += used;
    }
#ifdef OS999_TEXT_UNIBREAK
    if (length) {
        char *raw = (char *)breaks;
        set_linebreaks_utf8((const utf8_t *)utf8, length, language, raw);
        for (size_t i = 0; i < length;) {
            uint32_t cp; size_t used;
            utf8_one(s + i, length - i, &cp, &used); (void)cp;
            char status = raw[i + used - 1];
            memset(breaks + i, 0, used);
            breaks[i] = status == LINEBREAK_MUSTBREAK ? TEXT_LAYOUT_BREAK_MUST :
                        status == LINEBREAK_ALLOWBREAK ? TEXT_LAYOUT_BREAK_ALLOW :
                        TEXT_LAYOUT_BREAK_NONE;
            i += used;
        }
    }
#else
    (void)language;
    for (size_t i = 0; i < length;) {
        uint32_t cp, next = 0; size_t used, next_used = 0;
        utf8_one(s + i, length - i, &cp, &used);
        size_t next_at = i + used;
        if (next_at < length) utf8_one(s + next_at, length - next_at, &next, &next_used);
        if (cp == '\n' || cp == '\r') breaks[i] = TEXT_LAYOUT_BREAK_MUST;
        else if (cp == ' ' || cp == '\t' || cp == '-' || cp == 0x2010u)
            breaks[i] = TEXT_LAYOUT_BREAK_ALLOW;
        else if (cjk(cp) && next_used && cjk(next)) breaks[i] = TEXT_LAYOUT_BREAK_ALLOW;
        i = next_at;
    }
#endif
    return 0;
}
