# PowerPC runtime slice: interpreter, PEF parser and fragment loader. The
# sources are self-contained (no Musashi, no SDL), so each test links only
# what it exercises. docs/POWERPC-RUNTIME.md describes the pieces.
PPC_CPU_SRC := src/ppc_cpu.c src/ppc_exec_int.c src/ppc_exec_branch.c src/ppc_exec_mem.c src/ppc_exec_fp.c
PPC_PEF_SRC := src/mac_pef.c src/mac_pef_unpack.c
PPC_CFM_SRC := $(PPC_PEF_SRC) src/mac_cfm.c src/mac_cfm_reloc.c src/ppc_host.c
PPC_HDRS := include/ppc_cpu.h src/ppc_int.h include/mac_pef.h include/mac_cfm.h include/mac_ppc_probe.h tests/ppc_asm.h tests/pef_fixture.h
# The Toolbox bridge needs the whole 68K runtime (it serves imports through the trap services).
PPC_BRIDGE_SRC := src/mac_ppc_runtime.c src/mac_ppc_bridge.c src/mac_ppc_mixed.c src/mac_ppc_native.c src/mac_ppc_math.c src/mac_ppc_files.c src/mac_ppc_sigs_memory.c src/mac_ppc_sigs_quickdraw.c src/mac_ppc_sigs_toolbox.c src/mac_ppc_sigs_managers.c
RUNTIME += $(PPC_CPU_SRC) $(PPC_CFM_SRC) src/mac_ppc_probe.c $(PPC_BRIDGE_SRC)
RUNTIME_HDRS += include/ppc_cpu.h src/ppc_int.h include/mac_pef.h include/mac_cfm.h include/mac_ppc_probe.h include/mac_ppc.h

build/test_ppc_cpu: $(PPC_CPU_SRC) tests/test_ppc_cpu.c $(PPC_HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc $(PPC_CPU_SRC) tests/test_ppc_cpu.c -lm -o $@

build/test_ppc_fp: $(PPC_CPU_SRC) tests/test_ppc_fp.c $(PPC_HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc $(PPC_CPU_SRC) tests/test_ppc_fp.c -lm -o $@

build/test_mac_pef: $(PPC_PEF_SRC) tests/test_mac_pef.c $(PPC_HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) $(PPC_PEF_SRC) tests/test_mac_pef.c -o $@

build/test_mac_cfm: $(PPC_CPU_SRC) $(PPC_CFM_SRC) tests/test_mac_cfm.c $(PPC_HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) $(PPC_CPU_SRC) $(PPC_CFM_SRC) tests/test_mac_cfm.c -lm -o $@

build/test_mac_cfm_reloc: $(PPC_CPU_SRC) $(PPC_CFM_SRC) tests/test_mac_cfm_reloc.c $(PPC_HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) $(PPC_CPU_SRC) $(PPC_CFM_SRC) tests/test_mac_cfm_reloc.c -lm -o $@

build/test_mac_ppc_probe: $(PPC_CPU_SRC) $(PPC_CFM_SRC) src/mac_ppc_probe.c src/mac_resource.c tests/test_mac_ppc_probe.c $(PPC_HDRS) include/mac_resource.h
	mkdir -p build
	$(CC) $(CFLAGS) $(PPC_CPU_SRC) $(PPC_CFM_SRC) src/mac_ppc_probe.c src/mac_resource.c tests/test_mac_ppc_probe.c -lm -o $@

build/test_mac_ppc_bridge: $(RUNTIME_OBJ) tests/test_mac_ppc_bridge.c $(RUNTIME_HDRS) $(PPC_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_ppc_bridge.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_ppc_mdef: $(RUNTIME_OBJ) tests/test_mac_ppc_mdef.c tests/mdef_fixture.h tests/fork_builder.h $(RUNTIME_HDRS) $(PPC_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_ppc_mdef.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_ppc_gworld: $(RUNTIME_OBJ) tests/test_mac_ppc_gworld.c $(RUNTIME_HDRS) $(PPC_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_ppc_gworld.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_ppc_libs: $(RUNTIME_OBJ) tests/test_mac_ppc_libs.c $(RUNTIME_HDRS) $(PPC_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_ppc_libs.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_ppc_bridge build/test_mac_ppc_mdef build/test_mac_ppc_gworld build/test_mac_ppc_libs
test: build/test_ppc_cpu build/test_ppc_fp build/test_mac_pef build/test_mac_cfm build/test_mac_cfm_reloc build/test_mac_ppc_probe
