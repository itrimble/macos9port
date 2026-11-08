/* Translation between Apple Event records and flat JSON bridge objects. */
#include "osa_bridge.h"

#include <stdio.h>
#include <string.h>

#include "osa_json.h"

static const struct {
    uint32_t event_class;
    uint32_t event_id;
    const char *suite;
    const char *event;
} k_supported_events[] = {
    { OSA_CLASS_CORE, OSA_ID_GETD, "core", "getd" },
    { OSA_CLASS_CORE, OSA_ID_SETD, "core", "setd" },
    { OSA_CLASS_MISC, OSA_ID_SHUT, "misc", "shut" },
};

static int lookup_event(uint32_t event_class, uint32_t event_id,
                        const char **suite, const char **event) {
    for (size_t i = 0; i < sizeof k_supported_events / sizeof k_supported_events[0]; i++) {
        if (k_supported_events[i].event_class == event_class &&
            k_supported_events[i].event_id == event_id) {
            if (suite) *suite = k_supported_events[i].suite;
            if (event) *event = k_supported_events[i].event;
            return OSA_OK;
        }
    }
    return OSA_ERR_UNKNOWN_EVENT;
}

static uint32_t suite_code(const char *suite) {
    if (suite && strcmp(suite, "core") == 0) return OSA_CLASS_CORE;
    if (suite && strcmp(suite, "misc") == 0) return OSA_CLASS_MISC;
    return 0;
}

static uint32_t event_code(const char *event) {
    if (event && strcmp(event, "getd") == 0) return OSA_ID_GETD;
    if (event && strcmp(event, "setd") == 0) return OSA_ID_SETD;
    if (event && strcmp(event, "shut") == 0) return OSA_ID_SHUT;
    return 0;
}

static int copy_bounded(char *out, size_t n, const char *in) {
    if (!in || !out || n == 0) return OSA_ERR_BAD_PARAMETER;
    size_t len = strnlen(in, n);
    if (len >= n) return OSA_ERR_BAD_PARAMETER;
    memcpy(out, in, len + 1);
    return OSA_OK;
}

int osa_ae_to_request(const AppleEvent *e, OsaBridgeRequest *q) {
    if (!e || !q) return OSA_ERR_BAD_PARAMETER;
    memset(q, 0, sizeof *q);
    if (lookup_event(e->event_class, e->event_id, NULL, NULL)) return OSA_ERR_UNKNOWN_EVENT;
    if (e->event_id == OSA_ID_GETD || e->event_id == OSA_ID_SETD) {
        if (e->property.type != OSA_DESC_TEXT || !e->property.text[0]) return OSA_ERR_BAD_PARAMETER;
        if (e->event_id == OSA_ID_SETD &&
            (e->value.type != OSA_DESC_TEXT || !e->value.text[0])) {
            return OSA_ERR_BAD_PARAMETER;
        }
    }
    q->event_class = e->event_class;
    q->event_id = e->event_id;
    q->restart = e->restart ? 1 : 0;
    int err = copy_bounded(q->target, sizeof q->target, e->target[0] ? e->target : "os999");
    if (!err && e->event_id != OSA_ID_SHUT) {
        err = copy_bounded(q->property, sizeof q->property, e->property.text);
        if (!err && e->event_id == OSA_ID_SETD)
            err = copy_bounded(q->value, sizeof q->value, e->value.text);
    }
    if (err) {
        memset(q, 0, sizeof *q);
        return err;
    }
    return OSA_OK;
}

static int valid_request(const OsaBridgeRequest *q) {
    if (lookup_event(q->event_class, q->event_id, NULL, NULL)) return OSA_ERR_UNKNOWN_EVENT;
    if (!memchr(q->target, 0, sizeof q->target) ||
        !memchr(q->property, 0, sizeof q->property) ||
        !memchr(q->value, 0, sizeof q->value) ||
        (q->target[0] && strcmp(q->target, "os999")) ||
        (q->restart != 0 && q->restart != 1)) return OSA_ERR_BAD_PARAMETER;
    if (q->event_id != OSA_ID_SHUT && !q->property[0]) return OSA_ERR_BAD_PARAMETER;
    if (q->event_id == OSA_ID_SETD && !q->value[0]) return OSA_ERR_BAD_PARAMETER;
    return OSA_OK;
}

int osa_request_to_json(const OsaBridgeRequest *q, char *json, size_t n) {
    if (!q || !json || n == 0) return OSA_ERR_BAD_PARAMETER;
    int validation = valid_request(q);
    if (validation) return validation;
    const char *suite, *event;
    if (lookup_event(q->event_class, q->event_id, &suite, &event)) return OSA_ERR_UNKNOWN_EVENT;
    char target[128], property[OSPI_NAME * 6 + 4], value[OSPI_NAME * 6 + 4];
    int err = osa_json_escape(q->target[0] ? q->target : "os999", target, sizeof target);
    if (err) return err;
    if (q->event_id == OSA_ID_GETD) {
        err = osa_json_escape(q->property, property, sizeof property);
        if (err) return err;
        int need = snprintf(json, n, "{\"class\":\"%s\",\"event\":\"%s\",\"target\":%s,\"property\":%s}",
                            suite, event, target, property);
        if (need < 0 || (size_t)need >= n) return OSA_ERR_BAD_PARAMETER;
        return OSA_OK;
    }
    if (q->event_id == OSA_ID_SETD) {
        err = osa_json_escape(q->property, property, sizeof property);
        if (!err) err = osa_json_escape(q->value, value, sizeof value);
        if (err) return err;
        int need = snprintf(json, n,
                            "{\"class\":\"%s\",\"event\":\"%s\",\"target\":%s,\"property\":%s,\"value\":%s}",
                            suite, event, target, property, value);
        if (need < 0 || (size_t)need >= n) return OSA_ERR_BAD_PARAMETER;
        return OSA_OK;
    }
    int need = snprintf(json, n, "{\"class\":\"%s\",\"event\":\"%s\",\"target\":%s,\"restart\":%s}",
                        suite, event, target, q->restart ? "true" : "false");
    if (need < 0 || (size_t)need >= n) return OSA_ERR_BAD_PARAMETER;
    return OSA_OK;
}

int osa_json_to_request(const char *json, OsaBridgeRequest *q) {
    if (!json || !q) return OSA_ERR_BAD_PARAMETER;
    memset(q, 0, sizeof *q);
    char suite[16], event[16];
    if (osa_json_find_string(json, "class", suite, sizeof suite)) return OSA_ERR_BAD_PARAMETER;
    if (osa_json_find_string(json, "event", event, sizeof event)) return OSA_ERR_BAD_PARAMETER;
    uint32_t event_class = suite_code(suite);
    uint32_t event_id = event_code(event);
    if (lookup_event(event_class, event_id, NULL, NULL)) return OSA_ERR_UNKNOWN_EVENT;
    int present = osa_json_member_present(json, "target");
    if (present < 0 || (present && osa_json_find_string(json, "target", q->target, sizeof q->target)))
        return OSA_ERR_BAD_PARAMETER;
    if (!present) snprintf(q->target, sizeof q->target, "%s", "os999");
    q->event_class = event_class;
    q->event_id = event_id;
    if (event_id == OSA_ID_GETD || event_id == OSA_ID_SETD) {
        if (osa_json_find_string(json, "property", q->property, sizeof q->property))
            return OSA_ERR_BAD_PARAMETER;
        if (event_id == OSA_ID_SETD &&
            osa_json_find_string(json, "value", q->value, sizeof q->value))
            return OSA_ERR_BAD_PARAMETER;
    }
    if (event_id == OSA_ID_SHUT) {
        int restart = 0;
        present = osa_json_member_present(json, "restart");
        if (present < 0 || (present && osa_json_find_bool(json, "restart", &restart)))
            return OSA_ERR_BAD_PARAMETER;
        q->restart = restart;
    }
    return valid_request(q);
}

int osa_reply_to_json(const OsaBridgeReply *r, char *json, size_t n) {
    if (!r || !json || n == 0) return OSA_ERR_BAD_PARAMETER;
    if (!memchr(r->value, 0, sizeof r->value) || !memchr(r->error, 0, sizeof r->error)) return OSA_ERR_BAD_PARAMETER;
    char value[OSPI_NAME * 6 + 4], error[192 * 6 + 4];
    if (r->ok) {
        if (!r->value[0]) {
            int need = snprintf(json, n, "{\"ok\":true}");
            if (need < 0 || (size_t)need >= n) return OSA_ERR_BAD_PARAMETER;
            return OSA_OK;
        }
        int err = osa_json_escape(r->value, value, sizeof value);
        if (err) return err;
        int need = snprintf(json, n, "{\"ok\":true,\"value\":%s}", value);
        if (need < 0 || (size_t)need >= n) return OSA_ERR_BAD_PARAMETER;
        return OSA_OK;
    }
    int err = osa_json_escape(r->error, error, sizeof error);
    if (err) return err;
    int need = snprintf(json, n, "{\"ok\":false,\"error\":%s}", error);
    if (need < 0 || (size_t)need >= n) return OSA_ERR_BAD_PARAMETER;
    return OSA_OK;
}

int osa_json_to_reply(const char *json, OsaBridgeReply *r) {
    if (!json || !r) return OSA_ERR_BAD_PARAMETER;
    memset(r, 0, sizeof *r);
    int ok = 0;
    if (osa_json_find_bool(json, "ok", &ok)) return OSA_ERR_BAD_PARAMETER;
    r->ok = ok;
    if (ok) {
        int present = osa_json_member_present(json, "value");
        if (present < 0 || (present && osa_json_find_string(json, "value", r->value, sizeof r->value)))
            return OSA_ERR_BAD_PARAMETER;
        return OSA_OK;
    }
    int present = osa_json_member_present(json, "error");
    if (present < 0 || (present && osa_json_find_string(json, "error", r->error, sizeof r->error)))
        return OSA_ERR_BAD_PARAMETER;
    return OSA_OK;
}

void osa_request_to_ae_reply(const OsaBridgeReply *r, AppleEventReply *out) {
    if (!r || !out) return;
    memset(out, 0, sizeof *out);
    if (r->ok) {
        out->ok = 1;
        out->error_code = OSA_OK;
        out->result.type = OSA_DESC_TEXT;
        size_t len = strnlen(r->value, sizeof out->result.text);
        if (len >= sizeof out->result.text) len = sizeof out->result.text - 1;
        memcpy(out->result.text, r->value, len);
        out->result.text[len] = 0;
        return;
    }
    out->ok = 0;
    out->error_code = r->code;
    size_t len = strnlen(r->error, sizeof out->message);
    if (len >= sizeof out->message) len = sizeof out->message - 1;
    memcpy(out->message, r->error, len);
    out->message[len] = 0;
}

static void reply_fail(OsaBridgeReply *r, long code, const char *message) {
    memset(r, 0, sizeof *r);
    r->ok = 0;
    r->code = code;
    snprintf(r->error, sizeof r->error, "%s", message);
}

int osa_bridge_dispatch(const OsaBridgeRequest *q, const OsaBridgeBackend *backend, OsaBridgeReply *r) {
    if (!q || !r) return OSA_ERR_BAD_PARAMETER;
    memset(r, 0, sizeof *r);
    if (!backend) {
        reply_fail(r, OSA_ERR_HANDLER_FAILED, "no bridge backend");
        return OSA_ERR_HANDLER_FAILED;
    }
    int validation = valid_request(q);
    if (validation) {
        reply_fail(r, validation, validation == OSA_ERR_UNKNOWN_EVENT ? "unsupported event" : "invalid request");
        return validation;
    }
    char scratch[192] = {0};
    if (q->event_id == OSA_ID_GETD) {
        if (!backend->get) {
            reply_fail(r, OSA_ERR_HANDLER_FAILED, "no get handler");
            return OSA_OK;
        }
        if (backend->get(backend->ctx, q->property, r->value, sizeof r->value) != 0 ||
            !memchr(r->value, 0, sizeof r->value)) {
            reply_fail(r, OSA_ERR_UNKNOWN_PROPERTY, "unknown property");
            return OSA_OK;
        }
        r->ok = 1;
        r->code = OSA_OK;
        return OSA_OK;
    }
    if (q->event_id == OSA_ID_SETD) {
        if (!backend->set) {
            reply_fail(r, OSA_ERR_HANDLER_FAILED, "no set handler");
            return OSA_OK;
        }
        if (backend->set(backend->ctx, q->property, q->value, scratch, sizeof scratch) != 0) {
            reply_fail(r, OSA_ERR_SET_FAILED, "set failed");
            return OSA_OK;
        }
        r->ok = 1;
        r->code = OSA_OK;
        return OSA_OK;
    }
    if (q->event_id == OSA_ID_SHUT) {
        if (!backend->system) {
            reply_fail(r, OSA_ERR_HANDLER_FAILED, "no system handler");
            return OSA_OK;
        }
        if (backend->system(backend->ctx, q->restart, scratch, sizeof scratch) != 0) {
            reply_fail(r, OSA_ERR_HANDLER_FAILED, "system action failed");
            return OSA_OK;
        }
        r->ok = 1;
        r->code = OSA_OK;
        return OSA_OK;
    }
    reply_fail(r, OSA_ERR_UNKNOWN_EVENT, "unsupported event");
    return OSA_ERR_UNKNOWN_EVENT;
}

int osa_ae_send(const AppleEvent *e, const OsaBridgeBackend *backend, AppleEventReply *out) {
    if (!e || !out) return OSA_ERR_BAD_PARAMETER;
    OsaBridgeRequest q;
    int err = osa_ae_to_request(e, &q);
    if (err) {
        osa_ae_error_reply(out, err, err == OSA_ERR_UNKNOWN_EVENT ? "unsupported event" : "bad event");
        return err;
    }
    OsaBridgeReply r;
    err = osa_bridge_dispatch(&q, backend, &r);
    osa_request_to_ae_reply(&r, out);
    return err;
}
