/* Script Manager (ScriptUtil, $A8B5): the Roman-only answers and the refusal of every other selector. */
#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define SP 0x8000u
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static MacRuntime fresh(void) { MacRuntime r; memset(&r, 0, sizeof r); r.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(r.cpu); return r; }
int main(void) {
    MacRuntime r = fresh();
    /* FUNCTION FontScript: INTEGER and IntlScript: the result slot is below the selector; both answer smRoman */
    for (unsigned k = 0; k < 2; k++) {
        wr(&r, SP, 4, k ? 0x82000002u : 0x82000000u); wr(&r, SP + 4, 2, 0x1234);
        mac_cpu_set(r.cpu, MAC_A7, SP);
        CHECK(mac_script_service(&r, 0xa8b5) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4 && rd(&r, SP + 4, 2) == 0);
    }
    /* Font2Script(fontNumber): the argument is popped with the selector */
    wr(&r, SP, 4, 0x82020006u); wr(&r, SP + 4, 2, 3); wr(&r, SP + 6, 2, 0x1234);
    mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_script_service(&r, 0xa8b5) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 6 && rd(&r, SP + 6, 2) == 0);
    CHECK(mac_script_service(&r, 0xa8b6) == 0);                                   /* other traps are not ours */
    /* GetScript(script, verb): LONGINT. smScriptLang (28) of the Roman script is langEnglish (0); the result slot is above the two arguments */
    wr(&r, SP, 4, 0x8404000cu); wr(&r, SP + 4, 2, 28); wr(&r, SP + 6, 2, 0); wr(&r, SP + 8, 4, 0x12345678u);
    mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_script_service(&r, 0xa8b5) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 8 && rd(&r, SP + 8, 4) == 0);
    /* GetEnvirons(verb): LONGINT. smRegionCode (40) is verUS (0) */
    wr(&r, SP, 4, 0x84020008u); wr(&r, SP + 4, 2, 40); wr(&r, SP + 6, 4, 0x12345678u);
    mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_script_service(&r, 0xa8b5) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 6 && rd(&r, SP + 6, 4) == 0);
    wr(&r, SP, 4, 0x84020008u); wr(&r, SP + 4, 2, 4); wr(&r, SP + 6, 4, 0x12345678u); mac_cpu_set(r.cpu, MAC_A7, SP);                       /* smEnabled: one script */
    CHECK(mac_script_service(&r, 0xa8b5) == 1 && rd(&r, SP + 6, 4) == 1);
    wr(&r, SP, 4, 0x84020008u); wr(&r, SP + 4, 2, 36); wr(&r, SP + 6, 4, 0x12345678u); mac_cpu_set(r.cpu, MAC_A7, SP);                      /* smDoubleByte: none */
    CHECK(mac_script_service(&r, 0xa8b5) == 1 && rd(&r, SP + 6, 4) == 0);
    mac_cpu_destroy(r.cpu);
    /* a verb that is not answered halts and names itself and the verb; so does an unknown selector */
    r = fresh();
    wr(&r, SP, 4, 0x84020008u); wr(&r, SP + 4, 2, 24); mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_script_service(&r, 0xa8b5) == -1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "verb 24"));
    mac_cpu_destroy(r.cpu);
    r = fresh();
    wr(&r, SP, 4, 0x8404000cu); wr(&r, SP + 4, 2, 7); wr(&r, SP + 6, 2, 0); mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_script_service(&r, 0xa8b5) == -1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "verb 7"));
    mac_cpu_destroy(r.cpu);
    r = fresh();
    wr(&r, SP, 4, 0x8206000au); mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_script_service(&r, 0xa8b5) == -1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "8206000A"));
    mac_cpu_destroy(r.cpu);
    r = fresh(); mac_cpu_set(r.cpu, MAC_A7, SP + 1);                               /* an odd stack */
    CHECK(mac_script_service(&r, 0xa8b5) == -1 && mac_cpu_halted(r.cpu));
    mac_cpu_destroy(r.cpu);
    if (!fails) puts("Script Manager checks passed");
    return fails ? 1 : 0;
}
