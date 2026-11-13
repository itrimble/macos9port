test: build/test_mac_picture
test: build/test_mac_pict_v1

build/test_mac_sane: $(RUNTIME_OBJ) tests/test_mac_sane.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sane.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_sane_arith: $(RUNTIME_OBJ) tests/test_mac_sane_arith.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sane_arith.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_sane build/test_mac_sane_arith

build/test_mac_help: $(RUNTIME_OBJ) tests/test_mac_help.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_help.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_appleevents: $(RUNTIME_OBJ) tests/test_mac_appleevents.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_appleevents.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_window_colors: $(RUNTIME_OBJ) tests/test_mac_window_colors.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window_colors.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_setorigin: $(RUNTIME_OBJ) tests/test_mac_setorigin.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_setorigin.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_getnewpalette: $(RUNTIME_OBJ) tests/test_mac_getnewpalette.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_getnewpalette.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_palette_activation: $(RUNTIME_OBJ) tests/test_mac_palette_activation.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_palette_activation.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_setportbits: $(RUNTIME_OBJ) tests/test_mac_setportbits.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_setportbits.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_pict_depths: $(RUNTIME_OBJ) tests/test_mac_pict_depths.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_pict_depths.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_delay: $(RUNTIME_OBJ) tests/test_mac_delay.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_delay.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_numconv: $(RUNTIME_OBJ) tests/test_mac_numconv.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_numconv.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_numconv build/test_mac_help build/test_mac_appleevents build/test_mac_window_colors build/test_mac_setorigin build/test_mac_getnewpalette build/test_mac_palette_activation build/test_mac_pict_depths build/test_mac_setportbits build/test_mac_delay

build/test_mac_sound: $(RUNTIME_OBJ) tests/test_mac_sound.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_sound

build/test_mac_sound_double: $(RUNTIME_OBJ) tests/test_mac_sound_double.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound_double.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_sound_double

build/test_mac_sound_resample: $(RUNTIME_OBJ) tests/test_mac_sound_resample.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound_resample.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_sound_resample

build/test_mac_sound_dispose: $(RUNTIME_OBJ) tests/test_mac_sound_dispose.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound_dispose.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_sound_dispose

build/test_mac_time: $(RUNTIME_OBJ) tests/test_mac_time.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_time.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_time

$(MUSASHI)/m68kops.c: scripts/prepare-musashi.sh
	sh scripts/prepare-musashi.sh

build/m68kcpu.o: $(MUSASHI)/m68kops.c Makefile
	$(CC) -O2 $(CPUFLAGS) -c $(MUSASHI)/m68kcpu.c -o $@

build/m68kops.o: $(MUSASHI)/m68kops.c Makefile
	$(CC) -O2 $(CPUFLAGS) -c $(MUSASHI)/m68kops.c -o $@

build/softfloat.o: $(MUSASHI)/m68kops.c Makefile
	$(CC) -O2 $(CPUFLAGS) -c $(MUSASHI)/softfloat/softfloat.c -o $@

build/ospi-runtime: $(RUNTIME_OBJ) src/runtime_main.c src/runtime_video.c src/runtime_audio.c src/runtime_perf.c src/runtime_keys.c src/runtime_bridge.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(SDL_CFLAGS) $(RUNTIME_OBJ) src/runtime_main.c src/runtime_video.c src/runtime_audio.c src/runtime_perf.c src/runtime_keys.c src/runtime_bridge.c $(CPUOBJ) $(SDL_LIBS) -pthread -lm $(FT_LIBS) -o $@

# Headless probe with the protected heap and its bridge built under ASan/UBSan, for running real guest apps.
# (SDL is not linked: the SDL runtime deadlocks during Cocoa start-up under ASan on this host.)
build/ospi-runtime-probe-asan: $(RUNTIME_OBJ) src/os999_heap.c src/mac_heap_bridge.c src/runtime_probe.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined $(filter-out build/runtime/os999_heap.o build/runtime/mac_heap_bridge.o,$(RUNTIME_OBJ)) src/os999_heap.c src/mac_heap_bridge.c src/runtime_probe.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

# Headless deterministic probe: scripted input, fixed 60 Hz clock, production
# sound poll/render chain to WAV, periodic PPM frames. No SDL.
build/ospi-runtime-probe: $(RUNTIME_OBJ) src/runtime_probe.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) src/runtime_probe.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

# Real-time bridge probe: drives build/ospi-runtime --bridge like the desktop.
build/ospi-bridge-probe: src/bridge_probe.c src/runtime_bridge.c include/runtime_bridge.h
	$(CC) $(CFLAGS) -pthread src/bridge_probe.c src/runtime_bridge.c -o $@

# Guest RAM disassembler over Musashi's m68kdasm (diagnostic only).
build/ospi-dasm: src/ram_dasm.c $(MUSASHI)/m68kops.c
	$(CC) -O2 $(CPUFLAGS) -Wall src/ram_dasm.c $(MUSASHI)/m68kdasm.c -o $@

.PHONY: probe
probe: build/ospi-runtime-probe build/ospi-bridge-probe build/ospi-dasm

# Diagnostic runtime with the same code path; OSPI_AUDIO_WAV opts into a
# bounded producer-PCM capture without replacing the app's live executable.
build/ospi-runtime-capture: $(RUNTIME_OBJ) src/runtime_main.c src/runtime_video.c src/runtime_audio.c src/runtime_perf.c src/runtime_keys.c src/runtime_bridge.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(SDL_CFLAGS) $(RUNTIME_OBJ) src/runtime_main.c src/runtime_video.c src/runtime_audio.c src/runtime_perf.c src/runtime_keys.c src/runtime_bridge.c $(CPUOBJ) $(SDL_LIBS) -pthread -lm $(FT_LIBS) -o $@

build/test_mac_resource: src/mac_resource.c tests/test_mac_resource.c include/mac_resource.h
	mkdir -p build
	$(CC) $(CFLAGS) src/mac_resource.c tests/test_mac_resource.c -o $@

build/test_mac_cpu: src/mac_cpu.c tests/test_mac_cpu.c include/mac_cpu.h $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) src/mac_cpu.c tests/test_mac_cpu.c $(CPUOBJ) -lm -o $@

build/test_mac_runtime: $(RUNTIME_OBJ) tests/test_mac_runtime.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_runtime.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_resource_services: $(RUNTIME_OBJ) tests/test_mac_resource_services.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_resource_services.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_resource_edits: $(RUNTIME_OBJ) tests/test_mac_resource_edits.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_resource_edits.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_menubar: $(RUNTIME_OBJ) tests/test_mac_menubar.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menubar.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_memory: $(RUNTIME_OBJ) tests/test_mac_memory.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_memory.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_os999_heap: src/os999_heap.c tests/test_os999_heap.c include/os999_heap.h
	mkdir -p build
	$(CC) $(CFLAGS) src/os999_heap.c tests/test_os999_heap.c -o $@

build/test_mac_heap_bridge: $(RUNTIME_OBJ) tests/test_mac_heap_bridge.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_heap_bridge.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_guest_memory: $(RUNTIME_OBJ) tests/test_guest_memory.c include/guest_memory.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_guest_memory.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

# Classic application classification: pure byte analysis, so it links only the resource reader.
APP_CLASSIFY_SRC := src/app_classify.c src/app_classify_exec.c src/app_classify_json.c src/mac_resource.c
build/test_app_classify: $(APP_CLASSIFY_SRC) src/app_classify_int.h include/app_classify.h include/mac_resource.h tests/test_app_classify.c
	$(CC) $(CFLAGS) tests/test_app_classify.c $(APP_CLASSIFY_SRC) -o $@
build/os999-classify: $(APP_CLASSIFY_SRC) src/app_classify_tool.c src/app_classify_int.h include/app_classify.h include/mac_resource.h
	$(CC) $(CFLAGS) src/app_classify_tool.c $(APP_CLASSIFY_SRC) -o $@
test: build/test_app_classify
LAUNCH_SRC := src/launch_profile.c src/compat_known.c src/native_policy.c $(APP_CLASSIFY_SRC)
build/test_launch_profile: $(LAUNCH_SRC) include/launch_profile.h src/native_policy.h include/native_settings.h tests/test_launch_profile.c src/control_panel_store.c $(STORAGE_A)
	$(CC) $(CFLAGS) -Isrc -pthread tests/test_launch_profile.c $(LAUNCH_SRC) src/control_panel_store.c src/os999_private.c $(STORAGE_LINK) -o $@
test: build/test_launch_profile
build/test_legacy_pointers: $(RUNTIME_OBJ) tests/test_legacy_pointers.c include/guest_memory.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_legacy_pointers.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
test: build/test_legacy_pointers
build/test_app_mode: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_app_mode.c include/app_mode.h include/compat_known.h $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_app_mode.c $(FT_LIBS) -lm $(NET_LIBS)
test: build/test_app_mode

# Bridge under ASan/UBSan: only the heap, bridge and test are instrumented; the rest of the runtime is the normal build.
build/test_mac_heap_bridge_asan: $(RUNTIME_OBJ) src/os999_heap.c src/mac_heap_bridge.c tests/test_mac_heap_bridge.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined $(filter-out build/runtime/os999_heap.o build/runtime/mac_heap_bridge.o,$(RUNTIME_OBJ)) src/os999_heap.c src/mac_heap_bridge.c tests/test_mac_heap_bridge.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_os999_heap_asan: src/os999_heap.c tests/test_os999_heap.c include/os999_heap.h
	mkdir -p build
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined src/os999_heap.c tests/test_os999_heap.c -o $@

# Memory manager gate: plain and ASan/UBSan runs of the protected heap, plus the
# guest Memory Manager trap tests.
test-memory: build/test_os999_heap build/test_os999_heap_asan build/test_mac_memory build/test_mac_heap_bridge build/test_mac_heap_bridge_asan build/test_mac_resource_edits
	./build/test_os999_heap
	ASAN_OPTIONS=handle_segv=0:handle_sigbus=0 ./build/test_os999_heap_asan
	./build/test_mac_memory
	./build/test_mac_heap_bridge
	./build/test_mac_heap_bridge_asan
	./build/test_mac_resource_edits

build/test_mac_traps: $(RUNTIME_OBJ) tests/test_mac_traps.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_traps.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_guestcall: $(RUNTIME_OBJ) tests/test_mac_guestcall.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_guestcall.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_ditl: $(RUNTIME_OBJ) tests/test_mac_ditl.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_ditl.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_files_entries: $(RUNTIME_OBJ) tests/test_mac_files_entries.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_files_entries.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_stdfile_list: $(RUNTIME_OBJ) tests/test_mac_stdfile_list.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_stdfile_list.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_stdfile: $(RUNTIME_OBJ) tests/test_mac_stdfile.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_stdfile.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_alert: $(RUNTIME_OBJ) tests/test_mac_alert.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_alert.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_appfork: $(RUNTIME_OBJ) tests/test_mac_appfork.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_appfork.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_unloadseg: $(RUNTIME_OBJ) tests/test_mac_unloadseg.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_unloadseg.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_native_launch: src/control_panel_store.c include/native_settings.h include/control_panel_store.h $(STORAGE_A) src/application_probe.c include/application_probe.h src/native_launch.c src/native_policy.c src/launch_profile.c src/compat_known.c src/app_classify.c src/app_classify_exec.c src/app_classify_json.c src/runtime_bridge.c src/mac_resource.c tests/test_native_launch.c include/native_launch.h include/mac_resource.h include/runtime_bridge.h
	$(CC) $(CFLAGS) -pthread src/control_panel_store.c src/native_launch.c src/native_policy.c src/launch_profile.c src/compat_known.c src/app_classify.c src/app_classify_exec.c src/app_classify_json.c src/os999_private.c src/application_probe.c src/runtime_bridge.c src/mac_resource.c tests/test_native_launch.c $(STORAGE_LINK) -o $@

build/test_mac_events: $(RUNTIME_OBJ) tests/test_mac_events.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_events.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_quickdraw: $(RUNTIME_OBJ) tests/test_mac_quickdraw.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_quickdraw.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_runtime_video: tests/test_runtime_video.c src/runtime_video.c src/runtime_keys.c $(RUNTIME_HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/test_runtime_video.c src/runtime_keys.c -o $@

build/test_runtime_perf: tests/test_runtime_perf.c src/runtime_perf.c include/runtime_perf.h
	$(CC) $(CFLAGS) tests/test_runtime_perf.c src/runtime_perf.c -o $@

build/test_runtime_keys: tests/test_runtime_keys.c src/runtime_keys.c include/runtime_keys.h include/runtime_bridge.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/test_runtime_keys.c src/runtime_keys.c -o $@

build/test_mac_icon: tests/test_mac_icon.c src/mac_icon.c src/mac_icon_palette.c src/mac_resource.c src/volume.c src/volume_folders.c src/native_catalog.c include/mac_icon.h include/mac_resource.h include/ospi.h include/volume_directory.h include/native_apps.h
	$(CC) $(CFLAGS) -o $@ tests/test_mac_icon.c src/mac_icon.c src/mac_icon_palette.c src/mac_resource.c src/volume.c src/volume_folders.c src/native_catalog.c

build/test_runtime_bridge: tests/test_runtime_bridge.c src/runtime_bridge.c include/runtime_bridge.h
	mkdir -p build
	$(CC) $(CFLAGS) -pthread -o $@ tests/test_runtime_bridge.c src/runtime_bridge.c

runtime: build/ospi-runtime

