#include "native_apps.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { int app; const char *path, *marker; } NativeEntry;
static const NativeEntry entries[] = {
    {NATIVE_TERMINAL,"Applications/Terminal","OS999-BUILTIN:terminal:1\n"},
    {NATIVE_TAILSCALE,"Applications/Tailscale","OS999-BUILTIN:tailscale:1\n"},
    {NATIVE_CONSOLE,"Applications/OS999 Console","OS999-BUILTIN:console:1\n"},
    {NATIVE_BROWSER,"Applications/Web Browser","OS999-BUILTIN:browser:1\n"},
    {NATIVE_MUSIC,"Applications/OS999 Amp","OS999-BUILTIN:music:1\n"},
    {NATIVE_MOVIE,"Applications/Movie Player","OS999-BUILTIN:movie:1\n"},
    {NATIVE_NOTES,"Applications/Notes","OS999-BUILTIN:notes:1\n"},
    {NATIVE_CONTROLS,"Applications/Control Panels","OS999-BUILTIN:controls:1\n"},
    {NATIVE_QUAKE,"Applications/Quake III Arena","OS999-BUILTIN:quake:1\n"},
    {NATIVE_TERMINAL,"Applications/Utilities/Terminal","OS999-BUILTIN:terminal:1\n"},
    {NATIVE_TAILSCALE,"Applications/Utilities/Tailscale","OS999-BUILTIN:tailscale:1\n"}
};
static int parent_fd(const char *root, const char *rel, const char **leaf) {
    if (!root || !rel || strncmp(rel,"Applications/",13)) return -1;
    int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW); if(dir<0) return -1;
    int next=openat(dir,"Applications",O_RDONLY|O_DIRECTORY|O_NOFOLLOW); close(dir); dir=next;
    const char *name=rel+13;
    if(!strncmp(name,"Utilities/",10)) {
        next=dir<0?-1:openat(dir,"Utilities",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(dir>=0) close(dir);
        dir=next; name+=10;
    }
    if(!name[0] || strchr(name,'/')) { if(dir>=0) close(dir); return -1; }
    *leaf=name; return dir;
}
static int exact_file(const char *root,const char *rel,const char *body) {
    const char *leaf; int dir=parent_fd(root,rel,&leaf); if(dir<0) return 0;
    int fd=openat(dir,leaf,O_RDONLY|O_NONBLOCK|O_NOFOLLOW); close(dir);
    if(fd<0) return 0;
    struct stat st; char buffer[512]; size_t length=strlen(body);
    int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)length&&length<sizeof buffer;
    if(valid) valid=read(fd,buffer,length)==(ssize_t)length&&!memcmp(buffer,body,length);
    close(fd); return valid;
}
int native_catalog_id(const char *root,const char *rel) {
    if(!rel) return NATIVE_NONE;
    for(size_t i=0;i<sizeof entries/sizeof *entries;i++)
        if(!strcmp(rel,entries[i].path)&&exact_file(root,rel,entries[i].marker)) return entries[i].app;
    return NATIVE_NONE;
}
const char *native_catalog_path(int app) {
    for(size_t i=0;i<sizeof entries/sizeof *entries;i++) if(entries[i].app==app) return entries[i].path;
    return NULL;
}
int native_catalog_seed(const char *root) {
    for(size_t i=0;i<sizeof entries/sizeof *entries;i++) {
        const char *leaf; int dir=parent_fd(root,entries[i].path,&leaf); if(dir<0) return -1;
        int fd=openat(dir,leaf,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600); int saved_errno=errno; close(dir);
        if(fd<0) { if(saved_errno==EEXIST) continue; return -1; }
        size_t n=strlen(entries[i].marker);
        int failed=write(fd,entries[i].marker,n)!=(ssize_t)n;
        if(close(fd)) failed=1;
        if(failed) return -1;
    }
    return 0;
}
int native_catalog_placeholder(const char *root,const char *rel) {
    return rel&&!strcmp(rel,"Applications/Nothing Installed Yet")&&exact_file(root,rel,
        "No classic applications are installed.\n\nCopy a supported 68k app here and\n"
        "double-click it to try OS999's own\nruntime. Support is incomplete. Some\n"
        "archive formats use a legacy launcher.\n");
}

/* ---- Sample startup extensions --------------------------------------------
 * Small INITs written for OS999 (every byte is authored here) so the
 * Extensions Manager has something to list, load, fail and order. They are
 * placed once per volume; deleting them does not bring them back. */
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put32(unsigned char *p, unsigned long v) { put16(p, (unsigned)(v >> 16)); put16(p + 2, (unsigned)(v & 0xffff)); }

static int write_new(const char *path, const void *bytes, size_t n) {
    FILE *f = fopen(path, "wbx");
    if (!f) return -1;
    int ok = fwrite(bytes, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* A resource fork holding one INIT resource, id 0. */
static int sample_init(const char *dir, const char *name, const unsigned char *code, size_t n, const char *info) {
    enum { DATA = 256, MAPLEN = 50 };
    unsigned char fork[DATA + 64 + MAPLEN] = {0};
    size_t datalen = 4 + ((n + 1) & ~(size_t)1), map = DATA + datalen;
    char path[OSPI_PATH];
    if (n > 56 || snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) return -1;
    put32(fork, DATA); put32(fork + 4, (unsigned long)map); put32(fork + 8, (unsigned long)datalen); put32(fork + 12, MAPLEN);
    put32(fork + DATA, (unsigned long)n); memcpy(fork + DATA + 4, code, n);
    put16(fork + map + 24, 28); put16(fork + map + 26, MAPLEN);
    put16(fork + map + 28, 0); memcpy(fork + map + 30, "INIT", 4);
    put16(fork + map + 34, 0); put16(fork + map + 36, 10);
    put16(fork + map + 38, 0); put16(fork + map + 40, 0xffff);
    if (write_new(path, fork, map + MAPLEN)) return -1;
    if (!info) return 0;
    if (snprintf(path, sizeof path, "%s/%s.extinfo", dir, name) >= (int)sizeof path) return -1;
    return write_new(path, info, strlen(info));
}

int native_catalog_seed_sample_extensions(const char *root) {
    static const unsigned char ok[] = {0x4e, 0x75};                              /* RTS */
    /* MOVE.L #'os99',D0 ; SUBA.L A0,A0 ; NewGestalt ; RTS */
    static const unsigned char gestalt[] = {0x20, 0x3c, 'o', 's', '9', '9', 0x91, 0xc8, 0xa3, 0xad, 0x4e, 0x75};
    static const unsigned char faulty[] = {0xa8, 0x9d, 0x4e, 0x75};              /* a trap OS999 does not provide */
    char marker[OSPI_PATH], dir[OSPI_PATH];
    if (volume_join(marker, sizeof marker, root, ".os999-sample-extensions") != 0 ||
        volume_join(dir, sizeof dir, root, "System Folder/Extensions") != 0) return -1;
    if (access(marker, F_OK) == 0) return 0;
    if (write_new(marker, "1\n", 2)) return -1;
    (void)sample_init(dir, "Sample Hello", ok, sizeof ok, NULL);
    (void)sample_init(dir, "Sample Gestalt", gestalt, sizeof gestalt, "priority 120\n");
    (void)sample_init(dir, "Sample Needs Hello", ok, sizeof ok, "priority 150\nrequires Sample Hello\n");
    (void)sample_init(dir, "Sample Faulty", faulty, sizeof faulty, "priority 200\n");
    return 0;
}
