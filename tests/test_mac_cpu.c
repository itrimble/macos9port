#include "mac_cpu.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

static int trap_calls;
static int trap(MacCpu *cpu, uint16_t opcode, uint32_t pc, void *user) {
    CHECK(opcode == 0xa123);
    CHECK(pc == 0x108);
    CHECK(user == &trap_calls);
    CHECK(mac_cpu_get(cpu, MAC_PC) == 0x10a);
    CHECK(mac_cpu_get(cpu, MAC_D0) == 3);
    mac_cpu_set(cpu, MAC_A0, 0x200);
    mac_cpu_write(cpu, 0x200, 4, 0x12345678);
    trap_calls++;
    return 0;
}

static void model_68020(void) {
    CHECK(mac_cpu_create_model(4096, (MacCpuModel)99) == NULL);
    MacCpu *cpu = mac_cpu_create_model(65536, MAC_CPU_68020);
    CHECK(cpu != NULL);
    if (!cpu) return;
    mac_cpu_write(cpu, 0x100, 2, 0x49c0); /* EXTB.L D0: 68020 instruction. */
    const uint32_t input[] = {0x12345680,0xabcdef7f,0xffffffff,0x12345600};
    const uint32_t expected[] = {0xffffff80,0x7f,0xffffffff,0};
    const uint32_t flags[] = {0x18,0x10,0x18,0x14};
    for (unsigned i=0;i<4;i++) {
        mac_cpu_set(cpu,MAC_PC,0x100);
        mac_cpu_set(cpu,MAC_D0,input[i]);
        mac_cpu_set(cpu,MAC_SR,0x1f); /* X remains; C/V clear; N/Z reflect result. */
        CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
        CHECK(mac_cpu_get(cpu,MAC_D0)==expected[i]);
        CHECK((mac_cpu_get(cpu,MAC_SR)&0x1f)==flags[i]);
    }
    /* Tagged, odd guest addresses are legal through this 68020 bus adapter. */
    mac_cpu_write(cpu,0x100,2,0x2210); /* MOVE.L (A0),D1 */
    memcpy(mac_cpu_ram(cpu)+0x201,"\x12\x34\x56\x78",4);
    mac_cpu_set(cpu,MAC_A0,0xab000201);mac_cpu_set(cpu,MAC_PC,0x100);
    CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
    CHECK(mac_cpu_get(cpu,MAC_D1)==0x12345678);
    mac_cpu_write(cpu,0x100,2,0x3210); /* MOVE.W (A0),D1 */
    mac_cpu_set(cpu,MAC_D1,0xaaaa0000);mac_cpu_set(cpu,MAC_PC,0x100);
    CHECK(mac_cpu_run(cpu,1)==1 && mac_cpu_get(cpu,MAC_D1)==0xaaaa1234);
    mac_cpu_write(cpu,0x100,2,0x2081); /* MOVE.L D1,(A0) */
    mac_cpu_set(cpu,MAC_A0,0xcd000305);mac_cpu_set(cpu,MAC_D1,0x89abcdef);
    mac_cpu_set(cpu,MAC_PC,0x100);CHECK(mac_cpu_run(cpu,1)==1);
    CHECK(memcmp(mac_cpu_ram(cpu)+0x305,"\x89\xab\xcd\xef",4)==0);
    mac_cpu_write(cpu,0x100,2,0x3081); /* MOVE.W D1,(A0) */
    mac_cpu_set(cpu,MAC_D1,0x98761234);mac_cpu_set(cpu,MAC_PC,0x100);
    CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
    CHECK(memcmp(mac_cpu_ram(cpu)+0x305,"\x12\x34\xcd\xef",4)==0);
    mac_cpu_write(cpu,0x100,2,0x4e71); /* NOP at tagged instruction address. */
    mac_cpu_set(cpu,MAC_PC,0xab000100);
    CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
    CHECK(mac_cpu_get(cpu,MAC_PC)==0x102);
    /* Runtime helper API remains checked, rather than silently accepting tags. */
    CHECK(mac_cpu_read(cpu,0xab000201,1)==0 && mac_cpu_halted(cpu));
    mac_cpu_destroy(cpu);

    cpu=mac_cpu_create_model(4096,MAC_CPU_68020);
    if (!cpu) { CHECK(cpu!=NULL); return; }
    CHECK(mac_cpu_write(cpu,0x201,2,0x1234)==-1 && mac_cpu_halted(cpu));
    mac_cpu_destroy(cpu);
    cpu=mac_cpu_create_model(4096,MAC_CPU_68020);
    if (!cpu) { CHECK(cpu!=NULL); return; }
    mac_cpu_write(cpu,0x100,2,0x2081);
    mac_cpu_ram(cpu)[4095]=0xa5;
    mac_cpu_set(cpu,MAC_A0,0xfe000fff);mac_cpu_set(cpu,MAC_D1,0x12345678);
    mac_cpu_set(cpu,MAC_PC,0x100);CHECK(mac_cpu_run(cpu,1)==1);
    CHECK(mac_cpu_halted(cpu) && mac_cpu_ram(cpu)[4095]==0xa5); /* Atomic rejection. */
    mac_cpu_destroy(cpu);

    cpu=mac_cpu_create(4096);
    if (!cpu) { CHECK(cpu!=NULL); return; }
    mac_cpu_write(cpu,0x100,2,0x49c0);mac_cpu_set(cpu,MAC_PC,0x100);
    CHECK(mac_cpu_run(cpu,1)==1 && mac_cpu_halted(cpu)); /* EXTB illegal on 68000. */
    mac_cpu_destroy(cpu);
    cpu=mac_cpu_create(4096);
    if (!cpu) { CHECK(cpu!=NULL); return; }
    mac_cpu_write(cpu,0x100,2,0x2210);mac_cpu_set(cpu,MAC_PC,0x100);
    mac_cpu_set(cpu,MAC_A0,0x201);
    CHECK(mac_cpu_run(cpu,1)==1 && mac_cpu_halted(cpu)); /* Odd data illegal on 68000. */
    mac_cpu_destroy(cpu);
}

static int boundary_trap(MacCpu *cpu,uint16_t opcode,uint32_t pc,void *user) {
    CHECK(opcode==0xa123 && pc==0xfffffe && mac_cpu_get(cpu,MAC_PC)==0);
    (*(int *)user)++;
    return 0;
}

static void bus_wrap(MacCpuModel model) {
    MacCpu *cpu=mac_cpu_create_model(0x1000000,model);
    CHECK(cpu!=NULL);
    if (!cpu) return;
    uint8_t *ram=mac_cpu_ram(cpu);
    mac_cpu_write(cpu,0x100,2,0x2210);mac_cpu_write(cpu,0x102,2,0x2081);
    ram[0xfffffe]=0x12;ram[0xffffff]=0x34;ram[0]=0x56;ram[1]=0x78;
    mac_cpu_set(cpu,MAC_A0,0xabfffffe);mac_cpu_set(cpu,MAC_PC,0x100);
    CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
    CHECK(mac_cpu_get(cpu,MAC_D1)==0x12345678);
    mac_cpu_set(cpu,MAC_D1,0xabcdef01);mac_cpu_set(cpu,MAC_PC,0x102);
    CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
    CHECK(ram[0xfffffe]==0xab && ram[0xffffff]==0xcd && ram[0]==0xef && ram[1]==1);
    if(model==MAC_CPU_68020) {
        mac_cpu_set(cpu,MAC_A0,0x12ffffff);mac_cpu_set(cpu,MAC_D1,0x11223344);
        mac_cpu_set(cpu,MAC_PC,0x102);CHECK(mac_cpu_run(cpu,1)==1);
        CHECK(ram[0xffffff]==0x11 && ram[0]==0x22 && ram[1]==0x33 && ram[2]==0x44);
        mac_cpu_set(cpu,MAC_PC,0x100);CHECK(mac_cpu_run(cpu,1)==1);
        CHECK(mac_cpu_get(cpu,MAC_D1)==0x11223344 && !mac_cpu_halted(cpu));
    }
    mac_cpu_write(cpu,0xfffffe,2,0x203c); /* MOVE.L #immediate,D0 crosses bus boundary. */
    mac_cpu_write(cpu,0,4,0xd00df00d);mac_cpu_set(cpu,MAC_PC,0xfffffe);
    CHECK(mac_cpu_run(cpu,1)==1 && !mac_cpu_halted(cpu));
    CHECK(mac_cpu_get(cpu,MAC_D0)==0xd00df00d && mac_cpu_get(cpu,MAC_PC)==4);
    int called=0;mac_cpu_set_trap(cpu,boundary_trap,&called);
    mac_cpu_write(cpu,0xfffffe,2,0xa123);mac_cpu_set(cpu,MAC_PC,0xabfffffe);
    CHECK(mac_cpu_run(cpu,1)==1 && called==1 && !mac_cpu_halted(cpu));
    mac_cpu_destroy(cpu);
}

int main(void) {
    CHECK(mac_cpu_create(0) == NULL);
    CHECK(mac_cpu_create(4097) == NULL);
    CHECK(mac_cpu_create(0x1000002) == NULL);
    MacCpu *cpu = mac_cpu_create(65536);
    CHECK(cpu != NULL);
    if (!cpu) { return 1; }
    /* Authored fixture: MOVEQ #1,D0; ADDQ.L #2,D0; BRA.S +2; MOVEQ #7,D0;
     * A-line callback; MOVE.L (A0),D1; BRA.S -2. No copied guest code. */
    const unsigned char code[] = {0x70,1, 0x54,0x80, 0x60,2, 0x70,7,
                                  0xa1,0x23, 0x22,0x10, 0x60,0xfe};
    memcpy(mac_cpu_ram(cpu) + 0x100, code, sizeof code);
    mac_cpu_set(cpu, MAC_PC, 0x100);
    mac_cpu_set(cpu, MAC_A5, 0x300);
    mac_cpu_set_trap(cpu, trap, &trap_calls);
    CHECK(mac_cpu_run(cpu, 0) == 0);
    CHECK(mac_cpu_run(cpu, 5) == 5);
    CHECK(!mac_cpu_halted(cpu));
    CHECK(mac_cpu_get(cpu, MAC_PC) == 0x10c);
    CHECK(mac_cpu_get(cpu, MAC_D1) == 0x12345678);
    CHECK(mac_cpu_get(cpu, MAC_A5) == 0x300);
    CHECK(trap_calls == 1);
    CHECK(mac_cpu_cycles(cpu) > 0);
    CHECK(mac_cpu_run(cpu, 1000) == 1000);
    CHECK(mac_cpu_get(cpu, MAC_PC) == 0x10c);
    MacCpu *second = mac_cpu_create(4096);
    CHECK(second != NULL);
    if (second) {
        mac_cpu_set(second, MAC_D0, 0x55);
        CHECK(mac_cpu_get(cpu, MAC_D0) == 3);
        CHECK(mac_cpu_run(cpu, 1) == 1);
        CHECK(mac_cpu_get(second, MAC_D0) == 0x55);
        CHECK(mac_cpu_write(second, 0x100, 4, 0xffffffff) == 0);
        CHECK(mac_cpu_read(second, 0x100, 4) == 0xffffffff);
        CHECK(mac_cpu_write(second, 4094, 4, 0) == -1);
        CHECK(mac_cpu_halted(second));
        CHECK(strstr(mac_cpu_error(second), "memory access") != NULL);
        CHECK(mac_cpu_run(second, 1) == 0);
        mac_cpu_destroy(second);
    }
    mac_cpu_set(cpu, MAC_PC, 0xffff);
    CHECK(mac_cpu_run(cpu, 1) == 0);
    CHECK(mac_cpu_halted(cpu));
    mac_cpu_destroy(cpu);
    cpu = mac_cpu_create(4096);
    if (!cpu) { return 1; }
    mac_cpu_write(cpu, 0x100, 2, 0xa999);
    mac_cpu_set(cpu, MAC_PC, 0x100);
    CHECK(mac_cpu_run(cpu, 1) == 1);
    CHECK(strstr(mac_cpu_error(cpu), "unhandled A-line") != NULL);
    mac_cpu_destroy(cpu);
    cpu = mac_cpu_create(4096);
    if (!cpu) { return 1; }
    mac_cpu_write(cpu, 0x100, 2, 0x4afc);
    mac_cpu_set(cpu, MAC_PC, 0x100);
    CHECK(mac_cpu_run(cpu, 1) == 1);
    CHECK(strstr(mac_cpu_error(cpu), "illegal instruction") != NULL);
    mac_cpu_destroy(cpu);
    model_68020();
    bus_wrap(MAC_CPU_68000);
    bus_wrap(MAC_CPU_68020);
    printf("CPU checks: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
