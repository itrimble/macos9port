#include "archive_fs.h"
#include "ospi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

static int write_str(const char *path, const char *s) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = strlen(s);
    int ok = !n || fwrite(s, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void test_archive_copy_and_remove(const char *base) {
    char src[OSPI_PATH], dst[OSPI_PATH], p1[OSPI_PATH], p2[OSPI_PATH];
    snprintf(src, sizeof src, "%s/src", base);
    snprintf(dst, sizeof dst, "%s/dst", base);
    CHECK(volume_mkdir_p(src) == 0);

    /* 1. Empty directory fails archive_copy_tree (stats.files == 0). */
    ArchiveCopyStats stats = {0};
    CHECK(archive_copy_tree(src, dst, &stats) == -1);

    /* 2. Regular file copy and O_EXCL collision protection. */
    snprintf(p1, sizeof p1, "%s/hello.txt", src);
    snprintf(p2, sizeof p2, "%s/hello-copy.txt", base);
    CHECK(write_str(p1, "clean-room") == 0);
    CHECK(archive_copy_file(p1, p2) == 0);
    CHECK(archive_copy_file(p1, p2) == -1); /* Refuses overwrite. */
    CHECK(unlink(p2) == 0);

    /* 3. Valid tree copy at maximum permitted depth (8). */
    char deep_dir[OSPI_PATH];
    snprintf(deep_dir, sizeof deep_dir, "%s/d1/d2/d3/d4/d5/d6/d7", src);
    CHECK(volume_mkdir_p(deep_dir) == 0);
    snprintf(p1, sizeof p1, "%s/leaf.txt", deep_dir);
    CHECK(write_str(p1, "depth8") == 0);
    CHECK(archive_copy_tree(src, dst, &stats) == 0);
    CHECK(stats.files == 2 && stats.bytes == 16);
    CHECK(archive_remove_tree(dst) == 0);

    /* 4. Depth 9 exceeds ARCHIVE_COPY_MAX_DEPTH (8) and fails closed. */
    char too_deep[OSPI_PATH];
    snprintf(too_deep, sizeof too_deep, "%s/d8", deep_dir);
    CHECK(volume_mkdir_p(too_deep) == 0);
    snprintf(p2, sizeof p2, "%s/too-deep.txt", too_deep);
    CHECK(write_str(p2, "x") == 0);
    CHECK(archive_copy_tree(src, dst, &stats) == -1);
    (void)archive_remove_tree(dst);
    CHECK(unlink(p2) == 0 && rmdir(too_deep) == 0);

    /* 5. Symlink inside source tree is rejected. */
    char link_path[OSPI_PATH];
    snprintf(link_path, sizeof link_path, "%s/escape.lnk", src);
    CHECK(symlink("/etc/hosts", link_path) == 0);
    CHECK(archive_copy_tree(src, dst, &stats) == -1);
    (void)archive_remove_tree(dst);
    CHECK(unlink(link_path) == 0);

    /* 6. FIFO special file inside source tree is rejected. */
    char fifo_path[OSPI_PATH];
    snprintf(fifo_path, sizeof fifo_path, "%s/pipe.fifo", src);
    CHECK(mkfifo(fifo_path, 0600) == 0);
    CHECK(archive_copy_tree(src, dst, &stats) == -1);
    (void)archive_remove_tree(dst);
    CHECK(unlink(fifo_path) == 0);

    CHECK(archive_remove_tree(src) == 0);
}

static void test_volume_boundaries_and_trash(const char *base) {
    char vol[OSPI_PATH], out[OSPI_PATH];
    snprintf(vol, sizeof vol, "%s/vol", base);
    CHECK(volume_seed(vol) == 0);

    /* Path traversal and malformed relative paths are rejected. */
    CHECK(volume_join(NULL, sizeof out, vol, "Read Me") == -1);
    CHECK(volume_join(out, 0, vol, "Read Me") == -1);
    CHECK(volume_join(out, 4, vol, "Read Me") == -1);
    CHECK(volume_join(out, sizeof out, vol, "/etc/passwd") == -1);
    CHECK(volume_join(out, sizeof out, vol, "..") == -1);
    CHECK(volume_join(out, sizeof out, vol, ".") == -1);
    CHECK(volume_join(out, sizeof out, vol, "Documents/../Read Me") == -1);
    CHECK(volume_join(out, sizeof out, vol, "Documents//Notes") == -1);
    CHECK(volume_join(out, sizeof out, vol, "Documents/Notes") == 0);

    /* Unique folder naming. */
    char created[OSPI_NAME];
    CHECK(volume_mkdir_unique(vol, "Documents", "Folder", created, sizeof created) == 0);
    CHECK(strcmp(created, "Folder") == 0);
    CHECK(volume_mkdir_unique(vol, "Documents", "Folder", created, sizeof created) == 0);
    CHECK(strcmp(created, "Folder 2") == 0);
    CHECK(volume_mkdir_unique(vol, "Documents", "Folder", created, sizeof created) == 0);
    CHECK(strcmp(created, "Folder 3") == 0);

    /* Trashing rules: cannot trash Trash itself or slash paths; deduplicates collisions. */
    CHECK(volume_trash(vol, "", "Trash") == -1);
    CHECK(volume_trash(vol, "", "Documents/Folder") == -1);
    CHECK(volume_trash(vol, "Documents", "Folder") == 0);
    CHECK(volume_mkdir_unique(vol, "Documents", "Folder", created, sizeof created) == 0);
    CHECK(strcmp(created, "Folder") == 0);
    CHECK(volume_trash(vol, "Documents", "Folder") == 0);
    char trashed2[OSPI_PATH];
    snprintf(trashed2, sizeof trashed2, "%s/Trash/Folder 2", vol);
    struct stat st;
    CHECK(stat(trashed2, &st) == 0 && S_ISDIR(st.st_mode));

    /* Symlink inside Trash is unlinked by volume_empty_trash without deleting outside target. */
    char outside[OSPI_PATH], sentinel[OSPI_PATH], trash_link[OSPI_PATH], keep[OSPI_PATH];
    snprintf(outside, sizeof outside, "%s/outside", base);
    CHECK(volume_mkdir_p(outside) == 0);
    snprintf(sentinel, sizeof sentinel, "%s/must-survive.txt", outside);
    CHECK(write_str(sentinel, "safe") == 0);
    snprintf(trash_link, sizeof trash_link, "%s/Trash/outside-link", vol);
    CHECK(symlink(outside, trash_link) == 0);

    CHECK(volume_empty_trash(vol) == 0);
    CHECK(lstat(trash_link, &st) != 0);
    CHECK(stat(sentinel, &st) == 0);
    snprintf(keep, sizeof keep, "%s/Trash/.keep", vol);
    CHECK(stat(keep, &st) == 0);

    /* Binary control bytes in volume_read return fallback text note. */
    char bin_file[OSPI_PATH], text_buf[256];
    snprintf(bin_file, sizeof bin_file, "%s/Documents/Binary", vol);
    FILE *bf = fopen(bin_file, "wb");
    CHECK(bf != NULL);
    if (bf) {
        const unsigned char raw[4] = {'M', 'Z', 0x01, 0x00};
        CHECK(fwrite(raw, 1, sizeof raw, bf) == sizeof raw);
        fclose(bf);
    }
    CHECK(volume_read(vol, "Documents/Binary", text_buf, sizeof text_buf) == 0);
    CHECK(strstr(text_buf, "cannot show this file as text") != NULL);
}

int main(void) {
    char tmpl[] = "/tmp/ospi-archive-fs-XXXXXX";
    char *base = mkdtemp(tmpl);
    CHECK(base != NULL);
    if (!base) return 1;

    test_archive_copy_and_remove(base);
    test_volume_boundaries_and_trash(base);

    char cmd[OSPI_PATH + 32];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (fails == 0) {
        (void)system(cmd);
    } else {
        fprintf(stderr, "left fixture at %s\n", base);
    }
    if (fails) {
        fprintf(stderr, "archive_fs checks: %d failed\n", fails);
        return 1;
    }
    puts("archive_fs and volume boundary checks: ok");
    return 0;
}
