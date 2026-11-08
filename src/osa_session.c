#include "osa_session.h"
#include "desktop_theme.h"
#include <stdio.h>
#include <string.h>

static int get(void *ctx, const char *property, char *out, size_t n) {
    (void)ctx;
    const char *value = NULL;
    if (!strcmp(property, "name")) value = "OS999 Stylized";
    if (!strcmp(property, "version")) value = "OS999 Script 1";
    if (!strcmp(property, "theme")) value = desktop_theme_name(desktop_theme_current());
    if (!value) return -1;
    return snprintf(out, n, "%s", value) < (int)n ? 0 : -1;
}

static int set(void *ctx, const char *property, const char *value, char *err, size_t n) {
    (void)ctx; (void)err; (void)n;
    if (strcmp(property, "theme")) return -1;
    for (int i = 0; i < desktop_theme_count(); i++)
        if (!strcmp(value, desktop_theme_name(i))) return desktop_theme_select(i);
    return -1;
}

static int system_event(void *ctx, int restart, char *err, size_t n) {
    (void)err; (void)n;
    session_command(ctx, restart ? CMD_RESTART_DO : CMD_SHUTDOWN);
    return 0;
}

int osa_session_json(Session *session, const char *request, char *reply, size_t n) {
    if (!session || !request || !reply || n == 0) return OSA_ERR_BAD_PARAMETER;
    OsaBridgeRequest q; OsaBridgeReply r = {0};
    int result = osa_json_to_request(request, &q);
    if (result) {
        r.code = result;
        snprintf(r.error, sizeof r.error, "invalid or unsupported request");
    } else {
        const OsaComponent *component = osa_component_find(OSA_TYPE_APPLESCRIPT);
        if (!component) {
            snprintf(r.error, sizeof r.error, "no scripting component");
            (void)osa_reply_to_json(&r, reply, n);
            return OSA_ERR_HANDLER_FAILED;
        }
        OsaBridgeBackend backend = {get, set, system_event, session};
        result = osa_bridge_dispatch(&q, &backend, &r);
    }
    int encoded = osa_reply_to_json(&r, reply, n);
    return encoded ? encoded : result;
}
