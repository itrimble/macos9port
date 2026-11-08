/* Headless checks for the OSA scripting components and JSON Apple Event bridge. */
#include <stdio.h>
#include <string.h>
#include "osa_events.h"
#include "osa_json.h"
#include "osa_bridge.h"
#include "osa.h"
static int fails = 0;
#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)
static void test_four_char_codes(void) {
    CHECK(OSA_CLASS_CORE == 0x636f7265u);
    CHECK(OSA_CLASS_MISC == 0x6d697363u);
    CHECK(OSA_ID_GETD == 0x67657464u);
    CHECK(OSA_ID_SETD == 0x73657464u);
    CHECK(OSA_ID_SHUT == 0x73687574u);
    CHECK(OSA_DESC_TEXT == 0x74657874u);
    CHECK(OSA_DESC_BOOL == 0x626f6f6cu);
}
static void test_getd_construction(void) {
    AppleEvent e;
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    CHECK(e.event_class == OSA_CLASS_CORE && e.event_id == OSA_ID_GETD);
    CHECK(strcmp(e.target, "os999") == 0);
    CHECK(e.property.type == OSA_DESC_TEXT && strcmp(e.property.text, "theme") == 0);
    CHECK(e.value.type == OSA_DESC_NULL);
    CHECK(e.restart == 0);
    char text[OSPI_NAME];
    CHECK(osa_ae_descriptor_text(&e.property, text, sizeof text) == OSA_OK);
    CHECK(strcmp(text, "theme") == 0);
}
static void test_setd_construction(void) {
    AppleEvent e;
    CHECK(osa_ae_init_setd(&e, "os999", "theme", "Platinum") == OSA_OK);
    CHECK(e.event_class == OSA_CLASS_CORE && e.event_id == OSA_ID_SETD);
    CHECK(e.property.type == OSA_DESC_TEXT && strcmp(e.property.text, "theme") == 0);
    CHECK(e.value.type == OSA_DESC_TEXT && strcmp(e.value.text, "Platinum") == 0);
}
static void test_shut_construction(void) {
    AppleEvent e;
    CHECK(osa_ae_init_shut(&e, "os999", 1) == OSA_OK);
    CHECK(e.event_class == OSA_CLASS_MISC && e.event_id == OSA_ID_SHUT);
    CHECK(e.restart == 1);
    CHECK(e.direct.type == OSA_DESC_BOOL && e.direct.number == 1);
    CHECK(osa_ae_init_shut(&e, "os999", 0) == OSA_OK);
    CHECK(e.restart == 0 && e.direct.number == 0);
}
static void test_descriptor_and_error_reply(void) {
    OsaDescriptor d;
    char small[4];
    memset(&d, 0, sizeof d);
    d.type = OSA_DESC_TEXT;
    snprintf(d.text, sizeof d.text, "%s", "theme");
    CHECK(osa_ae_descriptor_text(&d, small, sizeof small) == OSA_ERR_BAD_PARAMETER);
    char fits[OSPI_NAME];
    CHECK(osa_ae_descriptor_text(&d, fits, sizeof fits) == OSA_OK);
    CHECK(strcmp(fits, "theme") == 0);
    d.type = OSA_DESC_BOOL;
    CHECK(osa_ae_descriptor_text(&d, fits, sizeof fits) == OSA_ERR_BAD_PARAMETER);
    d.type = OSA_DESC_TEXT;
    CHECK(osa_ae_descriptor_text(NULL, fits, sizeof fits) == OSA_ERR_BAD_PARAMETER);
    AppleEventReply r;
    osa_ae_error_reply(&r, OSA_ERR_UNKNOWN_PROPERTY, "unknown property");
    CHECK(r.ok == 0 && r.error_code == OSA_ERR_UNKNOWN_PROPERTY);
    CHECK(strcmp(r.message, "unknown property") == 0);
    CHECK(r.result.type == OSA_DESC_NULL);
}
static void test_constructor_validation(void) {
    AppleEvent e;
    CHECK(osa_ae_init_getd(&e, NULL, "theme") == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_ae_init_getd(&e, "os999", NULL) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_ae_init_getd(NULL, "os999", "theme") == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_ae_init_setd(&e, "os999", "theme", NULL) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_ae_init_shut(&e, NULL, 0) == OSA_ERR_BAD_PARAMETER);
    char long_target[64];
    memset(long_target, 'a', sizeof long_target - 1);
    long_target[sizeof long_target - 1] = 0;
    CHECK(osa_ae_init_getd(&e, long_target, "theme") == OSA_ERR_BAD_PARAMETER);
    char long_property[OSPI_NAME + 8];
    memset(long_property, 'p', sizeof long_property - 1);
    long_property[sizeof long_property - 1] = 0;
    CHECK(osa_ae_init_setd(&e, "os999", long_property, "x") == OSA_ERR_BAD_PARAMETER);
}
static void test_json_escape(void) {
    char buf[64];
    CHECK(osa_json_escape("Platinum", buf, sizeof buf) == OSA_OK);
    CHECK(strcmp(buf, "\"Platinum\"") == 0);
    CHECK(osa_json_escape("say \"hi\"", buf, sizeof buf) == OSA_OK);
    CHECK(strcmp(buf, "\"say \\\"hi\\\"\"") == 0);
    CHECK(osa_json_escape("back\\slash", buf, sizeof buf) == OSA_OK);
    CHECK(strcmp(buf, "\"back\\\\slash\"") == 0);
    CHECK(osa_json_escape("line\nbreak\ttab", buf, sizeof buf) == OSA_OK);
    CHECK(strcmp(buf, "\"line\\u000abreak\\u0009tab\"") == 0);
    CHECK(osa_json_escape("x", buf, 3) == OSA_ERR_BAD_PARAMETER); /* "x" needs 4 bytes */
    CHECK(osa_json_escape(NULL, buf, sizeof buf) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_escape("x", NULL, 4) == OSA_ERR_BAD_PARAMETER);
}
static void test_json_find_string(void) {
    char out[64];
    const char *doc = "{\"class\":\"core\",\"event\":\"getd\",\"target\":\"os999\"}";
    CHECK(osa_json_find_string(doc, "event", out, sizeof out) == OSA_OK);
    CHECK(strcmp(out, "getd") == 0);
    CHECK(osa_json_find_string(doc, "class", out, sizeof out) == OSA_OK);
    CHECK(strcmp(out, "core") == 0);
    CHECK(osa_json_find_string(doc, "missing", out, sizeof out) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_string("{\"a\":\"unterminated}", "a", out, sizeof out) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_string("not an object", "a", out, sizeof out) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_string("{\"v\":\"0123456789012345678901234567890123456789012345678901234567890123\"}", "v", out, sizeof out) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_string("{\"esc\":\"quote \\\" and \\\\ end\"}", "esc", out, sizeof out) == OSA_OK);
    CHECK(strcmp(out, "quote \" and \\ end") == 0);
    CHECK(osa_json_find_string("{\"restart\":false,\"x\":\"y\"}", "x", out, sizeof out) == OSA_OK);
    CHECK(strcmp(out, "y") == 0);
    CHECK(osa_json_find_string(NULL, "a", out, sizeof out) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_string(doc, NULL, out, sizeof out) == OSA_ERR_BAD_PARAMETER);
}
static void test_json_find_bool(void) {
    int v = -1;
    CHECK(osa_json_find_bool("{\"restart\":false}", "restart", &v) == OSA_OK && v == 0);
    CHECK(osa_json_find_bool("{\"restart\":true}", "restart", &v) == OSA_OK && v == 1);
    CHECK(osa_json_find_bool("{\"restart\":\"yes\"}", "restart", &v) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_bool("{\"a\":1}", "missing", &v) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_find_bool("{\"a\":true}", "a", NULL) == OSA_ERR_BAD_PARAMETER);
}
static void test_bridge_getd_translation(void) {
    AppleEvent e;
    OsaBridgeRequest q;
    char json[512];
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_request_to_json(&q, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"class\":\"core\",\"event\":\"getd\",\"target\":\"os999\",\"property\":\"theme\"}") == 0);
    OsaBridgeRequest back;
    CHECK(osa_json_to_request(json, &back) == OSA_OK);
    CHECK(back.event_class == OSA_CLASS_CORE && back.event_id == OSA_ID_GETD);
    CHECK(strcmp(back.target, "os999") == 0 && strcmp(back.property, "theme") == 0);
    char round[512];
    CHECK(osa_request_to_json(&back, round, sizeof round) == OSA_OK);
    CHECK(strcmp(round, json) == 0);
}
static void test_bridge_setd_translation(void) {
    AppleEvent e;
    OsaBridgeRequest q;
    char json[512];
    CHECK(osa_ae_init_setd(&e, "os999", "theme", "Platinum") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_request_to_json(&q, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"class\":\"core\",\"event\":\"setd\",\"target\":\"os999\",\"property\":\"theme\",\"value\":\"Platinum\"}") == 0);
    OsaBridgeRequest back;
    CHECK(osa_json_to_request(json, &back) == OSA_OK);
    CHECK(back.event_class == OSA_CLASS_CORE && back.event_id == OSA_ID_SETD);
    CHECK(strcmp(back.property, "theme") == 0 && strcmp(back.value, "Platinum") == 0);
    char round[512];
    CHECK(osa_request_to_json(&back, round, sizeof round) == OSA_OK);
    CHECK(strcmp(round, json) == 0);
}
static void test_bridge_shut_translation(void) {
    AppleEvent e;
    OsaBridgeRequest q;
    char json[512];
    CHECK(osa_ae_init_shut(&e, "os999", 1) == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_request_to_json(&q, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"class\":\"misc\",\"event\":\"shut\",\"target\":\"os999\",\"restart\":true}") == 0);
    CHECK(q.restart == 1);
    CHECK(osa_ae_init_shut(&e, "os999", 0) == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_request_to_json(&q, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"class\":\"misc\",\"event\":\"shut\",\"target\":\"os999\",\"restart\":false}") == 0);
    OsaBridgeRequest back;
    char round[512];
    CHECK(osa_json_to_request(json, &back) == OSA_OK);
    CHECK(back.event_class == OSA_CLASS_MISC && back.event_id == OSA_ID_SHUT && back.restart == 0);
    CHECK(osa_request_to_json(&back, round, sizeof round) == OSA_OK);
    CHECK(strcmp(round, json) == 0);
}
static void test_bridge_unknown_event(void) {
    AppleEvent e;
    OsaBridgeRequest q;
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    e.event_id = OSA_4CC('b', 'a', 'd', ' ');
    CHECK(osa_ae_to_request(&e, &q) == OSA_ERR_UNKNOWN_EVENT);
    e.event_id = OSA_ID_GETD;
    e.event_class = OSA_4CC('z', 'z', 'z', 'z');
    CHECK(osa_ae_to_request(&e, &q) == OSA_ERR_UNKNOWN_EVENT);
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    e.property.type = OSA_DESC_NULL;
    e.property.text[0] = 0;
    CHECK(osa_ae_to_request(&e, &q) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_to_request("{\"class\":\"core\",\"event\":\"nope\",\"target\":\"os999\"}", &q) == OSA_ERR_UNKNOWN_EVENT);
    CHECK(osa_json_to_request("{\"class\":\"zzzz\",\"event\":\"getd\",\"target\":\"os999\",\"property\":\"theme\"}", &q) == OSA_ERR_UNKNOWN_EVENT);
    CHECK(osa_json_to_request("not json", &q) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_json_to_request("{\"class\":\"core\",\"event\":\"getd\"}", &q) == OSA_ERR_BAD_PARAMETER);
}
static void test_bridge_reply_json(void) {
    OsaBridgeReply r;
    char json[512];
    memset(&r, 0, sizeof r);
    r.ok = 1;
    snprintf(r.value, sizeof r.value, "%s", "Platinum");
    CHECK(osa_reply_to_json(&r, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"ok\":true,\"value\":\"Platinum\"}") == 0);
    OsaBridgeReply back;
    CHECK(osa_json_to_reply(json, &back) == OSA_OK);
    CHECK(back.ok == 1 && strcmp(back.value, "Platinum") == 0);
    r.ok = 0;
    r.value[0] = 0;
    snprintf(r.error, sizeof r.error, "%s", "unknown property");
    CHECK(osa_reply_to_json(&r, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"ok\":false,\"error\":\"unknown property\"}") == 0);
    CHECK(osa_json_to_reply(json, &back) == OSA_OK);
    CHECK(back.ok == 0 && strcmp(back.error, "unknown property") == 0);
    r.ok = 1;
    r.error[0] = 0;
    CHECK(osa_reply_to_json(&r, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"ok\":true}") == 0);
    r.ok = 0;
    snprintf(r.error, sizeof r.error, "%s", "say \"no\"");
    CHECK(osa_reply_to_json(&r, json, sizeof json) == OSA_OK);
    CHECK(strcmp(json, "{\"ok\":false,\"error\":\"say \\\"no\\\"\"}") == 0);
}
static void test_bridge_ae_reply_mapping(void) {
    OsaBridgeReply br;
    AppleEventReply ae;
    char text[OSPI_NAME];
    memset(&br, 0, sizeof br);
    br.ok = 1;
    snprintf(br.value, sizeof br.value, "%s", "Platinum");
    osa_request_to_ae_reply(&br, &ae);
    CHECK(ae.ok == 1 && ae.error_code == OSA_OK);
    CHECK(osa_ae_descriptor_text(&ae.result, text, sizeof text) == OSA_OK);
    CHECK(strcmp(text, "Platinum") == 0);
    br.ok = 0;
    br.value[0] = 0;
    br.code = OSA_ERR_UNKNOWN_PROPERTY;
    snprintf(br.error, sizeof br.error, "%s", "unknown property");
    osa_request_to_ae_reply(&br, &ae);
    CHECK(ae.ok == 0 && ae.error_code == OSA_ERR_UNKNOWN_PROPERTY);
    CHECK(strcmp(ae.message, "unknown property") == 0);
}
static char fake_last_property[OSPI_NAME];
static int fake_system_restart = -1;
static int fake_get(void *ctx, const char *property, char *out, size_t out_size) {
    (void)ctx;
    snprintf(fake_last_property, sizeof fake_last_property, "%s", property);
    if (strcmp(property, "theme") != 0) return -1;
    snprintf(out, out_size, "%s", "Platinum");
    return 0;
}
static int fake_set(void *ctx, const char *property, const char *value, char *err, size_t err_size) {
    (void)ctx;
    snprintf(err, err_size, "%s/%s", property, value);
    return strcmp(property, "theme") == 0 ? 0 : -1;
}
static int fake_system(void *ctx, int restart, char *err, size_t err_size) {
    (void)ctx;
    (void)err;
    (void)err_size;
    fake_system_restart = restart;
    return 0;
}
static void test_dispatch_getd(void) {
    OsaBridgeBackend backend = { fake_get, fake_set, fake_system, NULL };
    OsaBridgeRequest q;
    OsaBridgeReply r;
    AppleEvent e;
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_OK);
    CHECK(r.ok == 1 && strcmp(r.value, "Platinum") == 0);
    CHECK(strcmp(fake_last_property, "theme") == 0);
    CHECK(osa_ae_init_getd(&e, "os999", "bogus") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_OK);
    CHECK(r.ok == 0 && r.code == OSA_ERR_UNKNOWN_PROPERTY);
    CHECK(strcmp(r.error, "unknown property") == 0);
    OsaBridgeBackend no_get = { NULL, fake_set, fake_system, NULL };
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &no_get, &r) == OSA_OK);
    CHECK(r.ok == 0 && r.code == OSA_ERR_HANDLER_FAILED);
}
static void test_dispatch_setd(void) {
    OsaBridgeBackend backend = { fake_get, fake_set, fake_system, NULL };
    OsaBridgeRequest q;
    OsaBridgeReply r;
    AppleEvent e;
    CHECK(osa_ae_init_setd(&e, "os999", "theme", "Platinum") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_OK);
    CHECK(r.ok == 1 && r.value[0] == 0);
    CHECK(osa_ae_init_setd(&e, "os999", "wallpaper", "Blue") == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_OK);
    CHECK(r.ok == 0 && r.code == OSA_ERR_SET_FAILED);
    CHECK(strcmp(r.error, "set failed") == 0);
}
static void test_dispatch_shut_and_unknown(void) {
    OsaBridgeBackend backend = { fake_get, fake_set, fake_system, NULL };
    OsaBridgeRequest q;
    OsaBridgeReply r;
    fake_system_restart = -1;
    AppleEvent e;
    CHECK(osa_ae_init_shut(&e, "os999", 1) == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_OK);
    CHECK(r.ok == 1 && fake_system_restart == 1);
    CHECK(osa_ae_init_shut(&e, "os999", 0) == OSA_OK);
    CHECK(osa_ae_to_request(&e, &q) == OSA_OK);
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_OK);
    CHECK(r.ok == 1 && fake_system_restart == 0);
    q.event_id = OSA_4CC('z', 'z', 'z', 'z');
    CHECK(osa_bridge_dispatch(&q, &backend, &r) == OSA_ERR_UNKNOWN_EVENT);
    CHECK(r.ok == 0 && r.code == OSA_ERR_UNKNOWN_EVENT);
    CHECK(strcmp(r.error, "unsupported event") == 0);
    CHECK(osa_bridge_dispatch(NULL, &backend, &r) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_bridge_dispatch(&q, NULL, &r) == OSA_ERR_HANDLER_FAILED);
}
static void test_ae_send_pipeline(void) {
    OsaBridgeBackend backend = { fake_get, fake_set, fake_system, NULL };
    AppleEvent e;
    AppleEventReply reply;
    char text[OSPI_NAME];
    CHECK(osa_ae_init_getd(&e, "os999", "theme") == OSA_OK);
    CHECK(osa_ae_send(&e, &backend, &reply) == OSA_OK);
    CHECK(reply.ok == 1 && reply.error_code == OSA_OK);
    CHECK(osa_ae_descriptor_text(&reply.result, text, sizeof text) == OSA_OK);
    CHECK(strcmp(text, "Platinum") == 0);
    CHECK(osa_ae_init_getd(&e, "os999", "bogus") == OSA_OK);
    CHECK(osa_ae_send(&e, &backend, &reply) == OSA_OK);
    CHECK(reply.ok == 0 && reply.error_code == OSA_ERR_UNKNOWN_PROPERTY);
    e.event_id = OSA_4CC('q', 'q', 'q', 'q');
    CHECK(osa_ae_send(&e, &backend, &reply) == OSA_ERR_UNKNOWN_EVENT);
    CHECK(reply.ok == 0 && reply.error_code == OSA_ERR_UNKNOWN_EVENT);
    CHECK(strcmp(reply.message, "unsupported event") == 0);
}
static void test_component_registry(void) {
    static OsaComponent fake = { OSA_4CC('f', 'a', 'k', 'e'), "Fake", NULL, NULL };
    CHECK(osa_component_register(&fake) == 0);
    CHECK(osa_component_find(OSA_4CC('f', 'a', 'k', 'e')) == &fake);
    CHECK(osa_component_find(OSA_4CC('n', 'o', 'p', 'e')) == NULL);
    CHECK(osa_component_register(&fake) == -1); /* duplicate type */
    CHECK(osa_component_register(NULL) == -1);
    OsaComponent zero = { 0, "Zero", NULL, NULL };
    CHECK(osa_component_register(&zero) == -1);
    int base = osa_component_count();
    CHECK(base >= 1);
    static OsaComponent fillers[OSA_MAX_COMPONENTS];
    for (int i = base; i < OSA_MAX_COMPONENTS; i++) {
        fillers[i] = (OsaComponent){ OSA_4CC('f', 'i', 'l', '0') + (uint32_t)i, "Filler", NULL, NULL };
        CHECK(osa_component_register(&fillers[i]) == 0);
    }
    CHECK(osa_component_count() == OSA_MAX_COMPONENTS);
    OsaComponent overflow = { OSA_4CC('o', 'v', 'e', 'r'), "Overflow", NULL, NULL };
    CHECK(osa_component_register(&overflow) == -1);
}
static void test_script_compile(void) {
    OsaScript script;
    char error[192];
    CHECK(osa_script_compile(&script, "get theme", error, sizeof error) == OSA_OK);
    CHECK(script.count == 1);
    CHECK(script.events[0].event_class == OSA_CLASS_CORE && script.events[0].event_id == OSA_ID_GETD);
    CHECK(strcmp(script.events[0].property.text, "theme") == 0);
    CHECK(script.lines[0] == 1);
    CHECK(osa_script_compile(&script, "-- comment\n\nset theme to \"Platinum\"", error, sizeof error) == OSA_OK);
    CHECK(script.count == 1);
    CHECK(script.events[0].event_id == OSA_ID_SETD);
    CHECK(strcmp(script.events[0].property.text, "theme") == 0);
    CHECK(strcmp(script.events[0].value.text, "Platinum") == 0);
    CHECK(script.lines[0] == 3);
    CHECK(osa_script_compile(&script, "shut down\nrestart", error, sizeof error) == OSA_OK);
    CHECK(script.count == 2);
    CHECK(script.events[0].event_id == OSA_ID_SHUT && script.events[0].restart == 0);
    CHECK(script.events[1].event_id == OSA_ID_SHUT && script.events[1].restart == 1);
    const char *multi = "get theme\nget wallpaper\nset theme to \"Grape\"\nget clipboard\nrestart";
    CHECK(osa_script_compile(&script, multi, error, sizeof error) == OSA_OK);
    CHECK(script.count == 5);
    CHECK(script.events[2].event_id == OSA_ID_SETD && strcmp(script.events[2].value.text, "Grape") == 0);
    CHECK(script.events[4].event_id == OSA_ID_SHUT && script.events[4].restart == 1);
    CHECK(osa_script_compile(&script, "  get   theme  ", error, sizeof error) == OSA_OK);
    CHECK(script.count == 1 && strcmp(script.events[0].property.text, "theme") == 0);
}
static void test_script_compile_errors(void) {
    OsaScript script;
    char error[192];
    CHECK(osa_script_compile(&script, "frobnicate theme", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(strncmp(error, "line 1:", 7) == 0);
    CHECK(osa_script_compile(&script, "get theme\nbogus verb here", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(strncmp(error, "line 2:", 7) == 0);
    CHECK(osa_script_compile(&script, "set theme to \"open", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(strncmp(error, "line 1:", 7) == 0);
    CHECK(osa_script_compile(&script, "set theme to unquoted", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_script_compile(&script, "get", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_script_compile(&script, "get ", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_script_compile(NULL, "get theme", error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    CHECK(osa_script_compile(&script, NULL, error, sizeof error) == OSA_ERR_BAD_PARAMETER);
    char big[4096];
    size_t at = 0;
    for (int i = 0; i < 20 && at + 11 < sizeof big; i++) {
        memcpy(big + at, "get theme\n", 10);
        at += 10;
    }
    big[at] = 0;
    CHECK(osa_script_compile(&script, big, error, sizeof error) == OSA_ERR_FULL);
}
static void test_script_execute(void) {
    OsaBridgeBackend backend = { fake_get, fake_set, fake_system, NULL };
    OsaScript script;
    AppleEventReply last;
    char error[192];
    CHECK(osa_script_compile(&script, "get theme\nset theme to \"Grape\"\nget theme", error, sizeof error) == OSA_OK);
    CHECK(osa_script_execute(&script, &backend, &last, error, sizeof error) == OSA_OK);
    CHECK(last.ok == 1);
    char text[OSPI_NAME];
    CHECK(osa_ae_descriptor_text(&last.result, text, sizeof text) == OSA_OK);
    CHECK(strcmp(text, "Platinum") == 0); /* fake_get always answers Platinum */
    CHECK(osa_script_compile(&script, "-- setup\nset wallpaper to \"X\"\nget theme", error, sizeof error) == OSA_OK);
    CHECK(osa_script_execute(&script, &backend, &last, error, sizeof error) == OSA_ERR_SET_FAILED);
    CHECK(strstr(error, "line 2") != NULL);
    OsaScript empty = {{0}, {0}, 0};
    CHECK(osa_script_execute(&empty, &backend, &last, error, sizeof error) == OSA_OK);
    CHECK(osa_script_execute(&script, NULL, &last, error, sizeof error) == OSA_ERR_HANDLER_FAILED);
}
static void test_builtin_component_registered(void) {
    const OsaComponent *component = osa_component_find(OSA_TYPE_APPLESCRIPT);
    CHECK(component != NULL);
    CHECK(component->name != NULL && component->name[0] != 0);
    CHECK(component->compile == osa_script_compile);
    CHECK(component->execute == osa_script_execute);
    OsaScript script;
    char error[192];
    CHECK(component->compile(&script, "get theme", error, sizeof error) == OSA_OK);
    CHECK(script.count == 1);
}
int main(void) {
    test_four_char_codes();
    test_getd_construction();
    test_setd_construction();
    test_shut_construction();
    test_descriptor_and_error_reply();
    test_constructor_validation();
    test_json_escape();
    test_json_find_string();
    test_json_find_bool();
    test_bridge_getd_translation();
    test_bridge_setd_translation();
    test_bridge_shut_translation();
    test_bridge_unknown_event();
    test_bridge_reply_json();
    test_bridge_ae_reply_mapping();
    test_dispatch_getd();
    test_dispatch_setd();
    test_dispatch_shut_and_unknown();
    test_ae_send_pipeline();
    test_component_registry();
    test_script_compile();
    test_script_compile_errors();
    test_script_execute();
    test_builtin_component_registered();
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
