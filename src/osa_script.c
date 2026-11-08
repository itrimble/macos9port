/* Built-in AppleScript-style component: a small statement dialect compiled
 * into Apple Events and executed through the JSON bridge. */
#include "osa.h"

#include <stdio.h>
#include <string.h>

/* Trims leading and trailing blanks in place by returning the start and
 * shortening the token. */
static char *trim(char *token, size_t *len) {
    while (*len && (token[0] == ' ' || token[0] == '\t')) {
        token++;
        (*len)--;
    }
    while (*len && (token[*len - 1] == ' ' || token[*len - 1] == '\t' || token[*len - 1] == '\r')) (*len)--;
    return token;
}

static int fail(char *error, size_t error_size, int line, const char *reason, int code) {
    if (error && error_size > 0) snprintf(error, error_size, "line %d: %s", line, reason);
    return code;
}

static int parse_get(char *rest, size_t len, int line, AppleEvent *event, char *error, size_t error_size) {
    rest = trim(rest, &len);
    if (!len || len >= OSPI_NAME) return fail(error, error_size, line, "get needs a property", OSA_ERR_BAD_PARAMETER);
    char property[OSPI_NAME];
    memcpy(property, rest, len);
    property[len] = 0;
    if (strchr(property, '"')) return fail(error, error_size, line, "invalid property", OSA_ERR_BAD_PARAMETER);
    int err = osa_ae_init_getd(event, "os999", property);
    if (err) return fail(error, error_size, line, "invalid get statement", err);
    return OSA_OK;
}

static int parse_set(char *rest, size_t len, int line, AppleEvent *event, char *error, size_t error_size) {
    rest = trim(rest, &len);
    char *separator = strstr(rest, " to ");
    if (!separator) return fail(error, error_size, line, "set needs: set <property> to \"<value>\"", OSA_ERR_BAD_PARAMETER);
    size_t property_len = (size_t)(separator - rest);
    char *value_text = separator + 4;
    size_t value_len = len - property_len - 4;
    value_text = trim(value_text, &value_len);
    if (value_len < 2 || value_text[0] != '"' || value_text[value_len - 1] != '"')
        return fail(error, error_size, line, "value must be quoted", OSA_ERR_BAD_PARAMETER);
    value_len -= 2;
    value_text++;
    if (!property_len || property_len >= OSPI_NAME || !value_len || value_len >= OSPI_NAME)
        return fail(error, error_size, line, "property or value too long", OSA_ERR_BAD_PARAMETER);
    char property[OSPI_NAME], value[OSPI_NAME];
    memcpy(property, rest, property_len);
    property[property_len] = 0;
    memcpy(value, value_text, value_len);
    value[value_len] = 0;
    if (strchr(property, '"') || strchr(value, '"'))
        return fail(error, error_size, line, "quotes are not allowed inside names", OSA_ERR_BAD_PARAMETER);
    int err = osa_ae_init_setd(event, "os999", property, value);
    if (err) return fail(error, error_size, line, "invalid set statement", err);
    return OSA_OK;
}

static int parse_line(char *line, size_t len, int lineno, AppleEvent *event, char *error, size_t error_size) {
    line = trim(line, &len);
    if (!len) return 1; /* blank */
    if (len >= 2 && line[0] == '-' && line[1] == '-') return 1; /* comment */
    if (!strncmp(line, "get", 3) && (len == 3 || line[3] == ' ' || line[3] == '\t'))
        return parse_get(line + 3, len - 3, lineno, event, error, error_size);
    if (!strncmp(line, "set", 3) && (len == 3 || line[3] == ' ' || line[3] == '\t'))
        return parse_set(line + 3, len - 3, lineno, event, error, error_size);
    if (!strncmp(line, "shut down", 9) && len == 9) {
        int err = osa_ae_init_shut(event, "os999", 0);
        if (err) return fail(error, error_size, lineno, "invalid shut down statement", err);
        return OSA_OK;
    }
    if (!strncmp(line, "restart", 7) && len == 7) {
        int err = osa_ae_init_shut(event, "os999", 1);
        if (err) return fail(error, error_size, lineno, "invalid restart statement", err);
        return OSA_OK;
    }
    return fail(error, error_size, lineno, "unknown statement", OSA_ERR_BAD_PARAMETER);
}

static int compile_script(OsaScript *out, const char *source, char *error, size_t error_size) {
    if (!out || !source) return OSA_ERR_BAD_PARAMETER;
    memset(out, 0, sizeof *out);
    if (error && error_size > 0) error[0] = 0;
    char work[OSPI_TEXT];
    size_t used = 0;
    int lineno = 0;
    const char *p = source;
    for (;;) {
        /* Collect one line, bounded by the working buffer. */
        size_t n = 0;
        while (*p && *p != '\n' && used + n + 1 < sizeof work) {
            work[used + n] = *p++;
            n++;
        }
        lineno++;
        if (*p == '\n') p++;
        else if (*p) return fail(error, error_size, lineno, "statement too long", OSA_ERR_BAD_PARAMETER);
        work[used + n] = 0;
        char *line = work + used;
        AppleEvent event;
        int status = parse_line(line, n, lineno, &event, error, error_size);
        if (status < 0) return status;
        if (status == 0) {
            if (out->count >= OSA_MAX_STATEMENTS)
                return fail(error, error_size, lineno, "too many statements", OSA_ERR_FULL);
            out->events[out->count] = event;
            out->lines[out->count] = lineno;
            out->count++;
            used += n + 1;
            if (used >= sizeof work) return fail(error, error_size, lineno, "script too long", OSA_ERR_BAD_PARAMETER);
        }
        if (!*p) break;
    }
    return OSA_OK;
}

int osa_script_compile(OsaScript *out, const char *source, char *error, size_t error_size) {
    if (!out) return OSA_ERR_BAD_PARAMETER;
    memset(out, 0, sizeof *out);
    if (!source) return OSA_ERR_BAD_PARAMETER;
    OsaScript compiled;
    int result = compile_script(&compiled, source, error, error_size);
    if (!result) *out = compiled;
    return result;
}

int osa_script_execute(const OsaScript *script, const OsaBridgeBackend *bridge,
                       AppleEventReply *last, char *error, size_t error_size) {
    if (!script || !last || script->count < 0 || script->count > OSA_MAX_STATEMENTS) return OSA_ERR_BAD_PARAMETER;
    if (error && error_size > 0) error[0] = 0;
    memset(last, 0, sizeof *last);
    for (int i = 0; i < script->count && i < OSA_MAX_STATEMENTS; i++) {
        int err = osa_ae_send(&script->events[i], bridge, last);
        if (err || !last->ok) {
            if (error && error_size > 0)
                snprintf(error, error_size, "line %d: %s", script->lines[i],
                         last->message[0] ? last->message : "execution failed");
            return err ? err : (int)last->error_code;
        }
    }
    return OSA_OK;
}
