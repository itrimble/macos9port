#include "osa.h"
#include "osa_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int calls;
static int get(void *ctx, const char *key, char *out, size_t n) {
    (void)ctx; (void)key; calls++;
    snprintf(out, n, "value"); return 0;
}

int main(void) {
    char out[64], error[192]; int value;
    const char *bad[] = {
        "{\"x\":\"yes\"", "{\"x\":\"yes\"}junk", "{\"x\":\"yes\",}",
        "{\"x\":\"yes\",\"x\":\"no\"}", "{\"x\":\"yes\",\"junk\":bogus}",
        "{\"x\":\"bad\ncontrol\"}", "{\"x\":\"nul\\u0000tail\"}",
        "{\"x\":\"lossy\\u1234\"}", "{\"x\":\"yes\",\"junk\":trueish}"
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        assert(osa_json_find_string(bad[i], "x", out, sizeof out) == OSA_ERR_BAD_PARAMETER);
    assert(osa_json_find_bool("{\"x\":truejunk}", "x", &value) == OSA_ERR_BAD_PARAMETER);
    assert(osa_json_find_string("{\"\\u0078\":\"yes\"}", "x", out, sizeof out) == 0);
    OsaBridgeRequest q; OsaBridgeReply r;
    assert(osa_json_to_request("{\"class\":\"core\",\"event\":\"getd\",\"target\":true,\"property\":\"theme\"}", &q) != 0);
    assert(osa_json_to_request("{\"class\":\"misc\",\"event\":\"shut\",\"restart\":\"no\"}", &q) != 0);
    AppleEvent event;
    assert(osa_ae_init_getd(&event, "os999", "theme") == 0);
    assert(osa_ae_to_request(&event, &q) == 0);
    OsaBridgeBackend backend = {get, NULL, NULL, NULL};
    q.event_class = OSA_CLASS_MISC;
    assert(osa_bridge_dispatch(&q, &backend, &r) != 0 && calls == 0);
    q.event_class = OSA_CLASS_CORE; strcpy(q.target, "other-app");
    assert(osa_bridge_dispatch(&q, &backend, &r) != 0 && calls == 0);
    memset(q.target, 'x', sizeof q.target);
    assert(osa_bridge_dispatch(&q, &backend, &r) != 0 && calls == 0);
    OsaScript script; AppleEventReply reply;
    assert(osa_script_compile(&script, "get theme\nbogus", error, sizeof error) != 0);
    assert(script.count == 0);
    assert(osa_script_compile(&script, "get theme\r\n", error, sizeof error) == 0);
    char full[512] = "";
    for (int i = 0; i < OSA_MAX_STATEMENTS; i++) strcat(full, "get theme\n");
    strcat(full, "\n-- trailing comment\n");
    assert(osa_script_compile(&script, full, error, sizeof error) == 0);
    assert(script.count == OSA_MAX_STATEMENTS);
    script.count++;
    assert(osa_script_execute(&script, &backend, &reply, error, sizeof error) != 0 && calls == 0);
    script.count = -1;
    assert(osa_script_execute(&script, &backend, &reply, error, sizeof error) != 0);
    puts("OSA malformed inputs, dispatch isolation and transactional compilation passed");
    return 0;
}
