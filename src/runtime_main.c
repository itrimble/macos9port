#include "mac_runtime.h"
#include "mac_time.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_bytes(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f); return NULL;
    }
    uint8_t *bytes = malloc(length ? (size_t)length : 1);
    if (!bytes || fread(bytes, 1, (size_t)length, f) != (size_t)length) {
        free(bytes); fclose(f); return NULL;
    }
    fclose(f);
    *size = (size_t)length;
    return bytes;
}

static void trace_bytes(MacCpu *cpu, const char *label, uint32_t address, size_t count) {
    size_t size = mac_cpu_ram_size(cpu);
    if (address > size || count > size - address) {
        fprintf(stderr, "%s %08x: outside guest RAM\n", label, address);
        return;
    }
    /* Runtime memory helpers stop reading after a halt. Diagnostics must use
     * checked raw RAM, otherwise real parameter blocks falsely appear zero. */
    const uint8_t *bytes = mac_cpu_ram(cpu) + address;
    for (size_t i = 0; i < count; i++) {
        if (!(i % 16)) fprintf(stderr, "%s %08x:", label, address + (uint32_t)i);
        fprintf(stderr, " %02x", bytes[i]);
        if (i % 16 == 15 || i + 1 == count) fputc('\n', stderr);
    }
}

int main(int argc, char **argv) {
    const char *path = NULL;
    size_t budget = 100000;
    int inspect = 0, trace = 0, window = 0, indexed = 0, interactive = 0, steps_given = 0;
    const char *bridge_name = NULL, *saves = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--inspect")) inspect = 1;
        else if (!strcmp(argv[i], "--saves") && i + 1 < argc) saves = argv[++i];
        else if (!strcmp(argv[i], "--window")) window = 1;
        else if (!strcmp(argv[i], "--interactive")) interactive = 1;
        else if (!strcmp(argv[i], "--indexed")) indexed = 1;
        else if (!strcmp(argv[i], "--trace")) trace = 1;
        else if (!strcmp(argv[i], "--bridge") && i + 1 < argc) bridge_name = argv[++i];
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            char *end;
            errno = 0;
            unsigned long long v = strtoull(argv[++i], &end, 10);
            if (errno || !argv[i][0] || *end || v > 100000000) {
                fprintf(stderr, "Invalid instruction budget (0..100000000).\n"); return 2;
            }
            budget = (size_t)v;
            steps_given = 1;
        } else if (argv[i][0] != '-' && !path) path = argv[i];
        else { fprintf(stderr, "Unexpected argument: %s\n", argv[i]); return 2; }
    }
    if (interactive && (!window || steps_given || inspect)) {
        fprintf(stderr, "--interactive requires --window and cannot combine with --steps or --inspect.\n");
        return 2;
    }
    if (interactive) budget = SIZE_MAX;
    if (!path) {
        fprintf(stderr, "Usage: ospi-runtime [--inspect] [--window] [--indexed] [--trace] [--saves DIR] [--steps N | --interactive] resource-fork\n");
        return 2;
    }
    size_t size;
    uint8_t *bytes = read_bytes(path, &size);
    if (!bytes) { fprintf(stderr, "Cannot read resource fork (limit 64 MiB).\n"); return 2; }
    MacResourceFork fork;
    char error[240];
    if (mac_resource_open_container(&fork, bytes, size, error, sizeof error)) {
        fprintf(stderr, "Resource fork: %s\n", error); free(bytes); return 2;
    }
    printf("resources=%zu\n", mac_resource_count(&fork));
    if (inspect) {
        for (size_t i = 0; i < mac_resource_count(&fork); i++) {
            MacResource r;
            if (!mac_resource_at(&fork, i, &r))
                printf("type=%08x id=%d bytes=%zu attributes=%02x\n", r.type, r.id, r.size, r.attributes);
        }
        free(bytes); return 0;
    }
    MacRuntime rt;
    if (mac_runtime_init(&rt, &fork, error, sizeof error)) {
        fprintf(stderr, "Loader: %s\n", error); free(bytes); return 2;
    }
    rt.trace = trace;
    rt.indexed_requested = indexed;
    char data_root[PATH_MAX];
    if (!realpath(path, data_root)) {
        /* namedfork paths are virtual; canonicalize their base file. */
        size_t length = strlen(path), suffix = strlen("/..namedfork/rsrc");
        if (length <= suffix || length >= sizeof data_root ||
            strcmp(path + length - suffix, "/..namedfork/rsrc")) data_root[0] = 0;
        else {
            char base[PATH_MAX];
            memcpy(base, path, length - suffix); base[length - suffix] = 0;
            if (!realpath(base, data_root)) data_root[0] = 0;
        }
    }
    char *slash = strrchr(data_root, '/');
    if (slash) { if (slash == data_root) slash[1] = 0; else *slash = 0; }
    if (!slash || mac_files_root(&rt, data_root)) {
        fprintf(stderr, "Cannot establish the application's read-only data directory.\n");
        mac_runtime_destroy(&rt); free(bytes); return 2;
    }
    /* Saves live in a distinct directory with bounded quotas; without --saves
     * the data volume stays read-only and every guest write fails cleanly. */
    if (saves && mac_files_writable(&rt, saves, 64, UINT64_C(16) << 20)) {
        fprintf(stderr, "Cannot establish the writable save directory: %s\n", saves);
        mac_runtime_destroy(&rt); free(bytes); return 2;
    }
    int result;
    if (window) {
        const char *name = strrchr(path, '/');
        char title[256];
        snprintf(title, sizeof title, "%s", name ? name + 1 : path);
        size_t length = strlen(title);
        if (length > 5 && !strcmp(title + length - 5, ".rsrc")) title[length - 5] = 0;
        if (bridge_name) {
            RuntimeBridge *bridge = runtime_bridge_open(bridge_name);
            if (!bridge) {
                fprintf(stderr, "Could not attach to the OS999 guest display surface.\n");
                result = 5;
            } else {
                result = mac_runtime_bridge_window(&rt, budget, bridge);
                runtime_bridge_close(bridge);
            }
        } else result = mac_runtime_window(&rt, title, budget);
    } else {
        /* Deterministic headless clock, not a real-time performance model. */
        while (rt.instructions < budget && !mac_cpu_halted(rt.cpu)) {
            size_t remain = budget - (size_t)rt.instructions;
            rt.ticks++;
            if (mac_time_set(&rt, rt.clock_us + 16667)) break;
            if (!mac_runtime_run(&rt, remain < 10000 ? remain : 10000)) break;
        }
        result = rt.exited ? 0 : mac_cpu_halted(rt.cpu) ? 3 : 4;
    }
    printf("instructions=%llu cycles=%llu traps=%llu pc=%08x last_trap=%04x last_trap_pc=%08x\n",
           (unsigned long long)rt.instructions, (unsigned long long)mac_cpu_cycles(rt.cpu),
           (unsigned long long)rt.traps, mac_cpu_get(rt.cpu, MAC_PC), rt.last_trap, rt.last_trap_pc);
    if (trace) {
        for (int r = MAC_D0; r <= MAC_A7; r++)
            fprintf(stderr, "%c%d=%08x%c", r < MAC_A0 ? 'd' : 'a', r % 8,
                    mac_cpu_get(rt.cpu, (MacCpuReg)r), r % 4 == 3 ? '\n' : ' ');
        uint32_t pc = mac_cpu_get(rt.cpu, MAC_PC);
        if (pc < mac_cpu_ram_size(rt.cpu) && mac_cpu_ram_size(rt.cpu) - pc >= 16) {
            fprintf(stderr, "pc bytes:");
            for (unsigned i = 0; i < 16; i++) fprintf(stderr, " %02x", mac_cpu_ram(rt.cpu)[pc + i]);
            fputc('\n', stderr);
        }
        trace_bytes(rt.cpu, "stack", mac_cpu_get(rt.cpu, MAC_A7), 64);
        trace_bytes(rt.cpu, "a0", mac_cpu_get(rt.cpu, MAC_A0), 128);
    }
    if (!window) fprintf(stderr, "%s\n", mac_cpu_halted(rt.cpu) ? mac_cpu_error(rt.cpu) : "Instruction budget exhausted; gameplay unverified.");
    mac_runtime_destroy(&rt);
    free(bytes);
    return result;
}
