/* Disassemble a range of a guest RAM dump written by ospi-runtime-probe
 * --dump-ram, using Musashi's disassembler (CPU-only dependency). This is a
 * diagnostic for understanding what a guest program expects from the runtime;
 * it never feeds code back into the implementation. */
#include "m68k.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *ram;
static size_t ram_size;

static unsigned int read_byte(unsigned int address) {
    return address < ram_size ? ram[address] : 0;
}
unsigned int m68k_read_disassembler_8(unsigned int address) { return read_byte(address); }
unsigned int m68k_read_disassembler_16(unsigned int address) {
    return read_byte(address) << 8 | read_byte(address + 1);
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
    return m68k_read_disassembler_16(address) << 16 | m68k_read_disassembler_16(address + 2);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: ospi-dasm ram.bin start-hex end-hex\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length <= 0 || length > 64 * 1024 * 1024) { fclose(f); return 2; }
    ram = malloc((size_t)length);
    if (!ram || fread(ram, 1, (size_t)length, f) != (size_t)length) { fclose(f); return 2; }
    fclose(f);
    ram_size = (size_t)length;
    unsigned int pc = (unsigned int)strtoul(argv[2], NULL, 16), end = (unsigned int)strtoul(argv[3], NULL, 16);
    char text[256];
    while (pc < end && pc < ram_size) {
        unsigned int size = m68k_disassemble(text, pc, M68K_CPU_TYPE_68020);
        printf("%08x:", pc);
        for (unsigned int i = 0; i < size && i < 10; i += 2) printf(" %04x", m68k_read_disassembler_16(pc + i));
        printf("%*s%s\n", (int)(size < 10 ? (10 - size) / 2 * 5 : 0) + 2, "", text);
        pc += size ? size : 2;
    }
    free(ram);
    return 0;
}
