#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

int mac_resource_service(MacRuntime *rt, uint16_t opcode);

enum { DATA = 256, MAP = 264, TYPES = MAP + 28, REF = TYPES + 10,
       NAME = REF + 12, END = NAME + 5, SP = 0xf000, TEXT = 0x54455854 };
static int fails;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; \
} } while (0)

static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }

static int setup(MacRuntime *rt, uint8_t *p, unsigned attributes) {
    memset(rt, 0, sizeof *rt);
    memset(p, 0, END);
    put32(p, DATA); put32(p + 4, MAP); put32(p + 8, 8); put32(p + 12, END - MAP);
    put32(p + DATA, 4); memcpy(p + DATA + 4, "Test", 4);
    put16(p + MAP + 24, 28); put16(p + MAP + 26, NAME - MAP);
    put16(p + TYPES, 0); put32(p + TYPES + 2, TEXT);
    put16(p + TYPES + 6, 0); put16(p + TYPES + 8, 10);
    put16(p + REF, 128); put16(p + REF + 2, 0); p[REF + 4] = (uint8_t)attributes;
    p[NAME] = 4; memcpy(p + NAME + 1, "Name", 4);
    if (mac_resource_open(&rt->fork, p, END, NULL, 0)) return -1;
    rt->cpu = mac_cpu_create(65536);
    rt->heap_next = 0x2000; rt->heap_limit = 0x8000;
    return rt->cpu ? 0 : -1;
}

static void start(MacRuntime *rt) { mac_cpu_set(rt->cpu, MAC_A7, SP); }
static void word(MacRuntime *rt, uint32_t off, uint32_t value) { CHECK(mac_cpu_write(rt->cpu, SP + off, 2, value) == 0); }
static void lng(MacRuntime *rt, uint32_t off, uint32_t value) { CHECK(mac_cpu_write(rt->cpu, SP + off, 4, value) == 0); }
static int error(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0xa60, 2); }
static uint32_t get(MacRuntime *rt, uint16_t op, uint32_t type, int16_t id) {
    start(rt); word(rt, 0, (uint16_t)id); lng(rt, 2, type); lng(rt, 6, 0xdeadbeef);
    CHECK(mac_resource_service(rt, op) == 1);
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP + 6);
    return mac_cpu_read(rt->cpu, SP + 6, 4);
}

static uint32_t one(MacRuntime *rt, uint16_t op, uint32_t h, unsigned width) {
    start(rt); lng(rt, 0, h); lng(rt, 4, 0xdeadbeef);
    CHECK(mac_resource_service(rt, op) == 1);
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP + 4);
    return width ? mac_cpu_read(rt->cpu, SP + 4, width) : 0;
}

static uint32_t picture(MacRuntime *rt, int16_t id) {
    start(rt); word(rt,0,(uint16_t)id); lng(rt,2,0xdeadbeef);
    CHECK(mac_resource_service(rt,0xa9bc)==1);
    CHECK(mac_cpu_get(rt->cpu,MAC_A7)==SP+2);
    return mac_cpu_read(rt->cpu,SP+2,4);
}

static void pictures(void) {
    uint8_t bytes[END]; MacRuntime rt;
    CHECK(setup(&rt,bytes,0x20)==0);
    put32(bytes+TYPES+2,0x50494354); /* Authored synthetic PICT resource bytes. */
    uint32_t h=picture(&rt,128); CHECK(h && error(&rt)==0);
    CHECK(get(&rt,0xa9a0,0x50494354,128)==h);
    uint32_t old=mac_cpu_read(rt.cpu,h,4);
    CHECK(mac_cpu_read(rt.cpu,old,4)==0x54657374);
    CHECK(picture(&rt,128)==h); CHECK(mac_cpu_read(rt.cpu,h,4)==old);
    mac_cpu_write(rt.cpu,h,4,0); /* A purged resource retains its master handle. */
    CHECK(picture(&rt,128)==h && error(&rt)==0);
    uint32_t fresh=mac_cpu_read(rt.cpu,h,4); CHECK(fresh && fresh!=old);
    CHECK(mac_cpu_read(rt.cpu,fresh,4)==0x54657374);
    CHECK(picture(&rt,129)==0 && error(&rt)==-192);
    mac_cpu_write(rt.cpu,h,4,0); rt.heap_limit=rt.heap_next;
    CHECK(picture(&rt,128)==0 && error(&rt)==-108);
    CHECK(mac_cpu_read(rt.cpu,h,4)==0);
    mac_runtime_destroy(&rt);
    CHECK(setup(&rt,bytes,0)==0);
    CHECK(picture(&rt,128)==0 && error(&rt)==0); /* No PICT type matches GetResource. */
    mac_runtime_destroy(&rt);
    const uint32_t invalid[]={65532,0xffffffff,SP+1};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        CHECK(setup(&rt,bytes,0)==0); uint32_t heap=rt.heap_next;
        mac_cpu_set(rt.cpu,MAC_A7,invalid[i]);
        CHECK(mac_resource_service(&rt,0xa9bc)==-1 && mac_cpu_halted(rt.cpu));
        CHECK(rt.heap_next==heap && rt.handle_count==0);
        CHECK(mac_cpu_get(rt.cpu,MAC_A7)==invalid[i]);
        mac_runtime_destroy(&rt);
    }
}

static void normal(void) {
    uint8_t bytes[END]; MacRuntime rt;
    CHECK(setup(&rt, bytes, 0x24) == 0);
    uint32_t h = get(&rt, 0xa9a0, TEXT, 128);
    CHECK(h != 0 && error(&rt) == 0);
    uint32_t p = mac_cpu_read(rt.cpu, h, 4);
    CHECK(mac_cpu_read(rt.cpu, p, 4) == 0x54657374);
    CHECK(get(&rt, 0xa81f, TEXT, 128) == h);
    CHECK(one(&rt, 0xa9a5, h, 4) == 4 && error(&rt) == 0);
    CHECK(one(&rt, 0xa821, h, 4) == 4 && error(&rt) == 0);
    CHECK(one(&rt, 0xa9a6, h, 2) == 0x24 && error(&rt) == 0);
    CHECK(one(&rt, 0xa99c, TEXT, 2) == 1);
    CHECK(one(&rt, 0xa99c, 0x41414141, 2) == 0 && error(&rt) == 0);
    CHECK(one(&rt, 0xa80d, TEXT, 2) == 1 && one(&rt, 0xa80d, 0x41414141, 2) == 0 && error(&rt) == 0);   /* Count1Resources: the current file only (one file here) */
    CHECK(get(&rt, 0xa99d, TEXT, 1) == h);
    CHECK(get(&rt, 0xa99d, TEXT, 0) == 0 && error(&rt) == -192);
    CHECK(get(&rt, 0xa99d, TEXT, 2) == 0 && error(&rt) == -192);
    CHECK(get(&rt, 0xa9a0, TEXT, 129) == 0 && error(&rt) == -192);
    CHECK(get(&rt, 0xa9a0, 0x41414141, 128) == 0 && error(&rt) == 0);
    CHECK(one(&rt, 0xa9a5, 0, 4) == UINT32_MAX && error(&rt) == -192);
    CHECK(one(&rt, 0xa821, 0, 4) == UINT32_MAX && error(&rt) == -192);
    start(&rt); CHECK(mac_resource_service(&rt, 0xa9af) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP);
    CHECK((int16_t)mac_cpu_read(rt.cpu, SP, 2) == -192 && error(&rt) == -192);
    start(&rt); lng(&rt, 0, 0x1000); lng(&rt, 4, 0x1100); lng(&rt, 8, 0x1200); lng(&rt, 12, h);
    CHECK(mac_resource_service(&rt, 0xa9a8) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 16);
    CHECK(mac_cpu_read(rt.cpu, 0x1000, 1) == 4);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + 0x1001, "Name", 4) == 0);
    CHECK(mac_cpu_read(rt.cpu, 0x1100, 4) == TEXT);
    CHECK(mac_cpu_read(rt.cpu, 0x1200, 2) == 128);
    one(&rt, 0xa9a2, h, 0); CHECK(error(&rt) == 0 && mac_cpu_read(rt.cpu, h, 4) == p);
    mac_cpu_write(rt.cpu, h, 4, 0);
    one(&rt, 0xa9a2, h, 0); CHECK(error(&rt) == 0);
    CHECK(mac_cpu_read(rt.cpu, mac_cpu_read(rt.cpu, h, 4), 4) == 0x54657374);
    one(&rt, 0xa9aa, h, 0); CHECK(error(&rt) == 0);
    CHECK(rt.handles[0].resource_changed == 1);
    CHECK((one(&rt, 0xa9a6, h, 2) & 0x80) != 0);
    mac_cpu_write(rt.cpu, h, 4, 0);
    CHECK(get(&rt, 0xa9a0, TEXT, 128) == h); /* default automatic loading */
    CHECK(mac_cpu_read(rt.cpu, mac_cpu_read(rt.cpu, h, 4), 4) == 0x54657374);
    one(&rt, 0xa992, h, 0); CHECK(error(&rt) == 0);
    CHECK(mac_cpu_read(rt.cpu, mac_cpu_read(rt.cpu, h, 4), 4) == 0x54657374);
    CHECK(one(&rt, 0xa9a5, h, 4) == UINT32_MAX && error(&rt) == -192);
    uint32_t h2 = get(&rt, 0xa9a0, TEXT, 128); CHECK(h2 && h2 != h);
    one(&rt, 0xa9a3, h2, 0); CHECK(error(&rt) == 0);
    CHECK(mac_cpu_read(rt.cpu, h2, 4) == 0);
    CHECK(one(&rt, 0xa9a5, h2, 4) == UINT32_MAX && error(&rt) == -192);
    CHECK(get(&rt, 0xa9a0, TEXT, 128) != 0);
    start(&rt); CHECK(mac_resource_service(&rt, 0xa9a7) == 0); /* unsupported mutation */
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP);
    mac_runtime_destroy(&rt);
}

static void failures(void) {
    uint8_t bytes[END]; MacRuntime rt;
    CHECK(setup(&rt, bytes, 2) == 0);
    uint32_t h = get(&rt, 0xa9a0, TEXT, 128);
    uint32_t pointer = mac_cpu_read(rt.cpu, h, 4);
    one(&rt, 0xa9a3, h, 0); /* changed resources are retained */
    CHECK(error(&rt) == 0 && mac_cpu_read(rt.cpu, h, 4) == pointer);
    CHECK(one(&rt, 0xa9a5, h, 4) == 4);
    mac_runtime_destroy(&rt);
    CHECK(setup(&rt, bytes, 0) == 0);
    rt.heap_limit = rt.heap_next;
    CHECK(get(&rt, 0xa9a0, TEXT, 128) == 0 && error(&rt) == -108);
    mac_runtime_destroy(&rt);
    CHECK(setup(&rt, bytes, 0) == 0);
    mac_cpu_set(rt.cpu, MAC_A7, 65534);
    CHECK(mac_resource_service(&rt, 0xa9a0) == -1 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
    CHECK(setup(&rt, bytes, 0) == 0);
    h = get(&rt, 0xa9a0, TEXT, 128);
    start(&rt); lng(&rt, 0, 65535); lng(&rt, 4, 0x1100); lng(&rt, 8, 0x1200); lng(&rt, 12, h);
    CHECK(mac_resource_service(&rt, 0xa9a8) == -1 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

static void loaded_resource_guards(void) {
    MacRuntime rt; uint8_t bytes[END];
    CHECK(setup(&rt, bytes, 0) == 0);
    uint32_t h = mac_runtime_resource(&rt, TEXT, 128);
    CHECK(h != 0); rt.handles[0].loaded_code = 1;
    uint32_t p = rt.handles[0].pointer;
    start(&rt); mac_cpu_write(rt.cpu, SP, 4, h);
    CHECK(mac_resource_service(&rt, 0xa9a3) == -1 && mac_cpu_halted(rt.cpu));
    CHECK(rt.handles[0].pointer == p && rt.handles[0].resource == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP);
    mac_runtime_destroy(&rt);
    CHECK(setup(&rt, bytes, 0) == 0);
    h = mac_runtime_resource(&rt, TEXT, 128); rt.handles[0].loaded_code = 1;
    p = rt.handles[0].pointer;
    start(&rt); mac_cpu_write(rt.cpu, SP, 4, h);
    CHECK(mac_resource_service(&rt, 0xa992) == 1); /* Detach keeps the execution pin. */
    CHECK(!rt.handles[0].resource && rt.handles[0].loaded_code && rt.handles[0].pointer == p);
    mac_cpu_set(rt.cpu, MAC_A0, h);
    CHECK(mac_memory_service(&rt, 0xa023) == 1);
    CHECK((int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -117);
    CHECK(mac_cpu_read(rt.cpu, h, 4) == p);
    mac_runtime_destroy(&rt);
    CHECK(setup(&rt, bytes, 0) == 0);
    h = mac_runtime_resource(&rt, TEXT, 128); rt.handles[0].loaded_code = 1;
    mac_cpu_write(rt.cpu, h, 4, 0);
    start(&rt); mac_cpu_write(rt.cpu, SP, 4, h);
    CHECK(mac_resource_service(&rt, 0xa9a2) == -1 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

static void res_info(MacRuntime *rt, uint32_t h, uint32_t name_at) {
    start(rt); lng(rt, 0, name_at); lng(rt, 4, name_at + 0x100); lng(rt, 8, name_at + 0x110); lng(rt, 12, h);
    CHECK(mac_resource_service(rt, 0xa9a8) == 1);
}
static int set_info(MacRuntime *rt, uint32_t h, int16_t id, uint32_t name) {
    start(rt); lng(rt, 0, name); word(rt, 4, (uint16_t)id); lng(rt, 6, h);
    int served = mac_resource_service(rt, 0xa9a9);
    if (served == 1) CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP + 10);
    return served;
}

static void set_res_info(void) {
    uint8_t bytes[END]; MacRuntime rt;
    CHECK(setup(&rt, bytes, 0x20) == 0);
    uint32_t h = get(&rt, 0xa9a0, TEXT, 128); CHECK(h != 0);
    const uint8_t oz[] = {2, 'o', 'z'};
    memcpy(mac_cpu_ram(rt.cpu) + 0x1000, oz, sizeof oz);
    res_info(&rt, h, 0x1200);
    CHECK(mac_cpu_read(rt.cpu, 0x1200, 1) == 4 && memcmp(mac_cpu_ram(rt.cpu) + 0x1201, "Name", 4) == 0);
    CHECK(set_info(&rt, h, 128, 0x1000) == 1 && error(&rt) == 0);        /* rename at the same ID */
    res_info(&rt, h, 0x1200);
    CHECK(mac_cpu_read(rt.cpu, 0x1200, 1) == 2 && memcmp(mac_cpu_ram(rt.cpu) + 0x1201, "oz", 2) == 0);
    memset(mac_cpu_ram(rt.cpu) + 0x1000, 0xee, 3);                       /* guest edits its copy afterwards */
    res_info(&rt, h, 0x1200);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + 0x1201, "oz", 2) == 0);           /* stored name is unaffected */
    mac_cpu_ram(rt.cpu)[0x1000] = 0;                                     /* empty name clears to empty */
    CHECK(set_info(&rt, h, 128, 0x1000) == 1 && error(&rt) == 0);
    res_info(&rt, h, 0x1200); CHECK(mac_cpu_read(rt.cpu, 0x1200, 1) == 0);
    CHECK(set_info(&rt, 0x3000, 128, 0x1000) == 1 && error(&rt) == -192);   /* not a resource handle */
    CHECK(set_info(&rt, h, 129, 0x1000) == -1 && mac_cpu_halted(rt.cpu));    /* ID change: loud, not faked */
    mac_runtime_destroy(&rt);

    CHECK(setup(&rt, bytes, 0x28) == 0);                                  /* resProtected */
    h = get(&rt, 0xa9a0, TEXT, 128);
    CHECK(set_info(&rt, h, 128, 0x1000) == 1 && error(&rt) == -198);
    mac_runtime_destroy(&rt);

    CHECK(setup(&rt, bytes, 0x20) == 0);
    h = get(&rt, 0xa9a0, TEXT, 128);
    mac_cpu_write(rt.cpu, 0xff80, 1, 200);                                /* length runs past RAM */
    CHECK(set_info(&rt, h, 128, 0xff80) == -1);
    mac_runtime_destroy(&rt);
}

static int memory_call(MacRuntime *rt, uint16_t op, uint32_t a0, uint32_t d0) {
    mac_cpu_set(rt->cpu, MAC_A0, a0); mac_cpu_set(rt->cpu, MAC_D0, d0);
    return mac_memory_service(rt, op);
}

/* The rename made by SetResInfo must outlive ReleaseResource and a fresh GetResource. */
static void rename_survives_release(void) {
    uint8_t bytes[END]; MacRuntime rt;
    CHECK(setup(&rt, bytes, 0x20) == 0);
    uint32_t h = get(&rt, 0xa9a0, TEXT, 128); CHECK(h != 0);
    const uint8_t oz[] = {2, 'o', 'z'};
    memcpy(mac_cpu_ram(rt.cpu) + 0x1000, oz, sizeof oz);
    CHECK(set_info(&rt, h, 128, 0x1000) == 1 && error(&rt) == 0);
    one(&rt, 0xa9a3, h, 0); CHECK(error(&rt) == 0);                       /* ReleaseResource */
    CHECK(mac_cpu_read(rt.cpu, h, 4) == 0);
    uint32_t again = get(&rt, 0xa9a0, TEXT, 128); CHECK(again != 0 && error(&rt) == 0);
    res_info(&rt, again, 0x1200);
    CHECK(mac_cpu_read(rt.cpu, 0x1200, 1) == 2 && memcmp(mac_cpu_ram(rt.cpu) + 0x1201, "oz", 2) == 0);
    CHECK(memcmp(bytes + NAME + 1, "Name", 4) == 0);                      /* the fork's own bytes never change */
    mac_runtime_destroy(&rt);
}

static int write_res(MacRuntime *rt, uint32_t h) { return one(rt, 0xa9b0, h, 0), error(rt); }

static void write_resource(void) {
    uint8_t bytes[END], pristine[END]; MacRuntime rt;
    CHECK(setup(&rt, bytes, 0x20) == 0);
    memcpy(pristine, bytes, END);
    uint32_t h = get(&rt, 0xa9a0, TEXT, 128); CHECK(h != 0);
    uint32_t p = mac_cpu_read(rt.cpu, h, 4);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + p, "Test", 4) == 0);
    memcpy(mac_cpu_ram(rt.cpu) + p, "Edit", 4);                           /* same-size change */
    one(&rt, 0xa9aa, h, 0); CHECK(error(&rt) == 0 && (one(&rt, 0xa9a6, h, 2) & 0x80));   /* ChangedResource */
    CHECK(write_res(&rt, h) == 0);
    CHECK((one(&rt, 0xa9a6, h, 2) & 0x80) == 0);                          /* changed flag cleared */
    CHECK(memcmp(bytes, pristine, END) == 0);                             /* original fork untouched */
    one(&rt, 0xa9a3, h, 0);                                               /* release, then reload from the map */
    uint32_t h2 = get(&rt, 0xa9a0, TEXT, 128); CHECK(h2 != 0);
    p = mac_cpu_read(rt.cpu, h2, 4);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + p, "Edit", 4) == 0);

    /* Growing the handle first: the new size is what SizeResource reports after a reload. */
    CHECK(memory_call(&rt, 0xa024, h2, 10) == 1);
    p = mac_cpu_read(rt.cpu, h2, 4);
    memcpy(mac_cpu_ram(rt.cpu) + p, "Longer text", 10);
    CHECK(write_res(&rt, h2) == 0);
    one(&rt, 0xa9a3, h2, 0);
    uint32_t h3 = get(&rt, 0xa9a0, TEXT, 128);
    CHECK(one(&rt, 0xa9a5, h3, 4) == 10);
    CHECK(one(&rt, 0xa821, h3, 4) == 10);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + mac_cpu_read(rt.cpu, h3, 4), "Longer text", 10) == 0);
    CHECK(memcmp(bytes, pristine, END) == 0);

    /* Misuse is reported, not ignored. */
    CHECK(write_res(&rt, 0x3000) == -192);                                /* not a resource handle */
    mac_cpu_write(rt.cpu, h3, 4, 0);                                      /* purged: nothing to write */
    CHECK(write_res(&rt, h3) == -192);
    mac_runtime_destroy(&rt);

    CHECK(setup(&rt, bytes, 0x28) == 0);                                  /* resProtected */
    h = get(&rt, 0xa9a0, TEXT, 128);
    CHECK(write_res(&rt, h) == -198);
    mac_runtime_destroy(&rt);

    CHECK(setup(&rt, bytes, 0x20) == 0);
    h = get(&rt, 0xa9a0, TEXT, 128);
    rt.handles[0].loaded_code = 1;                                        /* live CODE must not be rewritten */
    CHECK(write_res(&rt, h) == -198);
    mac_runtime_destroy(&rt);
}

int main(void) {
    rename_survives_release();
    write_resource();
    set_res_info();
    pictures();
    loaded_resource_guards();
    normal(); failures();
    if (fails) return 1;
    puts("resource service checks ok");
    return 0;
}
