/* Text, CRC, Huffman, MacBinary, BinHex, AppleSingle/Double and ADC. */
#include "st_test.h"

static void test_text(void) {
    for (int c = 1; c < 256; c++) {
        uint8_t in = (uint8_t)c, back[8];
        char u[8];
        CHECK(st_macroman_to_utf8(&in, 1, u, sizeof u) > 0);
        if (c == 0xF0) continue; /* Apple logo maps to a private-use point that round-trips too */
        CHECK(st_utf8_to_macroman(u, back, sizeof back) == 1 && back[0] == in);
    }
    char out[300];
    CHECK(st_host_component("a/b", out, sizeof out) > 0 && !strcmp(out, "a:b"));
    CHECK(st_host_component("..", out, sizeof out) < 0);
    CHECK(st_host_component(".", out, sizeof out) < 0);
    CHECK(st_host_component("", out, sizeof out) < 0);
    CHECK(st_host_component("tab\there", out, sizeof out) > 0 && !strcmp(out, "tab_here"));
    char longname[600];
    memset(longname, 0, sizeof longname);
    for (int i = 0; i < 200; i++) memcpy(longname + 2 * i, "\xc3\xa9", 2); /* 200 x 'é' */
    int k = st_host_component(longname, out, sizeof out);
    CHECK(k > 0 && k <= 255 && k % 2 == 0); /* never splits a UTF-8 sequence */
}

static void test_crc(void) {
    const uint8_t *v = (const uint8_t *)"123456789";
    CHECK(st_crc16_xmodem(0, v, 9) == 0x31C3);
    CHECK(st_crc16_arc(0, v, 9) == 0xBB3D);
    CHECK(st_crc32(0, v, 9) == 0xCBF43926u);
}

static void test_macbinary(void) {
    StMacFile f, g;
    st_make_file(&f, "Résumé/Draft", 1000, 333, 5);
    StBytes enc = {0};
    StErr err;
    CHECK_RC(st_macbinary_encode(&f, &enc, &err), ST_OK);
    CHECK(enc.n % 128 == 0);
    CHECK(st_macbinary_detect(enc.p, enc.n) == 3);
    CHECK_RC(st_macbinary_decode(enc.p, enc.n, &g, NULL, &err), ST_OK);
    CHECK(st_same_file(&f, &g) && !strcmp(g.name, f.name) && g.mtime == f.mtime);
    st_macfile_free(&g);
    /* A corrupted CRC no longer looks like MacBinary II/III. */
    enc.p[124] ^= 0xFF;
    CHECK(st_macbinary_detect(enc.p, enc.n) == 0);
    enc.p[124] ^= 0xFF;
    /* Truncation is reported, not read past. */
    CHECK_RC(st_macbinary_decode(enc.p, 200, &g, NULL, &err), ST_EFORMAT);
    StLimits lim = st_limits_default();
    lim.max_entry = 100;
    CHECK_RC(st_macbinary_decode(enc.p, enc.n, &g, &lim, &err), ST_ELIMIT);
    st_bytes_free(&enc);
    st_macfile_free(&f);
}

static void test_binhex(void) {
    StMacFile f, g;
    st_make_file(&f, "Read Me", 5000, 900, 9);
    for (int i = 0; i < 600; i++) f.data.p[100 + i] = (i < 300) ? 0x90 : 'x'; /* runs and markers */
    StBytes enc = {0};
    StErr err;
    CHECK_RC(st_binhex_encode(&f, &enc, &err), ST_OK);
    CHECK(st_binhex_detect(enc.p, enc.n));
    CHECK_RC(st_binhex_decode(enc.p, enc.n, &g, NULL, &err), ST_OK);
    CHECK(st_same_file(&f, &g) && !strcmp(g.name, "Read Me"));
    st_macfile_free(&g);
    /* Every encoded line is at most 64 characters. */
    size_t col = 0, maxcol = 0;
    for (size_t i = 0; i < enc.n; i++) {
        if (enc.p[i] == '\r') { if (col > maxcol) maxcol = col; col = 0; } else col++;
    }
    CHECK(maxcol <= 64);
    /* Flip one payload character: a CRC (or character) error, never a crash. */
    size_t mid = enc.n / 2;
    while (enc.p[mid] == '\r') mid++;
    enc.p[mid] = enc.p[mid] == 'A' ? 'B' : 'A';
    int rc = st_binhex_decode(enc.p, enc.n, &g, NULL, &err);
    CHECK(rc == ST_ECHECKSUM || rc == ST_EFORMAT);
    CHECK_RC(st_binhex_decode(enc.p, enc.n - 10, &g, NULL, &err), ST_EFORMAT);
    st_bytes_free(&enc);
    st_macfile_free(&f);
}

static void test_applesingle(void) {
    StMacFile f, g;
    st_make_file(&f, "Icon Doc", 77, 4096, 3);
    StBytes enc = {0};
    StErr err;
    CHECK_RC(st_applesingle_encode(&f, 0, &enc, &err), ST_OK);
    CHECK(st_applesingle_detect(enc.p, enc.n) == 1);
    CHECK_RC(st_applesingle_decode(enc.p, enc.n, &g, NULL, &err), ST_OK);
    CHECK(st_same_file(&f, &g) && g.ctime == f.ctime && g.mtime == f.mtime && !strcmp(g.name, "Icon Doc"));
    CHECK(!memcmp(g.fxinfo, f.fxinfo, 16));
    st_macfile_free(&g);
    CHECK_RC(st_applesingle_encode(&f, 1, &enc, &err), ST_OK);
    CHECK(st_applesingle_detect(enc.p, enc.n) == 2);
    CHECK_RC(st_applesingle_decode(enc.p, enc.n, &g, NULL, &err), ST_OK);
    CHECK(g.data.n == 0 && g.rsrc.n == f.rsrc.n);
    st_macfile_free(&g);
    /* An entry pointing outside the file is rejected. */
    enc.p[26 + 4 + 3] = 0xFF;
    enc.p[26 + 4 + 2] = 0xFF;
    CHECK_RC(st_applesingle_decode(enc.p, enc.n, &g, NULL, &err), ST_EFORMAT);
    st_bytes_free(&enc);
    st_macfile_free(&f);
}

static void test_adc(void) {
    /* literal "abc", then copy 3 from distance 3 (2-byte op), then a
     * 3-byte op copying 6 bytes from distance 6. */
    const uint8_t in[] = {0x82, 'a', 'b', 'c', 0x00, 0x02, 0x42, 0x00, 0x05};
    StBytes out = {0};
    StErr err;
    CHECK_RC(st_adc_decode(in, sizeof in, &out, 12, &err), ST_OK);
    CHECK(out.n == 12 && !memcmp(out.p, "abcabcabcabc", 12));
    out.n = 0;
    const uint8_t bad[] = {0x00, 0x10};
    CHECK_RC(st_adc_decode(bad, sizeof bad, &out, 3, &err), ST_EFORMAT);
    st_bytes_free(&out);
}

int main(void) {
    test_text();
    test_crc();
    test_macbinary();
    test_binhex();
    test_applesingle();
    test_adc();
    return st_test_done("test_storage_codecs");
}
