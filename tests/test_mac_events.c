/* Independently authored synthetic Event Manager ABI checks. No guest assets. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define SP 0xe000u
#define RECORD 0x1000u
#define RAM 0x10000u

static MacRuntime fresh(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(RAM);
    CHECK(rt.cpu != NULL);
    mac_cpu_set(rt.cpu, MAC_A7, SP);
    rt.modifiers = 0x80;
    return rt;
}

static uint32_t peek(MacRuntime *rt, uint32_t at, unsigned n) {
    CHECK(at <= RAM && n <= RAM - at);
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v = v * 256 + mac_cpu_ram(rt->cpu)[at + i];
    return v;
}

static void frame(MacRuntime *rt, uint16_t opcode, uint16_t mask, uint32_t ptr) {
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    unsigned args = opcode == 0xa860 ? 14 : 6;
    memset(mac_cpu_ram(rt->cpu) + SP, 0, 16);
    mac_cpu_write(rt->cpu, SP + args - 6, 4, ptr);
    mac_cpu_write(rt->cpu, SP + args - 2, 2, mask);
    mac_cpu_write(rt->cpu, SP + args, 2, 0x55aa);
}

static void record_layout_and_queue(void) {
    MacRuntime rt = fresh();
    rt.ticks = 0x12345678;
    rt.event_head = 63; /* Exercise removal across the ring wrap. */
    mac_runtime_post_event(&rt, 1, 0x11111111, 0, 11, 22);
    mac_runtime_post_event(&rt, 3, 0x87654321, 0x0380, -123, -456);
    mac_runtime_post_event(&rt, 2, 0x22222222, 0x80, 33, 44);
    memset(mac_cpu_ram(rt.cpu) + RECORD - 2, 0xcc, 20);
    frame(&rt, 0xa971, 8, RECORD); /* EventAvail must preserve all events. */
    CHECK(mac_event_service(&rt, 0xa971) == 1);
    CHECK(rt.event_count == 3 && mac_cpu_get(rt.cpu, MAC_A7) == SP + 6);
    CHECK(peek(&rt, SP + 6, 2) == 0x01aa);
    CHECK(peek(&rt, RECORD, 2) == 3);
    CHECK(peek(&rt, RECORD + 2, 4) == 0x87654321);
    CHECK(peek(&rt, RECORD + 6, 4) == 0x12345678);
    CHECK(peek(&rt, RECORD + 10, 2) == (uint16_t)-456);
    CHECK(peek(&rt, RECORD + 12, 2) == (uint16_t)-123);
    CHECK(peek(&rt, RECORD + 14, 2) == 0x0380);
    CHECK(peek(&rt, RECORD - 2, 2) == 0xcccc && peek(&rt, RECORD + 16, 2) == 0xcccc);
    frame(&rt, 0xa970, 8, RECORD);
    CHECK(mac_event_service(&rt, 0xa970) == 1 && rt.event_count == 2);
    CHECK(rt.events[63].what == 1 && rt.events[0].what == 2);
    frame(&rt, 0xa860, 4, RECORD); /* Zero sleep and NULL mouse region. */
    CHECK(mac_event_service(&rt, 0xa860) == 1 && rt.event_count == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 14);
    CHECK(peek(&rt, SP + 14, 2) == 0x01aa && peek(&rt, RECORD, 2) == 2);
    rt.ticks++;
    frame(&rt, 0xa970, 8, RECORD);
    CHECK(mac_event_service(&rt, 0xa970) == 1 && rt.event_count == 1);
    CHECK(peek(&rt, SP + 6, 2) == 0x00aa);
    CHECK(peek(&rt, RECORD, 2) == 0 && peek(&rt, RECORD + 2, 4) == 0);
    CHECK(peek(&rt, RECORD + 6, 4) == rt.ticks);
    CHECK(peek(&rt, RECORD + 10, 2) == 44 && peek(&rt, RECORD + 12, 2) == 33);
    mac_cpu_destroy(rt.cpu);
}

static void point_ticks_button(void) {
    MacRuntime rt = fresh();
    rt.mouse_x = -10; rt.mouse_y = 200; rt.ticks = 0xfedcba98;
    mac_cpu_write(rt.cpu, SP, 4, RECORD);
    CHECK(mac_event_service(&rt, 0xa972) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 4);
    CHECK(peek(&rt, RECORD, 2) == 200 && peek(&rt, RECORD + 2, 2) == (uint16_t)-10);
    mac_cpu_set(rt.cpu, MAC_A7, SP);
    CHECK(mac_event_service(&rt, 0xa975) == 1 && peek(&rt, SP, 4) == rt.ticks);
    mac_cpu_write(rt.cpu, SP, 2, 0x55aa);
    CHECK(mac_event_service(&rt, 0xa974) == 1 && peek(&rt, SP, 2) == 0x00aa);
    rt.modifiers = 0;
    CHECK(mac_event_service(&rt, 0xa974) == 1 && peek(&rt, SP, 2) == 0x01aa);
    CHECK(mac_event_service(&rt, 0xafff) == 0);
    CHECK(mac_event_service(&rt, 0xa973) == 0); /* StillDown queue semantics unsupported. */
    CHECK(mac_event_service(&rt, 0xa977) == 0); /* WaitMouseUp queue semantics unsupported. */
    mac_cpu_destroy(rt.cpu);
}

static void flush_stop_mask(void) {
    MacRuntime rt = fresh();
    rt.event_head = 62;
    mac_runtime_post_event(&rt, 1, 11, 0, 0, 0);
    mac_runtime_post_event(&rt, 3, 22, 0, 0, 0);
    mac_runtime_post_event(&rt, 2, 33, 0, 0, 0);
    mac_runtime_post_event(&rt, 1, 44, 0, 0, 0);
    mac_cpu_set(rt.cpu, MAC_D0, (8u << 16) | 0xffff);
    mac_cpu_set(rt.cpu, MAC_D1, 0); /* D1 is unrelated to FlushEvents ABI. */
    CHECK(mac_event_service(&rt, 0xa032) == 1);
    CHECK(rt.event_count == 3 && rt.events[62].what == 3);
    CHECK(rt.events[63].message == 33 && rt.events[0].message == 44);
    CHECK((mac_cpu_get(rt.cpu, MAC_D0) & 0xffff) == 3);
    mac_cpu_set(rt.cpu, MAC_D0, 4);
    CHECK(mac_event_service(&rt, 0xa032) == 1 && rt.event_count == 2);
    CHECK(rt.events[62].what == 3 && rt.events[63].what == 1);
    mac_cpu_set(rt.cpu, MAC_D0, 0xffff);
    CHECK(mac_event_service(&rt, 0xa032) == 1 && rt.event_count == 0);
    CHECK((mac_cpu_get(rt.cpu, MAC_D0) & 0xffff) == 0);
    mac_cpu_destroy(rt.cpu);
}

static void bad_record_preserves_queue(uint32_t ptr) {
    MacRuntime rt = fresh();
    mac_runtime_post_event(&rt, 3, 0x1234, 0, 0, 0);
    frame(&rt, 0xa970, 8, ptr);
    CHECK(mac_event_service(&rt, 0xa970) == -1);
    CHECK(mac_cpu_halted(rt.cpu) && rt.event_count == 1);
    CHECK(rt.events[0].message == 0x1234 && mac_cpu_get(rt.cpu, MAC_A7) == SP);
    CHECK(peek(&rt, SP + 6, 2) == 0x55aa);
    mac_cpu_destroy(rt.cpu);
}

static void bad_stack_preserves_queue(uint32_t sp, uint16_t op) {
    MacRuntime rt = fresh();
    mac_runtime_post_event(&rt, 3, 0x1234, 0, 0, 0);
    memset(mac_cpu_ram(rt.cpu) + RECORD, 0xcc, 16);
    if (sp <= RAM - 6 && !(sp & 1)) {
        mac_cpu_write(rt.cpu, sp, 4, RECORD);
        mac_cpu_write(rt.cpu, sp + 4, 2, 8);
    }
    mac_cpu_set(rt.cpu, MAC_A7, sp);
    CHECK(mac_event_service(&rt, op) == -1);
    CHECK(mac_cpu_halted(rt.cpu) && rt.event_count == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == sp && peek(&rt, RECORD, 4) == 0xcccccccc);
    mac_cpu_destroy(rt.cpu);
}

static void bad_point_is_atomic(void) {
    MacRuntime rt = fresh();
    rt.mouse_y = 42;
    mac_cpu_write(rt.cpu, RAM - 2, 2, 0xcafe);
    mac_cpu_write(rt.cpu, SP, 4, RAM - 2);
    CHECK(mac_event_service(&rt, 0xa972) == -1);
    CHECK(mac_cpu_halted(rt.cpu));
    CHECK(peek(&rt, RAM - 2, 2) == 0xcafe && mac_cpu_get(rt.cpu, MAC_A7) == SP);
    mac_cpu_destroy(rt.cpu);
}

static void current_keys(void) {
    MacRuntime r=fresh();
    mac_runtime_post_event(&r,3,0x0061,0x0380,0,0); /* A + command + shift. */
    mac_runtime_post_event(&r,3,0x7e1e,0x0380,0,0); /* Up arrow. */
    mac_cpu_write(r.cpu,SP,4,RECORD);
    CHECK(mac_event_service(&r,0xa976)==1);
    CHECK(peek(&r,RECORD,1)==1 && peek(&r,RECORD+15,1)==0x40);
    CHECK(peek(&r,RECORD+6,1)==0x80 && peek(&r,RECORD+7,1)==1);
    CHECK(r.event_count==2 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    r.event_count=64; /* Physical release must survive event-queue overflow. */
    mac_runtime_post_event(&r,4,0x0061,0x80,0,0);
    mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_write(r.cpu,SP,4,RECORD);
    CHECK(mac_event_service(&r,0xa976)==1 && peek(&r,RECORD,1)==0);
    CHECK(peek(&r,RECORD+6,1)==0 && peek(&r,RECORD+7,1)==0);
    CHECK(r.event_count==64 && peek(&r,RECORD+15,1)==0x40);
    mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_write(r.cpu,SP,4,RAM-8);
    memset(mac_cpu_ram(r.cpu)+RAM-8,0x5a,8);
    CHECK(mac_event_service(&r,0xa976)==-1 && mac_cpu_get(r.cpu,MAC_A7)==SP);
    CHECK(mac_cpu_ram(r.cpu)[RAM-1]==0x5a);
    mac_cpu_destroy(r.cpu);
}

static void post_tests(void) {
    MacRuntime rt = fresh(); rt.ticks = 77; rt.mouse_x = 5; rt.mouse_y = 6;
    mac_cpu_set(rt.cpu, MAC_A0, 15); mac_cpu_set(rt.cpu, MAC_D0, 0x1234);   /* app4Evt */
    CHECK(mac_event_service(&rt, 0xa02f) == 1 && mac_cpu_get(rt.cpu, MAC_D0) == 0 && rt.event_count == 1);
    mac_cpu_set(rt.cpu, MAC_A0, 3); mac_cpu_set(rt.cpu, MAC_D0, 0x41);       /* keyDown, queued after the first */
    CHECK(mac_event_service(&rt, 0xa02f) == 1 && rt.event_count == 2);
    MacEvent a = rt.events[rt.event_head], b = rt.events[(rt.event_head + 1) % 64];
    CHECK(a.what == 15 && a.message == 0x1234 && a.when == 77 && a.x == 5 && a.y == 6 && a.modifiers == 0x80 && b.what == 3 && b.message == 0x41);
    frame(&rt, 0xa970, 1u << 3, RECORD);                                      /* GetNextEvent for keys only takes the second */
    CHECK(mac_event_service(&rt, 0xa970) == 1);
    CHECK(peek(&rt, RECORD, 2) == 3 && rt.event_count == 1 && rt.events[rt.event_head].what == 15);
    for (int i = 0; i < 3; i++) { mac_cpu_set(rt.cpu, MAC_A0, 2); mac_cpu_set(rt.cpu, MAC_D0, (uint32_t)i); CHECK(mac_event_service(&rt, 0xa02f) == 1); }
    rt.event_head = 63; /* wrap-around: head near the end of the ring */
    rt.event_count = 0;
    for (int i = 0; i < 64; i++) { mac_cpu_set(rt.cpu, MAC_A0, 1); mac_cpu_set(rt.cpu, MAC_D0, (uint32_t)i); CHECK(mac_event_service(&rt, 0xa02f) == 1 && mac_cpu_get(rt.cpu, MAC_D0) == 0); }
    CHECK(rt.event_count == 64);
    mac_cpu_set(rt.cpu, MAC_A0, 1); CHECK(mac_event_service(&rt, 0xa02f) == 1 && (int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -1 && rt.event_count == 64); /* full: evQErr */
    CHECK(rt.events[63].message == 0 && rt.events[62].message == 63);          /* order kept across the wrap */
    rt.event_count = 0;
    mac_cpu_set(rt.cpu, MAC_A0, 0); CHECK(mac_event_service(&rt, 0xa02f) == 1 && (int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -1 && rt.event_count == 0); /* null */
    mac_cpu_set(rt.cpu, MAC_A0, 16); CHECK(mac_event_service(&rt, 0xa02f) == 1 && (int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -1 && rt.event_count == 0);
    mac_cpu_set(rt.cpu, MAC_A0, 0x10003); CHECK(mac_event_service(&rt, 0xa02f) == 1 && (int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -1);   /* only the low word is a code; 0x10003 is not 3 */
    mac_cpu_destroy(rt.cpu);
}
int main(void) {
    post_tests();
    current_keys();
    record_layout_and_queue();
    point_ticks_button();
    flush_stop_mask();
    bad_record_preserves_queue(RECORD + 1);
    bad_record_preserves_queue(RAM - 14);
    bad_record_preserves_queue(0xfffffff8);
    bad_stack_preserves_queue(RAM - 6, 0xa970);
    bad_stack_preserves_queue(0xfffffffc, 0xa970);
    bad_stack_preserves_queue(0xfffffff8, 0xa860);
    bad_stack_preserves_queue(SP + 1, 0xa970);
    bad_stack_preserves_queue(RAM - 2, 0xa975);
    bad_stack_preserves_queue(RAM - 1, 0xa974);
    bad_point_is_atomic();
    /* Deterministic malformed frames above RAM and around uint32 wrap. */
    uint32_t random = 0x2468ace0;
    for (unsigned i = 0; i < 128; i++) {
        random = random * 1664525u + 1013904223u;
        uint32_t malformed = random | 0x80000000u;
        bad_stack_preserves_queue(malformed, i & 1 ? 0xa970 : 0xa860);
    }
    puts("mac event ABI tests passed");
    return 0;
}
