#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "mac_resource_chain_int.h"
#include "mac_resfork_write.h"
#include "mac_files_int.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The write side of the Resource Manager, for resource forks a program opened or created in the save overlay.
 *
 * The model: a fork a program may change is kept as bytes in host memory (MacOpenResource.bytes). Every change loads those
 * bytes into an editable copy (mac_resfork_write.c), changes it, and builds new bytes, so the lookups the read side does
 * stay valid and nothing else needs to know a fork can change. Bytes reach the file only at WriteResource, UpdateResFile and
 * CloseResFile (a program that is stopped without them loses its changes, as on a real machine). The file is written to a
 * staging name and renamed over "<name>.rsrc" in the overlay. A fork that was found only in the read-only application
 * data is written to the overlay the first time (the data side is never touched), and the open fork then follows the copy.
 *
 * Errors, from the public Resource Manager interface: resNotFound -192 (not a resource handle), rfNumErr -193, addResFailed
 * -194, resAttrErr -198 (a protected resource), wPrErr -44 (the file cannot be written: no overlay, no sidecar name, or its
 * mapReadOnly attribute is set), dskFulErr -34, opWrErr -49 (the guest has the sidecar open for writing), ioErr -36.
 * The application's fork and the System file are read-only here: RmveResource, SetResFileAttrs on them answer wPrErr, while
 * AddResource, ChangedResource, WriteResource, SetResInfo and SetResAttrs on them keep their older behaviour (see the
 * service in mac_resource_services.c). Not claimed: the changed flag is per handle, and SetResAttrs/SetResInfo reach the
 * file at the next update whether or not ChangedResource was called (the file is written whole). The ID Unique1ID and
 * UniqueID give is the lowest unused one from 128, not a random one. */
enum { RES_ERROR = 0xa60, MAP_READ_ONLY = 0x80, RES_PROTECTED = 8, FIRST_UNIQUE = 128 };

static int range(MacRuntime *rt, uint32_t a, uint32_t n, int aligned) {
    MacCpu *c = rt->cpu;
    if (a > mac_cpu_ram_size(c) || n > mac_cpu_ram_size(c) - a || (aligned && (a & 1))) { mac_cpu_halt(c, "Invalid Resource Manager guest address."); return 0; }
    return 1;
}
static int done(MacRuntime *rt, uint32_t sp, unsigned args, unsigned width, uint32_t value, int error) {
    if (width) mac_cpu_write(rt->cpu, sp + args, width, value);
    mac_cpu_write(rt->cpu, RES_ERROR, 2, (uint16_t)error);
    mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return mac_cpu_halted(rt->cpu) ? -1 : 1;
}
static MacHandle *res_handle(MacRuntime *rt, uint32_t value) {
    for (size_t i = 0; value && i < rt->handle_count; i++)
        if (rt->handles[i].handle == value && rt->handles[i].resource) return &rt->handles[i];
    return NULL;
}
static MacHandle *plain_handle(MacRuntime *rt, uint32_t value) {
    for (size_t i = 0; value && i < rt->handle_count; i++)
        if (rt->handles[i].handle == value && !rt->handles[i].resource && rt->handles[i].pointer) return &rt->handles[i];
    return NULL;
}
static uint16_t file_attrs(const MacResourceFork *f) { return (uint16_t)((f->bytes[f->map_offset + 22] << 8) | f->bytes[f->map_offset + 23]); }

/* May this fork be changed? One in the overlay, or one in the data that has an overlay to be copied into. */
static int can_write(const MacRuntime *rt, const MacOpenResource *f) {
    const FileState *fs = rt->file_state;
    return f->sidecar[0] && (f->writable || (fs && fs->saves >= 0)) && !(file_attrs(&f->fork) & MAP_READ_ONLY);
}
static int rebuild(MacOpenResource *f, const MacWFork *w) {
    uint8_t *bytes; size_t size; MacResourceFork fork;
    int error = mac_wfork_build(w, &bytes, &size);
    if (error) return error == -199 ? -194 : error;
    if (mac_resource_open(&fork, bytes, size, NULL, 0)) { free(bytes); return -194; }
    free(f->bytes); f->bytes = bytes; f->fork = fork; f->dirty = 1;
    return 0;
}
static int load_model(MacOpenResource *f, MacWFork *w) { int e = mac_wfork_load(w, &f->fork); return e == -199 ? -194 : e; }

/* Copy the data of the changed handles of this file (or just `only`) into the model. */
static int pull_changed(MacRuntime *rt, const MacOpenResource *f, MacWFork *w, uint32_t only) {
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *h = &rt->handles[i];
        if (!h->resource || h->resource_owner != f->ref || !h->resource_changed || (only && h->handle != only)) continue;
        long at = mac_wfork_find(w, h->type, h->id);
        uint32_t p = mac_cpu_read(rt->cpu, h->handle, 4);
        if (at < 0 || !p || h->loaded_code || !range(rt, p, h->size, 0)) continue;
        if (mac_wfork_set_data(w, (size_t)at, mac_cpu_ram(rt->cpu) + p, h->size)) return -108;
        h->resource_changed = 0;
    }
    return 0;
}

/* Write the fork's bytes over "<name>.rsrc" in the overlay: quotas first, then a staging file, then one rename. */
static int put_overlay(MacRuntime *rt, MacOpenResource *f) {
    FileState *s = rt->file_state;
    if (!s || s->saves < 0 || !f->sidecar[0]) return -44;
    GuestFile *open = mac_files_open_by_name(s, f->sidecar);
    if (open && open->writable) return -49;
    char dir[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(f->sidecar, dir, &leaf);
    if (dir[0]) { int e = mac_dirs_ensure_overlay(s, dir); if (e) return e == -120 ? -43 : e; }
    struct stat st;
    int exists = !mac_dirs_stat(s->saves, f->sidecar, &st) && S_ISREG(st.st_mode);
    uint64_t old = exists ? (uint64_t)st.st_size : 0, size = f->fork.size, growth = size > old ? size - old : 0;
    unsigned files; uint64_t bytes;
    int e = mac_files_usage(s, &files, &bytes);
    if (e) return e;
    if ((!exists && files >= s->max_files) || bytes + growth > s->max_bytes || (growth && volume_budget_spend(&s->disk, growth))) return -34;
    char stage[DIRS_PATH_MAX + 16];
    if (mac_files_stage_name(f->sidecar, stage, sizeof stage)) return -50;
    int fd = mac_dirs_openat(s->saves, stage, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno == EEXIST && !mac_dirs_unlink(s->saves, stage)) fd = mac_dirs_openat(s->saves, stage, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -36;
    for (size_t at = 0; at < f->fork.size;) {
        ssize_t put = write(fd, f->fork.bytes + at, f->fork.size - at);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) { close(fd); mac_dirs_unlink(s->saves, stage); return -36; }
        at += (size_t)put;
    }
    if (fsync(fd) || close(fd) || mac_dirs_rename(s->saves, stage, f->sidecar)) { mac_dirs_unlink(s->saves, stage); return -36; }
    fsync(s->saves);
    if (!mac_dirs_stat(s->saves, f->sidecar, &st)) { f->device = st.st_dev; f->inode = st.st_ino; }   /* a second OpenResFile finds this fork again */
    f->writable = 1; f->dirty = 0;
    return 0;
}

int mac_resource_flush_file(MacRuntime *rt, MacOpenResource *f) {
    int changed = 0;
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].resource && rt->handles[i].resource_owner == f->ref && rt->handles[i].resource_changed) changed = 1;
    if (changed) {
        MacWFork w; int e = load_model(f, &w);
        if (e) return e;
        e = pull_changed(rt, f, &w, 0);
        if (!e) e = rebuild(f, &w);
        mac_wfork_free(&w);
        if (e) return e;
    }
    return f->dirty ? put_overlay(rt, f) : 0;
}

/* A resource handle in a fork we may change: the file, the model with its entry, and the entry's index. */
typedef struct { MacOpenResource *f; MacHandle *h; MacWFork w; long at; int loaded; } Target;
static void release(Target *t) { if (t->loaded) mac_wfork_free(&t->w); t->loaded = 0; }
/* 1: not one of ours (the older code serves it); 0: ready; <0: the error to answer. */
static int target(MacRuntime *rt, uint32_t value, Target *t, int need_unprotected) {
    memset(t, 0, sizeof *t);
    t->h = res_handle(rt, value);
    if (!t->h || !t->h->resource_owner) return 1;
    t->f = mac_resource_file_of(rt, t->h->resource_owner);
    if (!t->f) return 1;
    if (!can_write(rt, t->f)) return -44;
    int e = load_model(t->f, &t->w);
    if (e) return e;
    t->loaded = 1;
    t->at = mac_wfork_find(&t->w, t->h->type, t->h->id);
    if (t->at < 0) { release(t); return -192; }
    if (need_unprotected && (t->w.res[t->at].attrs & RES_PROTECTED)) { release(t); return -198; }
    return 0;
}
static int read_name(MacRuntime *rt, uint32_t at, uint8_t pascal[256]) {
    if (!range(rt, at, 1, 0)) return -1;
    pascal[0] = (uint8_t)mac_cpu_read(rt->cpu, at, 1);
    if (!range(rt, at, (uint32_t)pascal[0] + 1, 0)) return -1;
    memcpy(pascal + 1, mac_cpu_ram(rt->cpu) + at + 1, pascal[0]);
    return 0;
}

static int add_resource(MacRuntime *rt, uint32_t sp) {
    uint32_t name = mac_cpu_read(rt->cpu, sp, 4), type = mac_cpu_read(rt->cpu, sp + 6, 4), value = mac_cpu_read(rt->cpu, sp + 10, 4);
    int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp + 4, 2);
    struct MacResourceChain *c = rt->resource_chain;
    MacOpenResource *f = c ? mac_resource_file_of(rt, c->current) : NULL;
    if (!f) return 0;   /* the application's fork or the System file is current: accepted and dropped, as before */
    MacHandle *h = plain_handle(rt, value);
    if (!h) return done(rt, sp, 14, 0, 0, -194);   /* nil, not a handle we made, or already a resource's handle */
    if (!can_write(rt, f)) return done(rt, sp, 14, 0, 0, -44);
    uint8_t pascal[256];
    if (name && read_name(rt, name, pascal)) return -1;
    uint32_t p = mac_cpu_read(rt->cpu, value, 4), size = p ? h->size : 0;
    if (size && !range(rt, p, size, 0)) return -1;
    MacWFork w; int e = load_model(f, &w);
    if (!e) e = mac_wfork_add(&w, type, id, 0, name && pascal[0] ? pascal : NULL, p ? mac_cpu_ram(rt->cpu) + p : NULL, size);
    if (!e) e = rebuild(f, &w);
    mac_wfork_free(&w);
    if (e) return done(rt, sp, 14, 0, 0, e);
    h->resource = 1; h->resource_owner = f->ref; h->type = type; h->id = id;
    h->resource_changed = 1;   /* the handle's contents at the next update are the resource's data */
    return done(rt, sp, 14, 0, 0, 0);
}

static int remove_resource(MacRuntime *rt, uint32_t sp) {
    uint32_t value = mac_cpu_read(rt->cpu, sp, 4);
    MacHandle *h = res_handle(rt, value);
    if (!h) return done(rt, sp, 4, 0, 0, -192);
    Target t; int e = target(rt, value, &t, 1);
    if (e == 1) return done(rt, sp, 4, 0, 0, -44);   /* the application's own fork: read-only */
    if (!e) {
        mac_wfork_remove(&t.w, (size_t)t.at);
        e = rebuild(t.f, &t.w);
        release(&t);
        if (!e) { h->resource = 0; h->resource_owner = 0; h->resource_changed = 0; h->type = 0; h->id = 0; }   /* an ordinary handle now */
    }
    return done(rt, sp, 4, 0, 0, e);
}

static int changed_resource(MacRuntime *rt, uint32_t sp) {
    Target t; int e = target(rt, mac_cpu_read(rt->cpu, sp, 4), &t, 1);
    if (e == 1) return 0;
    if (!e) { t.h->resource_changed = 1; release(&t); }
    return done(rt, sp, 4, 0, 0, e);
}

static int write_resource(MacRuntime *rt, uint32_t sp) {
    uint32_t value = mac_cpu_read(rt->cpu, sp, 4);
    Target t; int e = target(rt, value, &t, 1);
    if (e == 1) return 0;
    if (!e) {
        uint32_t p = mac_cpu_read(rt->cpu, value, 4);
        if (!p || t.h->loaded_code) e = p ? -198 : -192;   /* purged, or live CODE */
        else if (!range(rt, p, t.h->size, 0)) { release(&t); return -1; }
        else if (!(e = mac_wfork_set_data(&t.w, (size_t)t.at, mac_cpu_ram(rt->cpu) + p, t.h->size))) e = rebuild(t.f, &t.w);
        if (!e) { t.h->resource_changed = 0; e = put_overlay(rt, t.f); }
        release(&t);
    }
    return done(rt, sp, 4, 0, 0, e);
}

static int set_resource_info(MacRuntime *rt, uint32_t sp) {
    uint32_t name = mac_cpu_read(rt->cpu, sp, 4);
    int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp + 4, 2);
    Target t; int e = target(rt, mac_cpu_read(rt->cpu, sp + 6, 4), &t, 1);
    if (e == 1) return 0;
    if (e) return done(rt, sp, 10, 0, 0, e);
    uint8_t pascal[256];
    if (name && read_name(rt, name, pascal)) { release(&t); return -1; }
    if (id != t.h->id && mac_wfork_find(&t.w, t.h->type, id) >= 0) e = -194;   /* the ID is taken */
    else {
        t.w.res[t.at].id = id;
        if (name) e = mac_wfork_set_name(&t.w, (size_t)t.at, pascal[0] ? pascal : NULL);   /* a nil name leaves it, an empty one clears it */
        if (!e) e = rebuild(t.f, &t.w);
        if (!e) t.h->id = id;
    }
    release(&t);
    return done(rt, sp, 10, 0, 0, e);
}

static int set_res_attrs(MacRuntime *rt, uint32_t sp) {
    uint16_t attrs = (uint16_t)mac_cpu_read(rt->cpu, sp, 2);
    Target t; int e = target(rt, mac_cpu_read(rt->cpu, sp + 2, 4), &t, 0);
    if (e == 1) return 0;
    if (!e) {
        t.w.res[t.at].attrs = (uint8_t)(attrs & ~2u);   /* resChanged is the Resource Manager's, not the caller's */
        e = rebuild(t.f, &t.w);
        release(&t);
    }
    return done(rt, sp, 6, 0, 0, e);
}

static int update_file(MacRuntime *rt, uint32_t sp) {
    int16_t ref = (int16_t)mac_cpu_read(rt->cpu, sp, 2);
    MacOpenResource *f = mac_resource_file_of(rt, ref);
    if (f) return done(rt, sp, 2, 0, 0, mac_resource_flush_file(rt, f));
    if (ref == 0 || ref == MAC_APP_RES_REF || ref == MAC_SYSTEM_RES_REF) return 0;
    return done(rt, sp, 2, 0, 0, -193);
}

static int file_attributes(MacRuntime *rt, uint32_t sp, int set) {
    unsigned args = set ? 4 : 2;
    int16_t ref = (int16_t)mac_cpu_read(rt->cpu, sp + (set ? 2 : 0), 2);
    MacOpenResource *f = mac_resource_file_of(rt, ref);
    int app = ref == MAC_APP_RES_REF || ref == MAC_SYSTEM_RES_REF || ref == 0;
    if (!f && !app) return done(rt, sp, args, set ? 0 : 2, 0, -193);
    if (!set) return done(rt, sp, args, 2, f ? file_attrs(&f->fork) : ref == MAC_APP_RES_REF ? file_attrs(&rt->fork) : 0, 0);
    const FileState *fs = rt->file_state;
    if (!f || !f->sidecar[0] || !(f->writable || (fs && fs->saves >= 0))) return done(rt, sp, args, 0, 0, -44);
    MacWFork w; int e = load_model(f, &w);
    if (!e) { w.map_attrs = (uint16_t)mac_cpu_read(rt->cpu, sp, 2); e = rebuild(f, &w); }
    mac_wfork_free(&w);
    return done(rt, sp, args, 0, 0, e);
}

/* The lowest ID from 128 that no resource of the type has: in every open file (UniqueID), or the current one (Unique1ID). */
static int unique_id(MacRuntime *rt, uint32_t sp, int one) {
    uint32_t type = mac_cpu_read(rt->cpu, sp, 4);
    struct MacResourceChain *c = rt->resource_chain;
    const MacResourceFork *forks[MAC_FORK_LIMIT + 1]; unsigned n = 0;
    int16_t current = c ? c->current : MAC_APP_RES_REF;
    if (!one || current == MAC_APP_RES_REF) forks[n++] = &rt->fork;
    for (unsigned i = 0; c && i < MAC_FORK_LIMIT; i++)
        if (c->files[i].bytes && (!one || c->files[i].ref == current)) forks[n++] = &c->files[i].fork;
    for (int32_t id = FIRST_UNIQUE; id <= INT16_MAX; id++) {
        MacResource r; unsigned used = 0;
        for (unsigned i = 0; i < n; i++) if (!mac_resource_find(forks[i], type, (int16_t)id, &r)) used = 1;
        if (!used) return done(rt, sp, 4, 2, (uint32_t)id, 0);
    }
    return done(rt, sp, 4, 2, 0, -194);
}

static int mac_resource_write_one(MacRuntime *rt, uint16_t opcode, uint32_t sp);
int mac_resource_write_service(MacRuntime *rt, uint16_t opcode) {
    unsigned args = 0;
    switch (opcode) {
    case 0xa9ab: args = 14; break;
    case 0xa9ad: case 0xa9aa: case 0xa9b0: case 0xa9c1: case 0xa810: args = 4; break;
    case 0xa9a9: args = 10; break;
    case 0xa9a7: args = 6; break;
    case 0xa999: case 0xa9f6: case 0xa993: args = 2; break;
    case 0xa9f7: args = 4; break;
    default: return 0;
    }
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!range(rt, sp, args + ((opcode == 0xa9f6 || opcode == 0xa9c1 || opcode == 0xa810) ? 2 : 0), 1)) return -1;
    int served = mac_resource_write_one(rt, opcode, sp);
    if (rt->trace && served) fprintf(stderr, "resource-write op=%04x error=%d\n", opcode, (int16_t)mac_cpu_read(rt->cpu, RES_ERROR, 2));
    return served;
}
static int mac_resource_write_one(MacRuntime *rt, uint16_t opcode, uint32_t sp) {
    switch (opcode) {
    case 0xa9ab: return add_resource(rt, sp);
    case 0xa9ad: return remove_resource(rt, sp);
    case 0xa9aa: return changed_resource(rt, sp);
    case 0xa9b0: return write_resource(rt, sp);
    case 0xa9a9: return set_resource_info(rt, sp);
    case 0xa9a7: return set_res_attrs(rt, sp);
    case 0xa999: return update_file(rt, sp);
    case 0xa9f6: return file_attributes(rt, sp, 0);
    case 0xa9f7: return file_attributes(rt, sp, 1);
    case 0xa9c1: return unique_id(rt, sp, 0);
    case 0xa810: return unique_id(rt, sp, 1);
    default: {   /* SetResPurge */
        struct MacResourceChain *c = mac_resource_chain_ensure(rt);
        if (c) c->purge_install = mac_cpu_read(rt->cpu, sp, 1) != 0;
        return done(rt, sp, 2, 0, 0, c ? 0 : -108);
    }
    }
}
