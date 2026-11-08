/* Bounded JSON codec for flat OSA bridge objects. */
#include "osa_json.h"

#include <stdio.h>
#include <string.h>

#include "osa_events.h"

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int osa_json_escape(const char *in, char *out, size_t n) {
    if (!in || !out || n < 2) return OSA_ERR_BAD_PARAMETER;
    size_t at = 0;
    out[at++] = '"';
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        char piece[7];
        size_t len = 1;
        unsigned char c = *p;
        if (c == '"') {
            piece[0] = '\\';
            piece[1] = '"';
            len = 2;
        } else if (c == '\\') {
            piece[0] = '\\';
            piece[1] = '\\';
            len = 2;
        } else if (c < 0x20) {
            snprintf(piece, sizeof piece, "\\u%04x", c);
            len = 6;
        } else {
            piece[0] = (char)c;
        }
        if (at + len + 2 > n) return OSA_ERR_BAD_PARAMETER;
        memcpy(out + at, piece, len);
        at += len;
    }
    out[at++] = '"';
    out[at] = 0;
    return OSA_OK;
}

/* Parses a JSON string at its opening quote. Decodes into out when given;
 * a decoded value that does not fit is an error. Returns the position after
 * the closing quote, or NULL on malformed input. */
static const char *parse_string(const char *p, char *out, size_t n) {
    if (*p != '"') return NULL;
    p++;
    size_t at = 0;
    while (*p && *p != '"') {
        char ch = *p;
        if ((unsigned char)ch < 0x20) return NULL;
        if (ch == '\\') {
            p++;
            if (!*p) return NULL;
            char e = *p;
            if (e == 'u') {
                int v = 0;
                for (int i = 0; i < 4; i++) {
                    int h = hex_value(p[1]);
                    if (h < 0 || !p[1]) return NULL;
                    v = v * 16 + h;
                    p++;
                }
                if (!v || v > 0x7f) return NULL; /* No lossy Unicode or embedded NUL. */
                ch = (char)v;
            } else if (e == '"' || e == '\\' || e == '/') {
                ch = e;
            } else if (e == 'n') {
                ch = '\n';
            } else if (e == 't') {
                ch = '\t';
            } else if (e == 'r') {
                ch = '\r';
            } else if (e == 'b') {
                ch = '\b';
            } else if (e == 'f') {
                ch = '\f';
            } else {
                return NULL;
            }
        }
        if (out) {
            if (at + 1 >= n) return NULL;
            out[at++] = ch;
        }
        p++;
    }
    if (*p != '"') return NULL;
    if (out) out[at] = 0;
    return p + 1;
}

/* Validate the complete flat object before returning any field. Unknown,
 * duplicate or trailing value tokens cannot hide behind a valid field.
 * Well-formed unknown members are ignored for forward compatibility. */
static const char *find_member(const char *json, const char *key, int *valid) {
    *valid = 0;
    if (!json || !key) return NULL;
    const char *p = skip_ws(json), *found = NULL;
    char keys[16][64]; int count = 0;
    if (*p++ != '{') return NULL;
    p = skip_ws(p);
    if (*p == '}') { *valid = !*skip_ws(p + 1); return NULL; }
    for (;;) {
        if (count == 16) return NULL;
        p = parse_string(p, keys[count], sizeof keys[count]);
        if (!p) return NULL;
        for (int i = 0; i < count; i++) if (!strcmp(keys[i], keys[count])) return NULL;
        p = skip_ws(p);
        if (*p++ != ':') return NULL;
        p = skip_ws(p);
        if (!strcmp(keys[count++], key)) found = p;
        if (*p == '"') p = parse_string(p, NULL, 0);
        else if (!strncmp(p, "true", 4)) p += 4;
        else if (!strncmp(p, "false", 5)) p += 5;
        else return NULL;
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p == '}') { *valid = !*skip_ws(p + 1); return *valid ? found : NULL; }
        if (*p++ != ',') return NULL;
        p = skip_ws(p);
    }
}

int osa_json_member_present(const char *json, const char *key) {
    int valid;
    const char *p = find_member(json, key, &valid);
    return valid ? p != NULL : -1;
}

int osa_json_find_string(const char *json, const char *key, char *out, size_t n) {
    if (!out || n == 0) return OSA_ERR_BAD_PARAMETER;
    out[0] = 0;
    int valid;
    const char *v = find_member(json, key, &valid);
    if (!v || *v != '"') return OSA_ERR_BAD_PARAMETER;
    return parse_string(v, out, n) ? OSA_OK : OSA_ERR_BAD_PARAMETER;
}

int osa_json_find_bool(const char *json, const char *key, int *out) {
    if (!out) return OSA_ERR_BAD_PARAMETER;
    *out = 0;
    int valid;
    const char *v = find_member(json, key, &valid);
    if (!v) return OSA_ERR_BAD_PARAMETER;
    if (!strncmp(v, "true", 4)) {
        *out = 1;
        return OSA_OK;
    }
    if (!strncmp(v, "false", 5)) return OSA_OK;
    return OSA_ERR_BAD_PARAMETER;
}
