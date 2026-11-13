#include "compat_known.h"

/* Add entries only with evidence: the program, run headless on both machines, behaves differently from its headers. */
static const CompatKnown builtin[] = {
    /* S5: sets is32BitCompatible, but halts on the 32-bit machine with a 24-bit-dirty pointer (an ASCII string used as an
     * address) and runs to its idle loop on the 24-bit machine. Headless probe, 12 Nov 2026. */
    {0x6f7c7bb26ad8adc6ull, APP_PROFILE_A_LEGACY_68K, "runs only with 24-bit addressing despite its SIZE flag (24-bit-dirty pointer)"},
};

static const CompatKnown *table = builtin;
static size_t table_count = sizeof builtin / sizeof *builtin;

const CompatKnown *compat_known_lookup(uint64_t identity) {
    for (size_t i = 0; identity && i < table_count; i++)
        if (table[i].identity == identity) return &table[i];
    return NULL;
}

void compat_known_set_table(const CompatKnown *t, size_t count) {
    table = t ? t : builtin;
    table_count = t ? count : sizeof builtin / sizeof *builtin;
}
