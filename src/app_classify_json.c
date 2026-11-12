#include "app_classify.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { char *out; size_t cap, len; } Sink;

static void put(Sink *s, const char *text, size_t n) {
    for (size_t i = 0; i < n; i++, s->len++)
        if (s->cap && s->len + 1 < s->cap) s->out[s->len] = text[i];
}
static void fmt(Sink *s, const char *f, ...) {
    char tmp[96];
    va_list ap;
    va_start(ap, f);
    int k = vsnprintf(tmp, sizeof tmp, f, ap);
    va_end(ap);
    if (k > 0) put(s, tmp, (size_t)k < sizeof tmp ? (size_t)k : sizeof tmp - 1);
}
static void str(Sink *s, const char *v) {
    put(s, "\"", 1);
    for (; *v; v++) {
        unsigned char ch = (unsigned char)*v;
        if (ch == '"' || ch == '\\') { char e[2] = {'\\', (char)ch}; put(s, e, 2); }
        else if (ch < 0x20 || ch >= 0x7f) { char e[8]; int k = snprintf(e, sizeof e, "\\u%04x", ch); put(s, e, (size_t)k); }
        else put(s, (const char *)v, 1);
    }
    put(s, "\"", 1);
}
static void key_str(Sink *s, const char *k, const char *v) { fmt(s, ",\"%s\":", k); str(s, v); }

size_t app_classification_json(const AppClassification *c, char *out, size_t n) {
    Sink s = {out, n, 0};
    if (!c) { if (n) out[0] = 0; return 0; }
    put(&s, "{\"format\":", 10); str(&s, app_format_name(c->format));
    key_str(&s, "profile", app_profile_name(c->profile));
    key_str(&s, "confidence", app_confidence_name(c->confidence));
    fmt(&s, ",\"alternate\":"); str(&s, c->alternate ? app_profile_name(c->alternate) : "");
    fmt(&s, ",\"arch\":%u,\"runnable\":%d,\"is_application\":%d,\"demonstrated\":false", c->arch, c->runnable, c->is_application);
    fmt(&s, ",\"identity\":\"%016llx\"", (unsigned long long)c->identity);
    fmt(&s, ",\"evidence\":{\"resource_fork_ok\":%d,\"code_resources\":%u,\"code0\":%d,\"code0_layout_ok\":%d", c->resource_fork_ok, c->code_resources, c->has_code0, c->code0_layout_ok);
    fmt(&s, ",\"size\":%d,\"size_flags\":%u,\"size_32bit\":%u,\"size_preferred\":%u,\"size_minimum\":%u", c->has_size, c->size_flags, c->size_declares_32bit, (unsigned)c->size_preferred, (unsigned)c->size_minimum);
    fmt(&s, ",\"cfrg\":%d,\"cfrg_members\":%u,\"carb\":%d", c->has_cfrg, c->cfrg_members, c->has_carb);
    fmt(&s, ",\"pef\":%d,\"pef_main\":%d,\"pef_sections\":%u,\"pef_imports\":%u,\"pef_libs\":[", c->pef_valid, c->pef_has_main, c->pef_sections, c->pef_lib_total);
    for (unsigned i = 0; i < c->pef_lib_total && i < APP_MAX_LIBS; i++) { if (i) put(&s, ",", 1); str(&s, c->pef_libs[i]); }
    fmt(&s, "],\"macho_cputype\":%u}", (unsigned)c->macho_cputype);
    key_str(&s, "reason", c->reason);
    key_str(&s, "uncertainty", c->uncertainty);
    key_str(&s, "missing", c->missing);
    put(&s, "}", 1);
    if (n) out[s.len < n ? s.len : n - 1] = 0;
    return s.len;
}
