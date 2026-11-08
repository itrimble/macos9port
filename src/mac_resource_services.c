#include "mac_runtime.h"
#include "mac_resource_chain.h"

#include <stdio.h>
#include <string.h>

/* Independent Pascal ABI implementation for read-only resource forks.
 * Public interface/behavior references (no ROM or Apple implementation used):
 * https://leopard-adc.pepas.com/documentation/Carbon/Reference/Resource_Manager/Reference/reference.html
 * Trap IDs: The Programmer's Apple Mac Sourcebook (1989), section III.
 * No writeback, SetResLoad, or attribute mutation is claimed.
 */
enum { RES_ERROR = 0xa60, RES_NOT_FOUND = -192, MEM_FULL = -108 };

static int valid(MacCpu *cpu, uint32_t address, uint32_t size, int aligned) {
    if (address > mac_cpu_ram_size(cpu) || size > mac_cpu_ram_size(cpu) - address ||
        (aligned && (address & 1))) {
        mac_cpu_halt(cpu, "Invalid Resource Manager guest address.");
        return 0;
    }
    return 1;
}

static int result(MacRuntime *rt, uint32_t sp, unsigned args, unsigned width,
                   uint32_t value, int error) {
    if (width) mac_cpu_write(rt->cpu, sp + args, width, value);
    mac_cpu_write(rt->cpu, RES_ERROR, 2, (uint16_t)error);
    mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return mac_cpu_halted(rt->cpu) ? -1 : 1;
}

static MacHandle *handle(MacRuntime *rt, uint32_t value, MacResource *resource) {
    if (!value) return NULL;
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *h = &rt->handles[i];
        const MacResourceFork *fork=mac_resource_owner_fork(rt,h->resource_owner);
        if (h->handle == value && h->resource && fork &&
            mac_resource_find(fork, h->type, h->id, resource) == 0) return h;
    }
    return NULL;
}

static int load(MacRuntime *rt, MacHandle *h, const MacResource *r) {
    if (!valid(rt->cpu, h->handle, 4, 1)) return -1;
    if (h->loaded_code && mac_cpu_read(rt->cpu, h->handle, 4) != h->pointer) {
        mac_cpu_halt(rt->cpu, "Loaded CODE master pointer changed; safe reload is unsupported.");
        return -1;
    }
    if (mac_cpu_read(rt->cpu, h->handle, 4) == 0) {
        uint32_t pointer = mac_runtime_alloc_data(rt, (uint32_t)r->size);
        if (!pointer) return MEM_FULL;
        memcpy(mac_cpu_ram(rt->cpu) + pointer, r->data, r->size);
        h->pointer = pointer;
        h->size = (uint32_t)r->size;
        mac_cpu_write(rt->cpu, h->handle, 4, pointer);
    }
    return 0;
}

static int get_resource(MacRuntime *rt, uint32_t sp, int indexed, int picture, int one) {
    unsigned args = picture ? 2 : 6;
    int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp, 2);
    int16_t requested_id = id;
    uint32_t type = picture ? MAC_RESOURCE_TYPE('P', 'I', 'C', 'T') :
                             mac_cpu_read(rt->cpu, sp + 2, 4);
    MacResource r;
    int16_t owner=0;
    int found=indexed ? id>0 && !mac_resource_chain_at(rt,type,(unsigned)id,one,&r,&owner) :
                       !mac_resource_chain_find(rt,type,id,one,&r,&owner);
    if(found)id=r.id;
    if (rt->trace)
        fprintf(stderr, "resource-find type=%08x requested-id=%d found=%d actual-id=%d indexed=%d picture=%d one=%d owner=%d\n",
                type, requested_id, found, found ? id : requested_id,
                indexed, picture, one, owner);
    if (!found) {
        int error = indexed || mac_resource_chain_type_exists(rt, type, one) ? RES_NOT_FOUND : 0;
        return result(rt, sp, args, 4, 0, error);
    }
    uint32_t value = mac_runtime_resource_owned(rt, type, id, owner);
    if (!value) return result(rt, sp, args, 4, 0, MEM_FULL);
    MacHandle *h = handle(rt, value, &r);
    if (!h) return result(rt, sp, args, 4, 0, RES_NOT_FOUND);
    int error = load(rt, h, &r);
    if (error == -1) return -1;
    return result(rt, sp, args, 4, error ? 0 : value, error);
}

static int resource_info(MacRuntime *rt, uint32_t sp) {
    uint32_t name = mac_cpu_read(rt->cpu, sp, 4);
    uint32_t type = mac_cpu_read(rt->cpu, sp + 4, 4);
    uint32_t id = mac_cpu_read(rt->cpu, sp + 8, 4);
    uint32_t value = mac_cpu_read(rt->cpu, sp + 12, 4);
    MacResource r;
    MacHandle *h = handle(rt, value, &r);
    if (!h) return result(rt, sp, 16, 0, 0, RES_NOT_FOUND);
    const uint8_t *renamed = mac_resname_get(rt, h->handle); /* SetResInfo override wins over the fork's name */
    size_t name_size = renamed ? renamed[0] : r.name_size;
    const uint8_t *name_bytes = renamed ? renamed + 1 : r.name;
    if (!valid(rt->cpu, name, (uint32_t)name_size + 1, 0) ||
        !valid(rt->cpu, type, 4, 1) || !valid(rt->cpu, id, 2, 1)) return -1;
    if (h->resource_changed) r.attributes |= 0x80;
    mac_cpu_write(rt->cpu, id, 2, (uint16_t)r.id);
    mac_cpu_write(rt->cpu, type, 4, r.type);
    mac_cpu_write(rt->cpu, name, 1, (uint32_t)name_size);
    if (name_size) memcpy(mac_cpu_ram(rt->cpu) + name + 1, name_bytes, name_size);
    return result(rt, sp, 16, 0, 0, 0);
}

/* SetResInfo(theResource, theID, name). The name is stored; changing the ID is not supported
 * because resources are located by (type, id) in the immutable fork. */
static int set_resource_info(MacRuntime *rt, uint32_t sp) {
    uint32_t name = mac_cpu_read(rt->cpu, sp, 4);
    int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp + 4, 2);
    uint32_t value = mac_cpu_read(rt->cpu, sp + 6, 4);
    MacResource r;
    MacHandle *h = handle(rt, value, &r);
    if (!h) return result(rt, sp, 10, 0, 0, RES_NOT_FOUND);
    if (r.attributes & 0x08) return result(rt, sp, 10, 0, 0, -198); /* resProtected: resAttrErr */
    if (id != r.id) {
        mac_cpu_halt(rt->cpu, "SetResInfo changing a resource ID is unsupported.");
        return -1;
    }
    if (!valid(rt->cpu, name, 1, 0)) return -1;
    uint8_t pascal[256];
    pascal[0] = (uint8_t)mac_cpu_read(rt->cpu, name, 1);
    if (!valid(rt->cpu, name, (uint32_t)pascal[0] + 1, 0)) return -1;
    memcpy(pascal + 1, mac_cpu_ram(rt->cpu) + name + 1, pascal[0]);
    if (mac_resname_set(rt, h->handle, pascal)) return result(rt, sp, 10, 0, 0, MEM_FULL);
    return result(rt, sp, 10, 0, 0, 0);
}

int mac_resource_service(MacRuntime *rt, uint16_t opcode) {
    unsigned args = 0, width = 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    int file_result=mac_resource_file_service(rt,opcode);
    if(file_result)return file_result;
    switch (opcode) {
    case 0xa9a0: case 0xa81f: case 0xa99d: args = 6; width = 4; break;
    case 0xa9bc: args = 2; width = 4; break; /* GetPicture(picID): GetResource(PICT,id). */
    case 0xa9a5: args = 4; width = 4; break;
    case 0xa9a6: case 0xa99c: args = 4; width = 2; break;
    case 0xa9a2: case 0xa9a3: case 0xa992: args = 4; break;
    case 0xa9a8: args = 16; break;
    case 0xa9a9: args = 10; break;
    case 0xa9aa: args = 4; break;
    case 0xa9af: width = 2; break;
    default: return 0;
    }
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!valid(rt->cpu, sp, args + width, 1)) return -1;
    if (opcode == 0xa9a0 || opcode == 0xa81f || opcode == 0xa99d || opcode == 0xa9bc)
        return get_resource(rt, sp, opcode == 0xa99d, opcode == 0xa9bc, opcode == 0xa81f);
    if (opcode == 0xa9a8) return resource_info(rt, sp);
    if (opcode == 0xa9a9) return set_resource_info(rt, sp);
    if (opcode == 0xa9aa) {
        uint32_t value = mac_cpu_read(rt->cpu, sp, 4);
        MacResource r;
        MacHandle *h = handle(rt, value, &r);
        int error = h ? 0 : RES_NOT_FOUND;
        if (h && (r.attributes & 0x08)) error = -198;
        else if (h) h->resource_changed = 1;
        return result(rt, sp, 4, 0, 0, error);
    }
    if (opcode == 0xa9af) {
        uint32_t error = mac_cpu_read(rt->cpu, RES_ERROR, 2);
        return result(rt, sp, 0, 2, error, (int16_t)error);
    }
    uint32_t value = mac_cpu_read(rt->cpu, sp, 4);
    if (opcode == 0xa99c) {
        uint32_t count = mac_resource_chain_count(rt,value,0);
        return result(rt, sp, 4, 2, count, 0);
    }
    MacResource r;
    MacHandle *h = handle(rt, value, &r);
    if (!h) return result(rt, sp, args, width, UINT32_MAX, RES_NOT_FOUND);
    if (opcode == 0xa9a5) return result(rt, sp, 4, 4, (uint32_t)r.size, 0);
    if (opcode == 0xa9a6) return result(rt, sp, 4, 2,
        r.attributes | (h->resource_changed ? 0x80 : 0), 0);
    if (opcode == 0xa9a2) {
        int error = load(rt, h, &r);
        if (error == -1) return -1;
        return result(rt, sp, 4, 0, 0, error);
    }
    if (opcode == 0xa992) {
        h->resource = 0; /* Keep the memory handle and its bytes alive. */
        return result(rt, sp, 4, 0, 0, 0);
    }
    if (h->loaded_code) {
        mac_cpu_halt(rt->cpu, "ReleaseResource of loaded CODE requires UnloadSeg support.");
        return -1;
    }
    /* ReleaseResource must not discard a resource marked changed. The current
     * bump allocator cannot reclaim released storage yet; invalidate the handle
     * rather than expose it as a live resource. Exhaustion remains an error. */
    if (!(r.attributes & 2) && !h->resource_changed) {
        if (!valid(rt->cpu, h->handle, 4, 1)) return -1;
        mac_cpu_write(rt->cpu, h->handle, 4, 0);
        mac_runtime_free_data(rt, h->pointer); /* heap storage is reclaimed (and zeroed); bump storage is not */
        h->resource = 0;
        h->pointer = h->size = 0;
    }
    return result(rt, sp, 4, 0, 0, 0);
}
