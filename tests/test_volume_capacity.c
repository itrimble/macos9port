/* Internal disk capacity (volume_capacity.h): default, persistence, shrink refusal,
 * free-space maths including host-limited cases, and the classic block view.
 * Headless; temporary directories and synthetic (sparse) sizes only. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "volume_capacity.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define MIB (UINT64_C(1) << 20)
#define GIB (UINT64_C(1) << 30)

static void sparse(const char *dir, const char *name, uint64_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0);
    if (fd >= 0) { CHECK(ftruncate(fd, (off_t)size) == 0); close(fd); }
}

static void pure_rules(void) {
    CHECK(volume_capacity_default_for(0, 100 * GIB) == 2 * GIB);                       /* fixed default */
    CHECK(volume_capacity_default_for(3 * GIB, 100 * GIB) == 3 * GIB + 512 * MIB);     /* usage plus headroom */
    CHECK(volume_capacity_default_for(0, GIB) == GIB);                                 /* host cannot supply 2 GiB */
    CHECK(volume_capacity_default_for(3 * GIB, 100 * MIB) == 3 * GIB + 100 * MIB);     /* never above used + host free */
    CHECK(volume_capacity_default_for(5 * MIB, 0) == 5 * MIB);                         /* never below used */

    VolumeSpace sp;
    volume_space_compute(2 * GIB, GIB, 10 * GIB, &sp);
    CHECK(sp.capacity == 2 * GIB && sp.used == GIB && sp.free == GIB);
    volume_space_compute(2 * GIB, GIB, 100 * MIB, &sp);                                /* host-limited */
    CHECK(sp.free == 100 * MIB && sp.host_free == 100 * MIB);
    volume_space_compute(2 * GIB, 3 * GIB, 10 * GIB, &sp);                             /* over capacity: no negative free */
    CHECK(sp.free == 0);
    volume_space_compute(2 * GIB, 0, 0, &sp);
    CHECK(sp.free == 0);

    char msg[200];
    CHECK(volume_capacity_check(3 * GIB, 2 * GIB, 50 * GIB, msg, sizeof msg) == 0);
    CHECK(volume_capacity_check(2 * GIB, 2 * GIB, 50 * GIB, msg, sizeof msg) == 0);    /* exactly full is allowed */
    msg[0] = 0;
    CHECK(volume_capacity_check(GIB, 2 * GIB, 50 * GIB, msg, sizeof msg) == -1 && strstr(msg, "smaller"));
    CHECK(volume_capacity_check(10 * GIB, 2 * GIB, GIB, msg, sizeof msg) == -1 && strstr(msg, "supply"));
    CHECK(volume_capacity_check(2 * GIB + GIB, 2 * GIB, GIB, msg, sizeof msg) == 0);   /* exactly used + host free */
    CHECK(volume_capacity_check(MIB, 0, 50 * GIB, msg, sizeof msg) == -1);             /* below the minimum */
    CHECK(volume_capacity_check(VOLUME_CAPACITY_MAX_BYTES + 1, 0, UINT64_MAX / 2, msg, sizeof msg) == -1);
}

static void classic_view(void) {
    VolumeClassic c;
    VolumeSpace sp = {64 * MIB, 0, 100 * GIB, 64 * MIB};
    volume_classic_view(&sp, &c);
    CHECK(c.block_size == 1536 && c.total_blocks == 43690 && c.free_blocks == 43690);
    sp.free = 0;
    volume_classic_view(&sp, &c);
    CHECK(c.free_blocks == 0 && c.total_blocks == 43690);
    sp.free = 10 * MIB;
    volume_classic_view(&sp, &c);
    CHECK(c.free_blocks == (10 * MIB) / 1536);
    uint64_t caps[] = {2 * GIB, 3 * GIB, UINT64_C(10) << 40, VOLUME_CAPACITY_MIN_BYTES};
    for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++) {
        VolumeSpace big = {caps[i], caps[i] / 4, 100 * GIB, caps[i] - caps[i] / 4};
        volume_classic_view(&big, &c);
        CHECK(c.total_blocks <= 65535 && c.free_blocks <= c.total_blocks);
        CHECK(c.block_size % 512 == 0 && c.block_size >= 512);
        CHECK((uint64_t)c.total_blocks * c.block_size <= UINT64_C(0x7FFFFE00));         /* just under 2 GiB */
        CHECK(c.total_bytes == (uint32_t)((uint64_t)c.total_blocks * c.block_size));
        CHECK((uint64_t)c.total_blocks * c.block_size + c.block_size > (caps[i] < UINT64_C(0x7FFFFE00) ? caps[i] : UINT64_C(0x7FFFFE00)));
    }
}

static void on_disk(void) {
    char parent[] = "/tmp/os999-capacity-XXXXXX";
    CHECK(mkdtemp(parent) != NULL);
    if (!parent[0]) return;
    char root[1024], saves[1024], path[1024];
    snprintf(root, sizeof root, "%s/Volume", parent);
    snprintf(saves, sizeof saves, "%s/Saves", parent);
    CHECK(mkdir(root, 0700) == 0 && mkdir(saves, 0700) == 0);
    snprintf(path, sizeof path, "%s/Docs", root);
    CHECK(mkdir(path, 0700) == 0);
    sparse(root, "one", MIB);
    sparse(path, "nested", 512 * 1024);
    sparse(saves, "game", 2 * MIB);          /* guest saves sit beside the volume and count */

    uint64_t used = 0, host = 0, cap = 0;
    int stored = 1;
    CHECK(volume_used_bytes(root, &used) == 0 && used == 3 * MIB + 512 * 1024);
    CHECK(volume_host_free(root, &host) == 0);
    CHECK(volume_capacity_load(root, &cap, &stored) == 0 && stored == 0);
    CHECK(cap == volume_capacity_default_for(used, host) && cap >= used);

    /* Default is saved on first use, and survives a "reload" (a fresh read from disk). */
    CHECK(volume_capacity_ensure(root) == 0);
    uint64_t again = 0;
    CHECK(volume_capacity_load(root, &again, &stored) == 0 && stored == 1 && again == cap);
    snprintf(path, sizeof path, "%s/.os999/capacity", root);
    CHECK(access(path, F_OK) == 0);

    /* Expand (and shrink above usage), persisted. */
    char msg[200] = "";
    CHECK(volume_capacity_set(root, 128 * MIB, msg, sizeof msg) == 0);
    volume_space_invalidate();
    CHECK(volume_capacity_load(root, &again, &stored) == 0 && stored == 1 && again == 128 * MIB);
    CHECK(volume_capacity_set(root, 64 * MIB, msg, sizeof msg) == 0);
    CHECK(volume_capacity_set(root, 128 * MIB, msg, sizeof msg) == 0);

    /* Refuse to shrink below usage; the stored value is untouched. */
    sparse(root, "two", 70 * MIB);           /* used is now about 73.5 MiB */
    msg[0] = 0;
    CHECK(volume_capacity_set(root, 64 * MIB, msg, sizeof msg) == -1 && strstr(msg, "smaller"));
    CHECK(volume_capacity_load(root, &again, &stored) == 0 && again == 128 * MIB);
    msg[0] = 0;
    CHECK(volume_capacity_set(root, 128 * MIB + host + GIB, msg, sizeof msg) == -1 && strstr(msg, "supply"));

    /* Free-space maths with a real tree, host-limited and not. */
    VolumeSpace sp;
    CHECK(volume_space_at(root, 100 * GIB, &sp) == 0);
    CHECK(sp.capacity == 128 * MIB && sp.used >= 73 * MIB && sp.free == sp.capacity - sp.used);
    CHECK(volume_space_at(root, 5 * MIB, &sp) == 0 && sp.free == 5 * MIB);
    CHECK(volume_space_at(root, 0, &sp) == 0 && sp.free == 0);
    CHECK(volume_space(root, 1, &sp) == 0 && sp.capacity == 128 * MIB && sp.free <= sp.capacity - sp.used);

    /* Reserve: fits, then does not; the refusal leaves a Finder-ready sentence. */
    CHECK(volume_space_reserve(root, MIB, msg, sizeof msg) == 0);
    msg[0] = 0;
    CHECK(volume_space_reserve(root, 100 * MIB, msg, sizeof msg) == VOLUME_DISK_FULL && strstr(msg, "full"));
    const char *notice = volume_disk_full_notice(1);
    CHECK(notice && strstr(notice, "full") && volume_disk_full_notice(0) == NULL);

    /* A damaged capacity file is ignored, not trusted. */
    int fd = open(path, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && write(fd, "garbage", 7) == 7);
    close(fd);
    CHECK(volume_capacity_load(root, &again, &stored) == 0 && stored == 0 && again >= used);

    /* Per-process budget used by the guest runtime. */
    CHECK(volume_capacity_set(root, 128 * MIB, msg, sizeof msg) == 0);
    VolumeBudget b;
    CHECK(volume_budget_open(&b, root) == 0);
    CHECK(volume_budget_spend(&b, MIB) == 0);
    CHECK(volume_budget_spend(&b, 500 * MIB) == VOLUME_DISK_FULL);
}

int main(void) {
    pure_rules();
    classic_view();
    on_disk();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("volume capacity checks passed");
    return 0;
}
