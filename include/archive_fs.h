#ifndef OSPI_ARCHIVE_FS_H
#define OSPI_ARCHIVE_FS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    size_t files;
    uint64_t bytes;
} ArchiveCopyStats;

int archive_copy_file(const char *src, const char *dst);
int archive_copy_tree(const char *src, const char *dst, ArchiveCopyStats *stats);
int archive_remove_tree(const char *path);

#endif
