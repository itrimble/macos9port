#ifndef OS999_GUEST_MEMORY_H
#define OS999_GUEST_MEMORY_H

/* The one place that decides the guest machine's RAM capacity and addressing mode. Header-only and dependency-free
 * so the desktop, the launcher and the runtime agree. RAM capacity, addressing mode and the application partition
 * (OS999_GUEST_HEAP_MB) are three separate settings.
 *
 * Default (nothing requested, nothing saved): 256 MB, 32-bit addressing (Ian, 12 Nov 2026). The 16 MB, 24-bit machine
 * stays available (--ram-mb 16 --addressing 24, OS999_GUEST_RAM_MB / OS999_GUEST_ADDRESSING, or the saved Memory-panel
 * keys memory/guest_machine_mb and memory/addr_bits), and the launch policy asks for it per program that needs it. Rules:
 *  - nothing requested at all means 256 MB, 32-bit;
 *  - addressing 32 with no RAM size asks for GUEST_RAM_TARGET_MB (256);
 *  - a RAM size above 16 MB with no addressing mode implies 32-bit (24 bits cannot reach it);
 *  - a RAM size of 16 MB or less with no addressing mode is the 24-bit machine;
 *  - addressing 24 can only reach 16 MB, so the machine is 16 MB whatever was asked.
 * Precedence: environment, then saved policy. Invalid values are clamped, never rejected. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GUEST_RAM_DEFAULT_MB 256u /* default machine: 256 MB, 32-bit */
#define GUEST_RAM_TARGET_MB 256u  /* what "32-bit with no size" gets */
#define GUEST_RAM_MIN_MB 16u
#define GUEST_RAM_MAX_MB 1024u   /* host-side ceiling; the 32-bit bus itself could address 4 GB */
#define GUEST_ADDR24_MB 16u      /* 24-bit address space: 2^24 bytes */

typedef struct {
    unsigned ram_mb;  /* final guest RAM in MiB */
    int addr32;       /* 1 = 32-bit addressing, 0 = 24-bit fallback */
    int clamped;      /* nonzero when a requested value was out of range or invalid */
} GuestMemoryConfig;

/* Parse a positive decimal MiB value; 0 on missing or non-numeric text. */
static inline unsigned guest_memory_parse_mb(const char *text) {
    if (!text || !text[0]) return 0;
    char *end;
    unsigned long v = strtoul(text, &end, 10);
    if (*end || end == text) return 0;
    return v > 0xfffffu ? 0xfffffu : (unsigned)v;
}

/* policy_ram_mb (0 = unset) and policy_addr (0 unset, 24, 32) come from saved settings; env_ram / env_addr are the raw
 * environment strings (NULL when unset). */
static inline GuestMemoryConfig guest_memory_resolve(int policy_ram_mb, int policy_addr,
                                                     const char *env_ram, const char *env_addr) {
    GuestMemoryConfig c = {GUEST_RAM_DEFAULT_MB, 1, 0};
    int env_set = env_ram && env_ram[0];
    unsigned want = env_set ? guest_memory_parse_mb(env_ram)
                            : (policy_ram_mb > 0 ? (unsigned)policy_ram_mb : 0);
    if (want) {
        if (want < GUEST_RAM_MIN_MB) { want = GUEST_RAM_MIN_MB; c.clamped = 1; }
        if (want > GUEST_RAM_MAX_MB) { want = GUEST_RAM_MAX_MB; c.clamped = 1; }
    } else if (env_set || policy_ram_mb > 0) c.clamped = 1; /* garbage: as if unset */
    int mode = policy_addr == 24 || policy_addr == 32 ? policy_addr : 0;
    if (env_addr && env_addr[0]) {
        if (!strcmp(env_addr, "24")) mode = 24;
        else if (!strcmp(env_addr, "32")) mode = 32;
        else c.clamped = 1;
    }
    if (mode == 0) mode = want == 0 || want > GUEST_ADDR24_MB ? 32 : 24;
    c.addr32 = mode == 32;
    c.ram_mb = !c.addr32 ? GUEST_ADDR24_MB : want ? want : GUEST_RAM_TARGET_MB;
    return c;
}

/* Same, reading the environment. */
static inline GuestMemoryConfig guest_memory_from_env(int policy_ram_mb, int policy_addr) {
    return guest_memory_resolve(policy_ram_mb, policy_addr,
                                getenv("OS999_GUEST_RAM_MB"), getenv("OS999_GUEST_ADDRESSING"));
}

#endif
