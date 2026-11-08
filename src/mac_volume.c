#define _DARWIN_C_SOURCE 1

#define _POSIX_C_SOURCE 200809L
#include "mac_runtime.h"
#include "mac_drive_queue.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Logical, read-only mounted directory, not a physical HFS disk. HVolumeParam
 * is packed on two-byte boundaries and occupies 122 bytes. No raw blocks,
 * disk driver, Finder metadata, or host free-space values are fabricated.
 * Capacity is the occupied 512-byte allocation units of representable root
 * files. Children are counted, but recursive catalog access is not supported.
 * Public structure: https://ioi-xd.net/os9docs/Files_8h_source.html
 */
enum { VOLUME_PB_SIZE = 122, SCAN_LIMIT = 4096 };
static const char volume_name[] = "OS Pi Data";

static int range(MacCpu *cpu, uint32_t p, size_t n) {
    size_t ram = mac_cpu_ram_size(cpu);
    return p <= ram && n <= ram - p;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] * 256u + p[1]); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static int name_supported(const char *name) {
    size_t n = strlen(name);
    if (!n || n > 255 || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c >= 127 || c == ':' || c == '/' || c == '\\') return 0;
    }
    return 1;
}
static int volume_matches(const uint8_t *name) {
    size_t n = name[0], expected = sizeof(volume_name) - 1;
    if (n == expected + 1 && name[n] == ':') n--;
    if (n != expected) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned a = name[i + 1], b = (unsigned char)volume_name[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return 1;
}
static uint32_t mac_date(time_t date) {
    int64_t value = (int64_t)date;
    if (value < -INT64_C(2082844800) || value > INT64_C(2212122495)) return 0;
    return (uint32_t)(value + INT64_C(2082844800));
}

typedef struct {
    struct stat root;
    uint32_t files, directories, blocks;
} VolumeMetadata;

static int volume_metadata(int directory_fd, VolumeMetadata *metadata) {
    if (fstat(directory_fd, &metadata->root) || !S_ISDIR(metadata->root.st_mode)) return -35;
    /* A distinct open file description avoids changing the pinned fd's offset. */
    int fd = openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -36;
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return -36; }
    unsigned scanned = 0, files = 0, directories = 0;
    uint64_t blocks = 0;
    int error = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) error = -36; break; }
        if (++scanned > SCAN_LIMIT) { error = -108; break; }
        if (!name_supported(entry->d_name)) continue;
        struct stat info;
        if (fstatat(directory_fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW)) {
            error = -36; break;
        }
        if (S_ISDIR(info.st_mode)) directories++;
        else if (S_ISREG(info.st_mode)) {
            if (info.st_size < 0 || info.st_size > INT32_MAX) { error = -108; break; }
            files++;
            blocks += ((uint64_t)info.st_size + 511) / 512;
            if (blocks > UINT16_MAX) { error = -108; break; }
        }
    }
    if (closedir(dir) && !error) error = -36;
    if (error) return error;
    metadata->files = files;
    metadata->directories = directories;
    metadata->blocks = (uint32_t)blocks;
    return 0;
}

/* Shares the exact visibility, capacity, and work bounds used by PBHGetVInfo.
 * Output remains unchanged on failure. */
int mac_volume_blocks(int directory_fd, uint32_t *blocks) {
    if (!blocks) return -50;
    VolumeMetadata metadata;
    int error = volume_metadata(directory_fd, &metadata);
    if (!error) *blocks = metadata.blocks;
    return error;
}

/* Returns an OSErr; dispatcher owns ioResult, D0, and condition codes. All
 * guest outputs are committed only after every validation and scan succeeds.
 */
/* `size` is how much of the parameter block the call owns: VOLUME_PB_SIZE for PBHGetVInfo, 64 (the plain volumeParam, ending at
 * ioVFrBlk) for PBGetVInfo. Nothing at or beyond `size` is read or written, because a PBGetVInfo caller's block is that small. */
static int volume_info(MacRuntime *rt, uint32_t pb, int directory_fd, size_t size) {
    if (!rt || !rt->cpu || (pb & 1) || !range(rt->cpu, pb, size)) return -50;
    if (directory_fd < 0) return -35;
    uint8_t *ram = mac_cpu_ram(rt->cpu), *input = ram + pb;
    uint32_t name = get32(input + 18);
    int16_t volume = (int16_t)get16(input + 22), index = (int16_t)get16(input + 28);
    if (index > 1) return -35;
    if (name && (!range(rt->cpu, name, sizeof(volume_name)) ||
                 ((uint64_t)name < (uint64_t)pb + size &&
                  (uint64_t)name + sizeof(volume_name) > pb))) return -50;
    if (index <= 0) {
        if (index < 0 && name) {
            if (!range(rt->cpu, name, 1u + ram[name])) return -50;
            if (ram[name] && !volume_matches(ram + name)) return -35;
            if (!ram[name] && volume != 0 && volume != -1) return -35;
        } else if (volume != 0 && volume != -1) return -35;
    }
    VolumeMetadata metadata;
    int error = volume_metadata(directory_fd, &metadata);
    if (error) return error;
    if (mac_drive_mount(rt, 1, metadata.blocks)) return -108;
    uint8_t out[VOLUME_PB_SIZE];
    memcpy(out, input, sizeof(out));
    memset(out + 30, 0, sizeof(out) - 30);
    put16(out + 22, UINT16_MAX); /* The mounted application data volume. */
#ifdef __APPLE__
    put32(out + 30, mac_date(metadata.root.st_birthtimespec.tv_sec));
#endif
    put32(out + 34, mac_date(metadata.root.st_mtime));
    put16(out + 38, 0x8000); /* Software locked: guest writes are unsupported. */
    put16(out + 40, (uint16_t)metadata.files);
    put16(out + 46, (uint16_t)metadata.blocks);
    put32(out + 48, 512);
    put32(out + 52, 512);
    /* IDs 1 and 2 represent parent/root; reserve IDs 16.. for this snapshot
     * of children. No writable catalog or persistent child IDs are exposed. */
    put32(out + 58, 16u + metadata.files + metadata.directories);
    put16(out + 64, 0x4244); /* Logical HFS interface, not on-disk HFS storage. */
    put16(out + 66, 1); /* Matches the mounted application drive queue entry. */
    put32(out + 82, metadata.files);
    put32(out + 86, metadata.directories);
    memcpy(input, out, size);
    if (name) {
        ram[name] = sizeof(volume_name) - 1;
        memcpy(ram + name + 1, volume_name, sizeof(volume_name) - 1);
    }
    return 0;
}
int mac_volume_info(MacRuntime *rt, uint32_t pb, int directory_fd) { return volume_info(rt, pb, directory_fd, VOLUME_PB_SIZE); }
int mac_volume_info_basic(MacRuntime *rt, uint32_t pb, int directory_fd) { return volume_info(rt, pb, directory_fd, 64); }
