#include "launch_profile.h"
#include "ospi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void add_arg(LaunchPolicy *p, const char *name, unsigned value) {
    if (p->nargs + 2 > LAUNCH_MAX_ARGS) return;
    snprintf(p->args[p->nargs++], LAUNCH_ARG_LEN, "%s", name);
    snprintf(p->args[p->nargs++], LAUNCH_ARG_LEN, "%u", value);
}

/* RAM for a 32-bit machine that holds a partition of `preferred` bytes above the 16 MB low region, with headroom. */
static unsigned ram_for(uint32_t preferred) {
    unsigned need = 16u + (unsigned)((preferred + 0xfffffu) >> 20) + 16u;
    unsigned ram = need <= 256u ? 256u : need;
    return ram > 1024u ? 1024u : ram;
}

static int override_fits(const AppClassification *c, const LaunchOverride *o) {
    if (!o) return 0;
    if (o->profile == APP_PROFILE_A_LEGACY_68K || o->profile == APP_PROFILE_B_CLEAN_68K) return (c->arch & APP_ARCH_M68K) && c->code0_layout_ok;
    if (o->profile == APP_PROFILE_C_CLASSIC_PPC) return (c->arch & APP_ARCH_PEF_PPC) && c->pef_has_main;
    return 0;
}

void launch_policy_decide(const AppClassification *c, const LaunchOverride *ov, const LaunchSession *session, LaunchPolicy *p) {
    static const LaunchSession none;
    memset(p, 0, sizeof *p);
    if (!session) session = &none;
    p->profile = c->profile;
    p->source = "classifier";
    char note[APP_TEXT] = "";
    if (ov && override_fits(c, ov)) {
        p->profile = ov->profile;
        p->source = "override";
    } else if (ov) {
        snprintf(note, sizeof note, "; saved override (%s) ignored: it does not fit this executable", app_profile_name(ov->profile));
    }
    /* A listed missing requirement (CarbonLib services, a fragment the runtime cannot find) refuses the launch even when
     * the host has the CPU path: "runnable" says the runtime exists, "missing" says what this program still needs. */
    int runnable = p->profile == APP_PROFILE_C_CLASSIC_PPC ? c->runnable && c->profile == APP_PROFILE_C_CLASSIC_PPC && !c->missing[0]
                                                          : (p->profile == APP_PROFILE_A_LEGACY_68K || p->profile == APP_PROFILE_B_CLEAN_68K) && (c->arch & APP_ARCH_M68K);
    /* A mixed program whose classified path is PowerPC but whose host cannot run it still has its 68K half. */
    if (!runnable && p->profile == APP_PROFILE_C_CLASSIC_PPC && c->alternate && (c->arch & APP_ARCH_M68K) && !c->runnable && c->code0_layout_ok) {
        p->profile = c->has_size && c->size_declares_32bit ? APP_PROFILE_B_CLEAN_68K : APP_PROFILE_A_LEGACY_68K;
        runnable = 1;
    }
    if (c->profile == APP_PROFILE_D_UNSUPPORTED || !runnable) {
        p->refuse = 1;
        snprintf(p->reason, sizeof p->reason, "%s", c->reason);
        snprintf(p->message, sizeof p->message, "OS999 cannot run this application.\n%s.%s%s%s", c->reason,
                 c->missing[0] ? "\nMissing: " : "", c->missing, "\nOS999 does not start it in a different mode as a fallback.");
        return;
    }
    int addr = 0;
    unsigned ram = 0;
    const char *why = "default machine";
    switch (p->profile) {
    case APP_PROFILE_A_LEGACY_68K:
        if (session->addr == 32) { addr = 24; why = "profile A needs 24-bit addressing; session default is 32-bit"; }
        else why = "profile A runs on the default 24-bit machine";
        break;
    case APP_PROFILE_B_CLEAN_68K:
        if (c->has_size && c->size_preferred > 12u * 1024 * 1024) {
            unsigned need = ram_for(c->size_preferred);
            if (!(session->addr == 32 && session->ram_mb >= need)) { addr = 32; ram = need; }
            why = "profile B asks for a partition larger than the 24-bit region holds";
        } else why = "profile B runs on the session machine";
        break;
    default:   /* C */
        {
            unsigned need = ram_for(c->has_size ? c->size_preferred : 0);
            if (!(session->addr == 32 && session->ram_mb >= need)) { addr = 32; ram = need; }
            why = "profile C needs 32-bit addressing";
        }
    }
    if (ov && !strcmp(p->source, "override")) {   /* explicit choices win over the profile's own defaults */
        if (ov->addr == 24) { addr = 24; ram = 0; }
        else if (ov->addr == 32) { addr = 32; ram = ov->ram_mb ? ov->ram_mb : (ram ? ram : 256); }
        why = "saved per-application override";
    }
    p->addr = addr;
    p->ram_mb = ram;
    if (addr) add_arg(p, "--addressing", (unsigned)addr);
    if (addr == 32 && ram) add_arg(p, "--ram-mb", ram);
    snprintf(p->reason, sizeof p->reason, "%s; %s%s", why, c->reason, note);
}

void launch_policy_describe(const LaunchPolicy *p, const AppClassification *c, char *out, size_t n) {
    char args[LAUNCH_MAX_ARGS * (LAUNCH_ARG_LEN + 1) + 1] = "";
    for (int i = 0; i < p->nargs; i++) {
        size_t len = strlen(args);
        snprintf(args + len, sizeof args - len, "%s%s", len ? " " : "", p->args[i]);
    }
    snprintf(out, n, "launch policy: profile=%s source=%s confidence=%s identity=%016llx %s args=[%s] reason=%s",
             app_profile_name(p->profile), p->source, app_confidence_name(c->confidence), (unsigned long long)c->identity,
             p->refuse ? "REFUSED" : "launch", args, p->reason);
}

uint64_t launch_slot_key(const char *path) {
    uint64_t h = 1469598103934665603ull;
    for (; path && *path; path++) { h ^= (unsigned char)*path; h *= 1099511628211ull; }
    return h ? h : 1;
}

/* ---- override store ---- */
/* The store lives in the volume's private folder (<root>/.os999/launch-profiles, Build 23 rule). Builds before that
 * kept it at <root>/.os999-launch-profiles; that file is read once, copied to the new place and removed. */
#define STORE_NAME "launch-profiles"
#define OLD_STORE_NAME ".os999-launch-profiles"
#define MAX_ENTRIES 256

static int join(char *out, size_t n, const char *root, const char *rest) {
    int k = root && root[0] ? snprintf(out, n, "%s/%s", root, rest) : -1;
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

/* Parse one store file. 0 ok (including a missing file: *exists says which), -1 unreadable. */
static int read_store(const char *path, LaunchOverride *v, int *count, int *exists) {
    *count = 0;
    *exists = 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    *exists = 1;
    char text[MAX_ENTRIES * 80 + 1];
    struct stat st;
    ssize_t got = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) ? read(fd, text, sizeof text - 1) : -1;
    close(fd);
    if (got < 0) return -1;
    text[got] = 0;
    for (char *line = text; *line && *count < MAX_ENTRIES;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        unsigned long long id, slot;
        char letter;
        int addr;
        unsigned ram;
        int used = 0;
        if (sscanf(line, "v1 %16llx %16llx %c %d %u%n", &id, &slot, &letter, &addr, &ram, &used) == 5 && id && slot &&
            (letter == 'A' || letter == 'B' || letter == 'C') && (addr == 0 || addr == 24 || addr == 32) &&
            ram <= 1024 && (addr == 32 || ram == 0) && !line[used]) {
            v[(*count)++] = (LaunchOverride){id, slot, letter == 'A' ? APP_PROFILE_A_LEGACY_68K : letter == 'B' ? APP_PROFILE_B_CLEAN_68K : APP_PROFILE_C_CLASSIC_PPC, addr, ram};
        }   /* anything else (damaged line, unknown version) is dropped, never trusted */
        if (!next) break;
        line = next;
    }
    return 0;
}

static int save(const char *root, const LaunchOverride *v, int count) {
    char dir[4096], path[4096], temp[4096];
    if (!root || !root[0]) return -1;
    os999_private_set_root(root);   /* the private folder is anchored at the volume this store belongs to */
    if (os999_private_dir(NULL, dir, sizeof dir) || join(path, sizeof path, dir, STORE_NAME) || join(temp, sizeof temp, dir, STORE_NAME ".tmp")) return -1;
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    int bad = 0;
    for (int i = 0; i < count && !bad; i++) {
        char line[96];
        int k = snprintf(line, sizeof line, "v1 %016llx %016llx %c %d %u\n", (unsigned long long)v[i].identity, (unsigned long long)v[i].slot,
                         v[i].profile == APP_PROFILE_A_LEGACY_68K ? 'A' : v[i].profile == APP_PROFILE_B_CLEAN_68K ? 'B' : 'C', v[i].addr, v[i].ram_mb);
        bad = k < 0 || write(fd, line, (size_t)k) != k;
    }
    if (close(fd)) bad = 1;
    if (!bad && rename(temp, path)) bad = 1;
    if (bad) unlink(temp);
    return bad ? -1 : 0;
}

static int load(const char *root, LaunchOverride *v, int *count) {
    char path[4096], old[4096];
    int exists;
    *count = 0;
    if (join(path, sizeof path, root, ".os999/" STORE_NAME) || join(old, sizeof old, root, OLD_STORE_NAME)) return -1;
    if (read_store(path, v, count, &exists)) return -1;
    if (exists) return 0;
    /* No store in the private folder yet: take over an old-location file once. A symlink there is never followed. */
    if (read_store(old, v, count, &exists)) return -1;
    if (exists && save(root, v, *count) == 0) unlink(old);   /* the old file goes only after the copy is safely written */
    return 0;
}

int launch_override_find(const char *root, uint64_t identity, uint64_t slot, LaunchOverride *out, uint64_t *stale) {
    LaunchOverride v[MAX_ENTRIES];
    int n;
    if (load(root, v, &n)) return -1;
    for (int i = 0; i < n; i++)
        if (v[i].identity == identity) { *out = v[i]; return 1; }
    for (int i = 0; i < n; i++)
        if (v[i].slot == slot) {   /* same place, different executable: the saved choice described code that is gone */
            if (stale) *stale = v[i].identity;
            memmove(v + i, v + i + 1, (size_t)(n - i - 1) * sizeof *v);
            return save(root, v, n - 1) ? -1 : 2;
        }
    return 0;
}

int launch_override_set(const char *root, const LaunchOverride *o) {
    LaunchOverride v[MAX_ENTRIES];
    int n;
    if (!o || !o->identity || !o->slot || load(root, v, &n)) return -1;
    if (o->profile < APP_PROFILE_A_LEGACY_68K || o->profile > APP_PROFILE_C_CLASSIC_PPC) return -1;
    if (!(o->addr == 0 || o->addr == 24 || o->addr == 32) || o->ram_mb > 1024 || (o->ram_mb && o->addr != 32)) return -1;
    int w = 0;
    for (int i = 0; i < n; i++) if (v[i].identity != o->identity && v[i].slot != o->slot) v[w++] = v[i];
    if (w == MAX_ENTRIES) return -1;
    v[w++] = *o;
    return save(root, v, w);
}

int launch_override_clear(const char *root, uint64_t identity) {
    LaunchOverride v[MAX_ENTRIES];
    int n, w = 0;
    if (load(root, v, &n)) return -1;
    for (int i = 0; i < n; i++) if (v[i].identity != identity) v[w++] = v[i];
    return w == n ? 0 : save(root, v, w);
}
