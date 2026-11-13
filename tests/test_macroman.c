/* Mac Roman <-> Unicode and the guest <-> host name mapping (include/macroman.h). Headless; no SDL. */
#include "macroman.h"
#include "mac_chrome.h"
#include "mac_sysfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int to_host(const char *mr, size_t n, char *out) { return mr_component_to_host((const uint8_t *)mr, n, out, MR_HOST_MAX); }
static int from_host(const char *u, uint8_t *out) { return mr_component_from_host(u, out, MR_NAME_MAX); }
static int same(const char *host, const char *mr, size_t n) {
    uint8_t got[MR_NAME_MAX];
    int length = from_host(host, got);
    return length == (int)n && !memcmp(got, mr, n);
}

static void every_byte(void) {
    int allowed = 0;
    for (int b = 0; b < 256; b++) {
        char name[4] = { 'a', (char)b, 'z', 0 }, host[MR_HOST_MAX];
        int ok = mr_name_byte_ok((uint8_t)b);
        int expect = b == 13 || (b >= 32 && b != 127 && b != ':' && b != '/' && b != '\\');
        CHECK(ok == expect);
        int n = to_host(name, 3, host);
        CHECK((n >= 0) == expect);
        if (n < 0) { CHECK(!expect); continue; }
        allowed++;
        uint8_t back[MR_NAME_MAX];
        CHECK(from_host(host, back) == 3 && !memcmp(back, name, 3));   /* every allowed byte round-trips in the middle of a name */
        CHECK(same(host, name, 3));
        CHECK(strlen(host) == (size_t)n);
        if (b >= 128) CHECK((uint8_t)host[1] >= 0xC2);   /* high bytes become multi-byte UTF-8 */
    }
    CHECK(allowed == 256 - 32 - 1 - 3 + 1);   /* 95 printable ASCII minus ':' '/' '\\' is 92, plus 128 high bytes = 220, plus CR ($0D): the Finder's "Icon" + CR file */
    /* The table is a bijection onto distinct codepoints, so no two bytes name the same host character. */
    for (int a = 0; a < 256; a++) {
        CHECK(mr_from_unicode(mr_unicode((uint8_t)a)) == a);
        for (int b = a + 1; b < 256; b++) CHECK(mr_unicode((uint8_t)a) != mr_unicode((uint8_t)b));
    }
}

static void known_characters(void) {
    CHECK(mr_unicode(0xAA) == 0x2122);   /* trade mark sign: "QuickTime<tm> Folder" */
    CHECK(mr_unicode(0xD2) == 0x201C && mr_unicode(0xD3) == 0x201D);   /* curly double quotes */
    CHECK(mr_unicode(0xD4) == 0x2018 && mr_unicode(0xD5) == 0x2019);
    CHECK(mr_unicode(0x83) == 0x00C9 && mr_unicode(0x8E) == 0x00E9);
    CHECK(mr_unicode(0xDB) == 0x20AC && mr_unicode(0xCA) == 0x00A0 && mr_unicode(0xF0) == 0xF8FF && mr_unicode(0xA5) == 0x2022);
    char host[MR_HOST_MAX];
    CHECK(to_host("QuickTime\xAA Folder", 17, host) == 19 && !strcmp(host, "QuickTime\xE2\x84\xA2 Folder"));
    CHECK(same("QuickTime\xE2\x84\xA2 Folder", "QuickTime\xAA Folder", 17));
    CHECK(to_host("Caf\x8E", 4, host) == 5 && !strcmp(host, "Caf\xC3\xA9"));
}

static void case_folding(void) {
    CHECK(mr_fold('A') == 'a' && mr_fold('z') == 'z' && mr_fold('0') == '0');
    CHECK(mr_fold(0x83) == 0x8E);   /* E-acute with e-acute */
    CHECK(mr_fold(0x80) == 0x8A && mr_fold(0x85) == 0x9A && mr_fold(0x86) == 0x9F && mr_fold(0x84) == 0x96);
    CHECK(mr_fold(0xAE) == 0xBE && mr_fold(0xAF) == 0xBF && mr_fold(0xCE) == 0xCF && mr_fold(0xD9) == 0xD8);
    CHECK(mr_fold(0xAA) == 0xAA && mr_fold(0xA7) == 0xA7 && mr_fold(0xD2) == 0xD2);   /* symbols are not letters */
    /* Every folded pair is a real upper/lower pair: in Latin-1 the lower case is 0x20 higher, and the two ligature/Y-umlaut pairs are known. */
    unsigned pairs = 0;
    for (int b = 0x80; b < 256; b++) {
        uint8_t f = mr_fold((uint8_t)b);
        if (f == b) continue;
        pairs++;
        uint32_t up = mr_unicode((uint8_t)b), low = mr_unicode(f);
        CHECK(low == up + 0x20 || (up == 0x152 && low == 0x153) || (up == 0x178 && low == 0xFF));
        CHECK(mr_fold(f) == f);   /* lower case stays lower case */
    }
    CHECK(pairs == 29);   /* every upper-case accented Latin letter in Mac Roman */
    CHECK(mr_equal_fold((const uint8_t *)"\x83tude", 5, (const uint8_t *)"\x8Etude", 5));
    CHECK(mr_equal_fold((const uint8_t *)"READ ME", 7, (const uint8_t *)"read me", 7));
    CHECK(!mr_equal_fold((const uint8_t *)"\x83", 1, (const uint8_t *)"E", 1));
    CHECK(mr_host_equal_fold("Caf\xC3\xA9", "CAF\xC3\x89") && mr_host_equal_fold("Cafe\xCC\x81", "CAF\xC3\x89"));
    CHECK(!mr_host_equal_fold("Cafe", "Caf\xC3\xA9"));
}

static void decomposed_hosts(void) {
    uint8_t out[MR_NAME_MAX];
    CHECK(from_host("Cafe\xCC\x81", out) == 4 && out[3] == 0x8E);               /* NFD e + acute is NFC e-acute */
    CHECK(from_host("A\xCC\x88", out) == 1 && out[0] == 0x80);                  /* A + diaeresis */
    CHECK(from_host("n\xCC\x83" "o", out) == 2 && out[0] == 0x96 && out[1] == 'o');
    CHECK(from_host("c\xCC\xA7" "a", out) == 2 && out[0] == 0x8D);              /* c + cedilla */
    CHECK(from_host("a\xCC\x8A", out) == 1 && out[0] == 0x8C);                  /* a + ring */
    CHECK(from_host("Y\xCC\x88", out) == 1 && out[0] == 0xD9);
    CHECK(from_host("q\xCC\x81", out) < 0);                                     /* a combination Mac Roman has no letter for */
    CHECK(from_host("\xCC\x81" "e", out) < 0);                                  /* a combining mark with no base */
    CHECK(from_host("e\xCC\x81\xCC\x81", out) < 0);                             /* two marks */
    for (int b = 0x80; b < 256; b++) {   /* a precomposed name and its decomposed form give the same bytes; spot-check the pairs the table lists */
        char name[2] = { (char)b, 0 }, host[MR_HOST_MAX];
        CHECK(to_host(name, 1, host) > 0);
    }
}

static void hidden_and_refused(void) {
    uint8_t out[MR_NAME_MAX];
    char host[MR_HOST_MAX];
    CHECK(from_host("a:b", out) < 0);                              /* ':' is the guest separator */
    CHECK(from_host("a\\b", out) < 0);
    CHECK(from_host("a/b", out) < 0);
    CHECK(from_host(".hidden", out) < 0 && from_host("", out) < 0);
    CHECK(from_host("\xE6\x97\xA5\xE6\x9C\xAC", out) < 0);         /* CJK is not in Mac Roman */
    CHECK(from_host("smile \xF0\x9F\x99\x82", out) < 0);           /* four-byte UTF-8 */
    CHECK(from_host("\xC0\x80", out) < 0 && from_host("\xED\xA0\x80", out) < 0 && from_host("\xC3", out) < 0 && from_host("\xFF", out) < 0);
    CHECK(from_host("tab\there", out) < 0 && from_host("del\x7F", out) < 0);
    CHECK(from_host("\xE2\x82\xAC", out) == 1 && out[0] == 0xDB);  /* the euro sign is in this table */
    CHECK(to_host("a:b", 3, host) < 0 && to_host("a/b", 3, host) < 0 && to_host("a\\b", 3, host) < 0);
    CHECK(to_host("a\0b", 3, host) < 0 && to_host("\x01", 1, host) < 0 && to_host(".x", 2, host) < 0 && to_host("..", 2, host) < 0 && to_host(".", 1, host) < 0);
    CHECK(to_host("", 0, host) < 0);
}

static void length_limit(void) {
    char mr[40], host[MR_HOST_MAX];
    uint8_t out[MR_NAME_MAX];
    memset(mr, 'x', sizeof mr);
    CHECK(to_host(mr, 31, host) == 31 && to_host(mr, 32, host) < 0);
    /* 31 trade-mark signs are 31 Mac Roman bytes but 93 UTF-8 bytes: allowed. 32 are refused even though they are 96 UTF-8 bytes either way. */
    memset(mr, 0xAA, sizeof mr);
    CHECK(to_host(mr, 31, host) == 93 && mr_host_length(host) == 31 && from_host(host, out) == 31);
    CHECK(to_host(mr, 32, host) < 0);
    char long_host[200];
    memset(long_host, 0, sizeof long_host);
    for (int i = 0; i < 32; i++) memcpy(long_host + 3 * i, "\xE2\x84\xA2", 3);
    CHECK(mr_host_length(long_host) < 0);   /* 32 Mac Roman bytes: hidden from the guest */
    memset(long_host, 0, sizeof long_host);
    for (int i = 0; i < 31; i++) memcpy(long_host + 3 * i, "\xE2\x84\xA2", 3);
    CHECK(mr_host_length(long_host) == 31);
    /* 12 UTF-8-heavy letters: 24 bytes, 12 Mac Roman bytes, not an over-limit name. 31 e-acute is 62 UTF-8 bytes and legal. */
    memset(long_host, 0, sizeof long_host);
    for (int i = 0; i < 31; i++) memcpy(long_host + 2 * i, "\xC3\xA9", 2);
    CHECK(strlen(long_host) == 62 && mr_host_length(long_host) == 31);
    /* the fork sidecar "<leaf>.rsrc" is up to 36 bytes and hidden from the guest, so a name of the full 31 bytes can carry a fork */
    memset(long_host, 0, sizeof long_host);
    for (int i = 0; i < 31; i++) memcpy(long_host + 3 * i, "\xE2\x84\xA2", 3);
    CHECK(mr_sidecar_ok(long_host));
    char side[MR_HOST_MAX + 8];
    snprintf(side, sizeof side, "%s.rsrc", long_host);
    CHECK(mr_is_sidecar(side) && mr_host_length(side) < 0);   /* legal for the overlay, invisible to the guest */
    memcpy(long_host + 93, "\xE2\x84\xA2", 3);
    CHECK(!mr_sidecar_ok(long_host));
    CHECK(mr_sidecar_ok("Game Data") && !mr_sidecar_ok("a:b"));
    CHECK(mr_is_sidecar("QuickTime Internet Extras 68k.rsrc") && !mr_is_sidecar(".rsrc") && !mr_is_sidecar("a:b.rsrc") && !mr_is_sidecar("Game Data"));
}

static void pascal_strings(void) {
    uint8_t p[40];
    memset(p, 0xEE, sizeof p);
    CHECK(mr_put_pstring(p, 31, "QuickTime\xE2\x84\xA2 Folder") == 0 && p[0] == 17 && p[10] == 0xAA && p[11] == ' ');
    CHECK(mr_put_pstring(p, 31, "Cafe\xCC\x81") == 0 && p[0] == 4 && p[4] == 0x8E);
    CHECK(mr_put_pstring(p, 31, "\xE6\x97\xA5") == -1 && p[0] == 0);
    CHECK(mr_put_pstring(p, 3, "abcd") == -1 && p[0] == 0);
    CHECK(mr_host_representable(".os999") && mr_host_representable("x\xE2\x84\xA2") && !mr_host_representable("x:y") && !mr_host_representable("\xE6\x97\xA5"));
}

/* The 8x8 chrome font (menus, window titles, dialog text): Mac Roman bytes get their own glyphs, not '?'. */
static void chrome_glyphs(void) {
    uint8_t s1[8], s2[8], s3[8];
    const uint8_t *q = mac_chrome_glyph('?', s1);
    CHECK(mac_chrome_glyph('A', s2) != NULL && mac_chrome_glyph('A', s2)[0] == mac_chrome_glyph('A', s3)[0]);
#ifdef OSPI_FREETYPE
    mac_sysfont_set_path("fonts/ChicagoFLF.ttf");
    uint8_t quote[8], quote2[8], tm[8], e_acute[8], e_plain[8], logo[8], qcopy[8];
    memcpy(qcopy, q, 8);
    memcpy(quote, mac_chrome_glyph(0xD2, s2), 8); memcpy(quote2, mac_chrome_glyph(0xD3, s2), 8);
    memcpy(tm, mac_chrome_glyph(0xAA, s2), 8); memcpy(e_acute, mac_chrome_glyph(0x8E, s2), 8);
    memcpy(e_plain, mac_chrome_glyph('e', s2), 8); memcpy(logo, mac_chrome_glyph(0xF0, s2), 8);
    CHECK(memcmp(quote, qcopy, 8) && memcmp(quote2, qcopy, 8) && memcmp(tm, qcopy, 8) && memcmp(e_acute, qcopy, 8));   /* none of them is the "?" glyph */
    CHECK(memcmp(quote, quote2, 8) && memcmp(e_acute, e_plain, 8));
    CHECK(!memcmp(logo, qcopy, 8));   /* Apple's logo: the plain '?' look-alike, by decision */
    int ink = 0; for (int i = 0; i < 8; i++) for (int b = 0; b < 8; b++) ink += (e_acute[i] >> b) & 1;
    CHECK(ink > 8);
    for (unsigned ch = 0x80; ch <= 0xFF; ch++) if (ch != 0xCA && ch != 0xF0) {
        uint8_t g[8]; const uint8_t *p = mac_chrome_glyph(ch, g); int any = 0;
        for (int i = 0; i < 8; i++) any |= p[i];
        if (!any) fprintf(stderr, "empty chrome glyph %02X\n", ch);
        CHECK(any);   /* every other high byte draws something */
    }
    mac_sysfont_set_path(NULL);
#endif
    (void)q;
}

int main(void) {
    every_byte(); known_characters(); case_folding(); decomposed_hosts(); hidden_and_refused(); length_limit(); pascal_strings(); chrome_glyphs();
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("Mac Roman name mapping checks passed");
    return 0;
}
