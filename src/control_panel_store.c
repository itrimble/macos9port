#include "control_panel_store.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
enum { MAX_JSON_BYTES = 65536 };
static int valid_ident(const char *s, size_t limit) {
    if (!s || !s[0]) return 0;
    size_t i = 0;
    for (; s[i]; i++) {
        if (i + 1 >= limit) return 0;
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') return 0;
    }
    return 1;
}
static int find_entry(const os999_cp_store_t *store, const char *panel_id, const char *key) {
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].panel_id, panel_id) == 0 &&
            strcmp(store->entries[i].key, key) == 0)
            return i;
    }
    return -1;
}

static os999_cp_entry_t *upsert_slot(os999_cp_entry_t *entries, int *count,
                                     const char *panel_id, const char *key) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(entries[i].panel_id, panel_id) == 0 &&
            strcmp(entries[i].key, key) == 0)
            return &entries[i];
    }
    if (*count >= OS999_CP_MAX_ENTRIES) return NULL;
    os999_cp_entry_t *slot = &entries[(*count)++];
    memset(slot, 0, sizeof *slot);
    snprintf(slot->panel_id, sizeof slot->panel_id, "%s", panel_id);
    snprintf(slot->key, sizeof slot->key, "%s", key);
    return slot;
}

int os999_cp_store_init(os999_cp_store_t *store, const char *volume_root) {
    if (!store || !volume_root || !volume_root[0]) return -1;
    size_t root_len = strnlen(volume_root, OSPI_PATH);
    if (root_len >= OSPI_PATH || strstr(volume_root, "..") != NULL) return -1;
    struct stat st;
    if (lstat(volume_root, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    memset(store, 0, sizeof *store);
    if (pthread_mutex_init(&store->lock, NULL) != 0) return -1;
    int n = snprintf(store->path, sizeof store->path, "%s/%s",
                     volume_root, OS999_CP_STORE_FILENAME);
    if (n <= 0 || (size_t)n >= sizeof store->path) {
        pthread_mutex_destroy(&store->lock);
        return -1;
    }
    store->initialized = 1;
    return 0;
}

void os999_cp_store_destroy(os999_cp_store_t *store) {
    if (!store || !store->initialized) return;
    pthread_mutex_destroy(&store->lock);
    memset(store, 0, sizeof *store);
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
}

static int parse_string(const char **p, char *out, size_t out_n) {
    skip_ws(p);
    if (**p != '"' || out_n == 0) return -1;
    (*p)++;
    size_t j = 0;
    while (**p && **p != '"') {
        unsigned char c = (unsigned char)*(*p)++;
        if (c < 0x20) return -1;
        if (c == '\\') {
            unsigned char esc = (unsigned char)*(*p)++;
            if (!esc) return -1;
            switch (esc) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default: return -1;
            }
        }
        if (j + 1 >= out_n) return -1;
        out[j++] = (char)c;
    }
    if (**p != '"') return -1;
    (*p)++;
    out[j] = 0;
    return 0;
}

static int parse_int_token(const char **p, int *out) {
    skip_ws(p);
    const char *start = *p;
    if (*start == '-') (*p)++;
    if (!isdigit((unsigned char)**p)) return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(start, &end, 10);
    if (errno != 0 || end == start || v < INT_MIN || v > INT_MAX) return -1;
    if (*end == '.' || *end == 'e' || *end == 'E') return -1;
    *p = end;
    *out = (int)v;
    return 0;
}

static int parse_panel_object(const char **p, const char *panel_id,
                              os999_cp_entry_t *entries, int *count) {
    skip_ws(p);
    if (**p != '{') return -1;
    (*p)++;
    skip_ws(p);
    if (**p == '}') { (*p)++; return 0; }
    for (;;) {
        char key[OS999_CP_KEY_MAX];
        if (parse_string(p, key, sizeof key) != 0 || !valid_ident(key, sizeof key)) return -1;
        skip_ws(p);
        if (**p != ':') return -1;
        (*p)++;
        skip_ws(p);
        os999_cp_entry_t *slot = upsert_slot(entries, count, panel_id, key);
        if (!slot) return -1;
        if (**p == '"') {
            if (parse_string(p, slot->str_val, sizeof slot->str_val) != 0) return -1;
            slot->kind = OS999_CP_VAL_STR;
            slot->int_val = 0;
        } else if (strncmp(*p, "true", 4) == 0 && !isalnum((unsigned char)(*p)[4])) {
            *p += 4;
            slot->kind = OS999_CP_VAL_BOOL;
            slot->int_val = 1;
            slot->str_val[0] = 0;
        } else if (strncmp(*p, "false", 5) == 0 && !isalnum((unsigned char)(*p)[5])) {
            *p += 5;
            slot->kind = OS999_CP_VAL_BOOL;
            slot->int_val = 0;
            slot->str_val[0] = 0;
        } else {
            int val = 0;
            if (parse_int_token(p, &val) != 0) return -1;
            slot->kind = OS999_CP_VAL_INT;
            slot->int_val = val;
            slot->str_val[0] = 0;
        }
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; return 0; }
        return -1;
    }
}

static int parse_document(const char *json, os999_cp_entry_t *entries, int *count) {
    const char *p = json;
    int saw_version = 0, saw_panels = 0;
    skip_ws(&p);
    if (*p != '{') return -1;
    p++;
    skip_ws(&p);
    if (*p == '}') { p++; skip_ws(&p); return *p == 0 ? 0 : -1; }
    for (;;) {
        char top_key[32];
        if (parse_string(&p, top_key, sizeof top_key) != 0) return -1;
        skip_ws(&p);
        if (*p != ':') return -1;
        p++;
        skip_ws(&p);
        if (strcmp(top_key, "version") == 0) {
            int ver = 0;
            if (parse_int_token(&p, &ver) != 0 || ver != 1) return -1;
            saw_version = 1;
        } else if (strcmp(top_key, "panels") == 0) {
            if (*p != '{') return -1;
            p++;
            skip_ws(&p);
            if (*p == '}') {
                p++;
            } else {
                for (;;) {
                    char pid[OS999_CP_ID_MAX];
                    if (parse_string(&p, pid, sizeof pid) != 0 || !valid_ident(pid, sizeof pid))
                        return -1;
                    skip_ws(&p);
                    if (*p != ':') return -1;
                    p++;
                    if (parse_panel_object(&p, pid, entries, count) != 0) return -1;
                    skip_ws(&p);
                    if (*p == ',') { p++; continue; }
                    if (*p == '}') { p++; break; }
                    return -1;
                }
            }
            saw_panels = 1;
        } else {
            return -1;
        }
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        return -1;
    }
    skip_ws(&p);
    return (*p == 0 && saw_version && saw_panels) ? 0 : -1;
}

int os999_cp_store_load(os999_cp_store_t *store) {
    if (!store || !store->initialized) return -1;
    pthread_mutex_lock(&store->lock);
    struct stat lst;
    if (lstat(store->path, &lst) != 0) {
        int rc = (errno == ENOENT) ? 0 : -1;
        pthread_mutex_unlock(&store->lock);
        return rc;
    }
    if (!S_ISREG(lst.st_mode) || lst.st_size < 0 || lst.st_size > MAX_JSON_BYTES) {
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    int fd = open(store->path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    struct stat live;
    if (fstat(fd, &live) != 0 || !S_ISREG(live.st_mode) ||
        live.st_dev != lst.st_dev || live.st_ino != lst.st_ino || live.st_size != lst.st_size) {
        close(fd); pthread_mutex_unlock(&store->lock); return -1;
    }
    size_t size = (size_t)live.st_size;
    char *buf = malloc(size + 1);
    if (!buf) {
        close(fd);
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    size_t got = 0;
    while (got < size) {
        ssize_t r = read(fd, buf + got, size - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got != size) {
        free(buf);
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    buf[got] = 0;
    os999_cp_entry_t *parsed = calloc(OS999_CP_MAX_ENTRIES, sizeof *parsed);
    int parsed_count = 0;
    int ok = parsed && parse_document(buf, parsed, &parsed_count) == 0;
    free(buf);
    if (ok) {
        memcpy(store->entries, parsed, sizeof store->entries);
        store->count = parsed_count;
    }
    free(parsed);
    pthread_mutex_unlock(&store->lock);
    return ok ? 0 : -1;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int failed;
} JsonBuf;

static void jb_append(JsonBuf *jb, const char *s, size_t n) {
    if (jb->failed) return;
    if (jb->len + n + 1 > jb->cap) {
        size_t next = jb->cap ? jb->cap * 2 : 1024;
        while (next < jb->len + n + 1) next *= 2;
        if (next > MAX_JSON_BYTES) { jb->failed = 1; return; }
        char *grown = realloc(jb->data, next);
        if (!grown) { jb->failed = 1; return; }
        jb->data = grown;
        jb->cap = next;
    }
    memcpy(jb->data + jb->len, s, n);
    jb->len += n;
    jb->data[jb->len] = 0;
}

static void jb_puts(JsonBuf *jb, const char *s) { jb_append(jb, s, strlen(s)); }

static void jb_quoted(JsonBuf *jb, const char *s) {
    jb_puts(jb, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': jb_puts(jb, "\\\""); break;
        case '\\': jb_puts(jb, "\\\\"); break;
        case '\n': jb_puts(jb, "\\n"); break;
        case '\r': jb_puts(jb, "\\r"); break;
        case '\t': jb_puts(jb, "\\t"); break;
        default:
            if (*p >= 0x20) jb_append(jb, (const char *)p, 1);
            break;
        }
    }
    jb_puts(jb, "\"");
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int os999_cp_store_save(os999_cp_store_t *store) {
    if (!store || !store->initialized) return -1;
    pthread_mutex_lock(&store->lock);
    struct stat lst;
    if (lstat(store->path, &lst) == 0 && !S_ISREG(lst.st_mode)) {
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    JsonBuf jb = {0};
    jb_puts(&jb, "{\n  \"version\": 1,\n  \"panels\": {");
    uint8_t emitted[OS999_CP_MAX_ENTRIES] = {0};
    int first_panel = 1;
    for (int i = 0; i < store->count; i++) {
        if (emitted[i]) continue;
        const char *pid = store->entries[i].panel_id;
        jb_puts(&jb, first_panel ? "\n    " : ",\n    ");
        first_panel = 0;
        jb_quoted(&jb, pid);
        jb_puts(&jb, ": {");
        int first_key = 1;
        for (int j = i; j < store->count; j++) {
            if (strcmp(store->entries[j].panel_id, pid) != 0) continue;
            emitted[j] = 1;
            jb_puts(&jb, first_key ? "\n      " : ",\n      ");
            first_key = 0;
            jb_quoted(&jb, store->entries[j].key);
            jb_puts(&jb, ": ");
            if (store->entries[j].kind == OS999_CP_VAL_STR) {
                jb_quoted(&jb, store->entries[j].str_val);
            } else if (store->entries[j].kind == OS999_CP_VAL_BOOL) {
                jb_puts(&jb, store->entries[j].int_val ? "true" : "false");
            } else {
                char num[32];
                snprintf(num, sizeof num, "%d", store->entries[j].int_val);
                jb_puts(&jb, num);
            }
        }
        jb_puts(&jb, "\n    }");
    }
    jb_puts(&jb, first_panel ? "}\n}\n" : "\n  }\n}\n");
    if (jb.failed || !jb.data) {
        free(jb.data);
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    char tmp[OSPI_PATH + 64];
    int tn = snprintf(tmp, sizeof tmp, "%s.XXXXXX", store->path);
    int fd = (tn > 0 && (size_t)tn < sizeof tmp) ? mkstemp(tmp) : -1;
    if (fd < 0) {
        free(jb.data);
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    (void)fchmod(fd, 0644);
    int ok = write_all(fd, jb.data, jb.len) == 0 && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok && rename(tmp, store->path) != 0) ok = 0;
    if (!ok) unlink(tmp);
    free(jb.data);
    pthread_mutex_unlock(&store->lock);
    return ok ? 0 : -1;
}

int os999_cp_store_has(os999_cp_store_t *store, const char *panel_id, const char *key) {
    if (!store || !store->initialized || !panel_id || !key) return 0;
    pthread_mutex_lock(&store->lock);
    int found = find_entry(store, panel_id, key) >= 0;
    pthread_mutex_unlock(&store->lock);
    return found;
}

int os999_cp_store_get_int(os999_cp_store_t *store, const char *panel_id,
                           const char *key, int fallback) {
    if (!store || !store->initialized || !panel_id || !key) return fallback;
    pthread_mutex_lock(&store->lock);
    int idx = find_entry(store, panel_id, key);
    int val = (idx >= 0 && store->entries[idx].kind == OS999_CP_VAL_INT)
                  ? store->entries[idx].int_val
                  : fallback;
    pthread_mutex_unlock(&store->lock);
    return val;
}

int os999_cp_store_set_int(os999_cp_store_t *store, const char *panel_id,
                           const char *key, int value) {
    if (!store || !store->initialized || !valid_ident(panel_id, OS999_CP_ID_MAX) ||
        !valid_ident(key, OS999_CP_KEY_MAX))
        return -1;
    pthread_mutex_lock(&store->lock);
    os999_cp_entry_t *slot = upsert_slot(store->entries, &store->count, panel_id, key);
    if (slot) {
        slot->kind = OS999_CP_VAL_INT;
        slot->int_val = value;
        slot->str_val[0] = 0;
    }
    pthread_mutex_unlock(&store->lock);
    return slot ? 0 : -1;
}

int os999_cp_store_get_bool(os999_cp_store_t *store, const char *panel_id,
                            const char *key, int fallback) {
    if (!store || !store->initialized || !panel_id || !key) return !!fallback;
    pthread_mutex_lock(&store->lock);
    int idx = find_entry(store, panel_id, key);
    int val = (idx >= 0 && store->entries[idx].kind == OS999_CP_VAL_BOOL)
                  ? !!store->entries[idx].int_val
                  : !!fallback;
    pthread_mutex_unlock(&store->lock);
    return val;
}

int os999_cp_store_set_bool(os999_cp_store_t *store, const char *panel_id,
                            const char *key, int value) {
    if (!store || !store->initialized || !valid_ident(panel_id, OS999_CP_ID_MAX) ||
        !valid_ident(key, OS999_CP_KEY_MAX))
        return -1;
    pthread_mutex_lock(&store->lock);
    os999_cp_entry_t *slot = upsert_slot(store->entries, &store->count, panel_id, key);
    if (slot) {
        slot->kind = OS999_CP_VAL_BOOL;
        slot->int_val = !!value;
        slot->str_val[0] = 0;
    }
    pthread_mutex_unlock(&store->lock);
    return slot ? 0 : -1;
}

int os999_cp_store_get_str(os999_cp_store_t *store, const char *panel_id,
                           const char *key, char *out, size_t out_n,
                           const char *fallback) {
    if (!out || out_n == 0) return -1;
    const char *def = fallback ? fallback : "";
    if (!store || !store->initialized || !panel_id || !key) {
        snprintf(out, out_n, "%s", def);
        return 0;
    }
    pthread_mutex_lock(&store->lock);
    int idx = find_entry(store, panel_id, key);
    const char *src = (idx >= 0 && store->entries[idx].kind == OS999_CP_VAL_STR)
                          ? store->entries[idx].str_val
                          : def;
    snprintf(out, out_n, "%s", src);
    pthread_mutex_unlock(&store->lock);
    return 0;
}

int os999_cp_store_set_str(os999_cp_store_t *store, const char *panel_id,
                           const char *key, const char *value) {
    if (!store || !store->initialized || !value ||
        !valid_ident(panel_id, OS999_CP_ID_MAX) || !valid_ident(key, OS999_CP_KEY_MAX))
        return -1;
    size_t len = strnlen(value, OS999_CP_STR_MAX);
    if (len >= OS999_CP_STR_MAX) return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20 && c != '\t' && c != '\n') return -1;
    }
    pthread_mutex_lock(&store->lock);
    os999_cp_entry_t *slot = upsert_slot(store->entries, &store->count, panel_id, key);
    if (slot) {
        slot->kind = OS999_CP_VAL_STR;
        slot->int_val = 0;
        memcpy(slot->str_val, value, len + 1);
    }
    pthread_mutex_unlock(&store->lock);
    return slot ? 0 : -1;
}
