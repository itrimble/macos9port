#include "native_policy.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define FORK_LIMIT (64u * 1024 * 1024)

static int inside(const char *root, const char *path) {
    size_t n = strlen(root);
    return !strncmp(root, path, n) && ((n == 1 && root[0] == '/') || path[n] == '/' || path[n] == 0);
}

/* Reads one fork if it exists, is a regular file under the volume and is small enough. 0 absent/unusable, 1 read. */
static int read_fork(const char *root, const char *path, const char *suffix, uint8_t **bytes, size_t *size) {
    char resolved[PATH_MAX], open_path[PATH_MAX];
    *bytes = NULL; *size = 0;
    if (!realpath(path, resolved) || !inside(root, resolved)) return 0;
    if (snprintf(open_path, sizeof open_path, "%s%s", resolved, suffix) >= (int)sizeof open_path) return 0;
    int fd = open(open_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > FORK_LIMIT) { close(fd); return 0; }
    uint8_t *b = malloc((size_t)st.st_size);
    size_t got = 0;
    while (b && got < (size_t)st.st_size) {
        ssize_t k = read(fd, b + got, (size_t)st.st_size - got);
        if (k <= 0) break;
        got += (size_t)k;
    }
    close(fd);
    if (!b || got != (size_t)st.st_size) { free(b); return 0; }
    *bytes = b; *size = got;
    return 1;
}

/* The data fork only matters when it starts like an executable; archives and documents (often tens of MB) are not read. */
static int data_fork_is_executable(const char *root, const char *path) {
    char resolved[PATH_MAX];
    if (!realpath(path, resolved) || !inside(root, resolved)) return 0;
    int fd = open(resolved, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;
    uint8_t m[8];
    ssize_t got = read(fd, m, sizeof m);
    close(fd);
    if (got != (ssize_t)sizeof m) return 0;
    uint32_t w = (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
    return !memcmp(m, "Joy!peff", 8) || w == 0xfeedfaceu || w == 0xfeedfacfu || w == 0xcefaedfeu || w == 0xcffaedfeu || w == 0xcafebabeu;
}

int native_policy_prepare(const char *root, const char *full, const NativeSettings *settings, LaunchPolicy *policy,
                          char *logline, size_t logline_n, char *err, size_t err_n) {
    char canon[PATH_MAX], data_path[PATH_MAX], sidecar[PATH_MAX];
    if (logline_n) logline[0] = 0;
    if (!root || !full || !realpath(root, canon) || strlen(full) >= sizeof data_path - 8) return 1;
    size_t len = strlen(full);
    int is_sidecar = len > 5 && !strcasecmp(full + len - 5, ".rsrc");
    snprintf(data_path, sizeof data_path, "%s", full);
    if (is_sidecar) data_path[len - 5] = 0;
    snprintf(sidecar, sizeof sidecar, "%s.rsrc", data_path);
    uint8_t *data = NULL, *rsrc = NULL;
    size_t dn = 0, rn = 0;
    if (data_fork_is_executable(canon, data_path)) read_fork(canon, data_path, "", &data, &dn);
    if (!read_fork(canon, sidecar, "", &rsrc, &rn)) read_fork(canon, data_path, "/..namedfork/rsrc", &rsrc, &rn);
    AppClassification c;
    AppClassifyCaps caps = {1, 0};   /* the runtime has the PEF/CFM loader and the PowerPC interpreter (docs/POWERPC-RUNTIME.md) */
    app_classify(data, dn, rsrc, rn, 0, &caps, &c);
    if (c.format == APP_FMT_FAT) {
        /* A program that also has CODE 0 keeps the 68K path in the runtime (mac_ppc_runtime_open only takes programs
         * without CODE 0), so it is classified as a 68K program rather than promised a PowerPC start it will not get. */
        AppClassifyCaps m68k_only = {0, 0};
        app_classify(data, dn, rsrc, rn, 0, &m68k_only, &c);
    } else if (c.profile == APP_PROFILE_C_CLASSIC_PPC && !(c.has_cfrg && (c.cfrg_arch & APP_ARCH_PEF_PPC))) {
        /* The PowerPC start path finds its fragment through the resource fork's cfrg resource; a bare PEF in the data
         * fork has no member for it to load, and the runtime would stop at "missing CODE 0". Refuse that up front. */
        size_t k = strlen(c.missing);
        snprintf(c.missing + k, sizeof c.missing - k, "%scfrg resource naming the PowerPC fragment (the runtime starts PowerPC programs through it)",
                 k ? "; " : "");
        c.runnable = 0;
    }
    free(data); free(rsrc);
    if (c.format == APP_FMT_NONE) return 1;   /* plain data or a document: not ours to refuse */
    if (c.format == APP_FMT_MALFORMED && c.has_code0 && !c.code0_layout_ok && !(c.arch & ~APP_ARCH_M68K)) {
        /* CODE 0 exists but its jump-table header is outside what the classifier accepts. The runtime loader checks the
         * same header itself and reports exactly what is wrong, so the launch path is left as it always was. */
        memset(policy, 0, sizeof *policy);
        policy->profile = APP_PROFILE_A_LEGACY_68K;
        policy->source = "runtime-validated";
        snprintf(policy->reason, sizeof policy->reason, "CODE 0 layout not recognised by the classifier; the runtime validates it");
        if (logline_n) snprintf(logline, logline_n, "launch policy: profile=A-legacy-68k source=runtime-validated args=[] reason=%s", policy->reason);
        return 0;
    }

    /* The slot is the application's path inside the volume without any .rsrc suffix. */
    char real[PATH_MAX];
    const char *rel = "";
    if (realpath(data_path, real) || realpath(sidecar, real)) {
        size_t rl = strlen(real);
        if (rl > 5 && !strcasecmp(real + rl - 5, ".rsrc")) real[rl - 5] = 0;
        if (inside(canon, real)) rel = real + strlen(canon);
    }
    uint64_t slot = launch_slot_key(rel);
    LaunchOverride ov;
    uint64_t stale = 0;
    int found = c.identity ? launch_override_find(canon, c.identity, slot, &ov, &stale) : 0;
    LaunchSession session = {settings ? settings->addr : 0, settings && settings->ram[0] ? (unsigned)atoi(settings->ram) : 0};
    launch_policy_decide(&c, found == 1 ? &ov : NULL, &session, policy);
    char line[sizeof policy->reason + 400];
    launch_policy_describe(policy, &c, line, sizeof line);
    if (found == 2)
        snprintf(line + strlen(line), sizeof line - strlen(line), "; saved override for identity %016llx dropped: the executable at this place changed",
                 (unsigned long long)stale);
    if (logline_n) snprintf(logline, logline_n, "%s", line);
    if (policy->refuse) {
        if (err && err_n) snprintf(err, err_n, "%s", policy->message);
        return -1;
    }
    return 0;
}
