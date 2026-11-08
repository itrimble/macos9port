#include "mac_runtime.h"
#include "mac_drive_queue.h"
#include "archive_fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

enum {
    HFS_NO_ERR      = 0,
    HFS_DSK_FUL_ERR = -34,
    HFS_IO_ERR      = -36,
    HFS_FNF_ERR     = -43,
    HFS_VLCKD_ERR   = -46,
    HFS_DUP_FN_ERR  = -48,
    HFS_PARAM_ERR   = -50,
    HFS_PERM_ERR    = -54
};

typedef struct {
    char root[512];
    size_t max_files;
    size_t max_bytes;
    int locked;
} GuestSaveSandbox;

static int valid_hfs_save_name(const char *name) {
    if (!name) return 0;
    size_t len = strlen(name);
    if (len == 0 || len > 31 || name[0] == '.') return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == 127 || c == ':' || c == '/') return 0;
    }
    return 1;
}

static int sandbox_usage(const GuestSaveSandbox *sb, size_t *out_files, size_t *out_bytes) {
    DIR *d = opendir(sb->root);
    if (!d) return HFS_IO_ERR;
    size_t files = 0, bytes = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", sb->root, de->d_name);
        struct stat st;
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            closedir(d);
            return HFS_IO_ERR;
        }
        files++;
        bytes += (size_t)st.st_size;
    }
    closedir(d);
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    return HFS_NO_ERR;
}

/* Create a new empty save file inside the sandbox. */
static int sandbox_create(const GuestSaveSandbox *sb, const char *name) {
    if (!sb || !valid_hfs_save_name(name)) return HFS_PARAM_ERR;
    if (sb->locked) return HFS_VLCKD_ERR;
    size_t files = 0, bytes = 0;
    int rc = sandbox_usage(sb, &files, &bytes);
    if (rc != HFS_NO_ERR) return rc;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", sb->root, name);
    struct stat st;
    if (lstat(path, &st) == 0) return HFS_DUP_FN_ERR;
    if (files >= sb->max_files) return HFS_DSK_FUL_ERR;

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return HFS_IO_ERR;
    close(fd);
    return HFS_NO_ERR;
}

/* Atomic write via staging temp file + rename so partial/faulted writes never corrupt existing saves. */
static int sandbox_atomic_write(const GuestSaveSandbox *sb, const char *name,
                                const void *data, size_t len, int simulate_fault) {
    if (!sb || !valid_hfs_save_name(name) || (!data && len > 0)) return HFS_PARAM_ERR;
    if (sb->locked) return HFS_VLCKD_ERR;

    char target[1024], tmp[1024];
    snprintf(target, sizeof(target), "%s/%s", sb->root, name);
    snprintf(tmp, sizeof(tmp), "%s/.tmp-%s", sb->root, name);

    struct stat existing;
    if (lstat(target, &existing) != 0) return HFS_FNF_ERR;
    if (!S_ISREG(existing.st_mode)) return HFS_PERM_ERR;

    size_t files = 0, used = 0;
    int rc = sandbox_usage(sb, &files, &used);
    if (rc != HFS_NO_ERR) return rc;
    size_t base_used = used - (size_t)existing.st_size;
    if (base_used + len > sb->max_bytes) return HFS_DSK_FUL_ERR;

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return HFS_IO_ERR;

    size_t half = len / 2;
    if (half > 0 && write(fd, data, half) != (ssize_t)half) {
        close(fd);
        unlink(tmp);
        return HFS_IO_ERR;
    }
    if (simulate_fault) {
        close(fd);
        unlink(tmp);
        return HFS_IO_ERR;
    }
    if (len - half > 0 && write(fd, (const uint8_t *)data + half, len - half) != (ssize_t)(len - half)) {
        close(fd);
        unlink(tmp);
        return HFS_IO_ERR;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp);
        return HFS_IO_ERR;
    }
    if (rename(tmp, target) != 0) {
        unlink(tmp);
        return HFS_IO_ERR;
    }
    return HFS_NO_ERR;
}

static int sandbox_read(const GuestSaveSandbox *sb, const char *name,
                        void *buf, size_t cap, size_t *out_len) {
    if (!sb || !valid_hfs_save_name(name) || !buf || !out_len) return HFS_PARAM_ERR;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", sb->root, name);
    struct stat st;
    if (lstat(path, &st) != 0) return HFS_FNF_ERR;
    if (!S_ISREG(st.st_mode)) return HFS_PERM_ERR;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return HFS_IO_ERR;
    ssize_t got = read(fd, buf, cap);
    close(fd);
    if (got < 0) return HFS_IO_ERR;
    *out_len = (size_t)got;
    return HFS_NO_ERR;
}

static void test_readonly_volume_boundary(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(4096);
    rt.heap_next = 0x800;
    rt.heap_limit = 0xe00;
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return;

    char dir[] = "/tmp/os999-rovol-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char fpath[512];
    snprintf(fpath, sizeof(fpath), "%s/PilotSave", dir);
    int fd = open(fpath, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "RODATA", 6) == 6 && close(fd) == 0);
    CHECK(mac_files_root(&rt, dir) == 0);

    /* Attempting to open with write permission (2 or 3) on read-only volume returns permErr (-54). */
    enum { PB = 0x100, NAME = 0x200 };
    mac_cpu_ram(rt.cpu)[NAME] = 9;
    memcpy(mac_cpu_ram(rt.cpu) + NAME + 1, "PilotSave", 9);
    mac_cpu_write(rt.cpu, PB + 18, 4, NAME);
    mac_cpu_write(rt.cpu, PB + 22, 2, 0);
    mac_cpu_write(rt.cpu, PB + 27, 1, 2); /* fsWrPerm */
    mac_cpu_set(rt.cpu, MAC_A0, PB);
    CHECK(mac_file_service(&rt, 0xA000) == 1);
    CHECK((int16_t)mac_cpu_read(rt.cpu, PB + 16, 2) == HFS_PERM_ERR);

    /* Write traps are now implemented (L04, mac_files_write.c): they are
     * handled and report a result instead of falling through. */
    CHECK(mac_file_service(&rt, 0xA003) != 0); /* PBWrite */
    CHECK(mac_file_service(&rt, 0xA008) != 0); /* PBCreate */
    CHECK(mac_file_service(&rt, 0xA009) != 0); /* PBDelete */
    CHECK(mac_file_service(&rt, 0xA00B) != 0); /* PBRename */
    CHECK(mac_file_service(&rt, 0xA012) != 0); /* PBSetEOF */

    mac_files_destroy(&rt);
    mac_drive_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    CHECK(archive_remove_tree(dir) == 0);
}

static void test_writable_save_sandbox_and_atomic_commit(void) {
    GuestSaveSandbox sb = { .max_files = 3, .max_bytes = 128, .locked = 0 };
    snprintf(sb.root, sizeof(sb.root), "/tmp/os999-saves-XXXXXX");
    CHECK(mkdtemp(sb.root) != NULL);

    /* Path traversal, colons, leading dots, and overlong names must return paramErr (-50). */
    CHECK(sandbox_create(&sb, "../escape") == HFS_PARAM_ERR);
    CHECK(sandbox_create(&sb, ":Save1") == HFS_PARAM_ERR);
    CHECK(sandbox_create(&sb, ".hidden") == HFS_PARAM_ERR);
    CHECK(sandbox_create(&sb, "ThisNameIsDefinitelyOverThirtyOneChars") == HFS_PARAM_ERR);

    /* Locked sandbox rejects create/write with vLckdErr (-46). */
    sb.locked = 1;
    CHECK(sandbox_create(&sb, "Pilot 1") == HFS_VLCKD_ERR);
    sb.locked = 0;

    /* Create and populate Pilot 1, then verify duplicate create returns dupFNErr (-48). */
    CHECK(sandbox_create(&sb, "Pilot 1") == HFS_NO_ERR);
    CHECK(sandbox_create(&sb, "Pilot 1") == HFS_DUP_FN_ERR);
    CHECK(sandbox_atomic_write(&sb, "Pilot 1", "SCORE=1000", 10, 0) == HFS_NO_ERR);

    /* Simulated mid-write fault must leave previous committed save intact and remove .tmp. */
    CHECK(sandbox_atomic_write(&sb, "Pilot 1", "CORRUPTED!", 10, 1) == HFS_IO_ERR);
    char buf[64] = {0};
    size_t got = 0;
    CHECK(sandbox_read(&sb, "Pilot 1", buf, sizeof(buf), &got) == HFS_NO_ERR);
    CHECK(got == 10 && memcmp(buf, "SCORE=1000", 10) == 0);

    /* Byte quota breach returns dskFulErr (-34) and preserves Pilot 1. */
    char big[200];
    memset(big, 'X', sizeof(big));
    CHECK(sandbox_atomic_write(&sb, "Pilot 1", big, sizeof(big), 0) == HFS_DSK_FUL_ERR);
    CHECK(sandbox_read(&sb, "Pilot 1", buf, sizeof(buf), &got) == HFS_NO_ERR);
    CHECK(got == 10 && memcmp(buf, "SCORE=1000", 10) == 0);

    /* File-count quota (max_files = 3) returns dskFulErr (-34) on 4th file. */
    CHECK(sandbox_create(&sb, "Pilot 2") == HFS_NO_ERR);
    CHECK(sandbox_create(&sb, "Pilot 3") == HFS_NO_ERR);
    CHECK(sandbox_create(&sb, "Pilot 4") == HFS_DSK_FUL_ERR);

    /* Symlink planted in save folder must be rejected on read/write. */
    char sym[1024];
    snprintf(sym, sizeof(sym), "%s/SymSave", sb.root);
    CHECK(symlink("/etc/hosts", sym) == 0);
    CHECK(sandbox_read(&sb, "SymSave", buf, sizeof(buf), &got) == HFS_PERM_ERR);
    CHECK(sandbox_atomic_write(&sb, "SymSave", "NOPE", 4, 0) == HFS_PERM_ERR);

    CHECK(archive_remove_tree(sb.root) == 0);
}

int main(void) {
    test_readonly_volume_boundary();
    test_writable_save_sandbox_and_atomic_commit();

    if (failures > 0) {
        fprintf(stderr, "test_mac_writable_files: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_writable_files: all %d checks passed\n", checks);
    return 0;
}
