# Desktop integration, storage and networking test targets. Included from Makefile.
build/test_finder_refresh: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_refresh.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_refresh.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-finder-refresh
test-finder-refresh: build/test_finder_refresh
	./build/test_finder_refresh
test: test-finder-refresh

build/test_browser_menu_composition: $(CORE) $(STORAGE_A) $(STARTUP_SRC) src/browser_window.c src/browser_input.c src/browser_occlusion.c tests/test_browser_menu_composition.c $(HDRS) $(BROWSER_HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) src/browser_window.c src/browser_input.c src/browser_occlusion.c tests/test_browser_menu_composition.c $(FT_LIBS) -lm $(NET_LIBS)

.PHONY: test-browser-menu-composition
test-browser-menu-composition: build/test_browser_menu_composition
	./build/test_browser_menu_composition
test: test-browser-menu-composition

build/test_appearance: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_appearance.c $(HDRS) $(STARTUP_HDR)
	$(CC) $(CFLAGS) -o $@ $(filter-out src/desktop_wallpaper.c,$(CORE) $(STORAGE_LINK)) $(STARTUP_SRC) tests/test_appearance.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_mac_containers: src/mac_resource.c tests/test_mac_containers.c include/mac_resource.h
	$(CC) $(CFLAGS) -o $@ src/mac_resource.c tests/test_mac_containers.c

build/test_mac_writable_files: $(RUNTIME_OBJ) src/archive_fs.c tests/test_mac_writable_files.c $(RUNTIME_HDRS) $(CPUOBJ) include/archive_fs.h
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) src/archive_fs.c tests/test_mac_writable_files.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_controls_textedit: $(RUNTIME_OBJ) tests/test_mac_controls_textedit.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_controls_textedit.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_quickdraw_accel: $(RUNTIME_OBJ) tests/test_mac_quickdraw_accel.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_quickdraw_accel.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_desktop_themes: assets/icons/c/ospi_icon_hd.c src/archive_fs.c tests/test_desktop_themes.c assets/icons/c/ospi_icon_hd.h include/archive_fs.h
	$(CC) $(CFLAGS) -o $@ assets/icons/c/ospi_icon_hd.c src/archive_fs.c tests/test_desktop_themes.c

build/test_browser_stage: src/browser_stage.c tests/test_browser_stage.c include/browser_stage.h
	$(CC) $(CFLAGS) -o $@ src/browser_stage.c tests/test_browser_stage.c

PHASE_STANDALONE_TESTS := build/test_mac_quicktime build/test_browser_ipc build/test_mac_network_policy build/test_mac_pef_cfm build/test_browser_media_gateway build/test_cleanroom_release_gate
$(PHASE_STANDALONE_TESTS): build/%: tests/%.c
	$(CC) $(CFLAGS) -o $@ $<
build/test_mac_perf_audio_gates: src/runtime_perf.c tests/test_mac_perf_audio_gates.c include/runtime_perf.h
	$(CC) $(CFLAGS) -o $@ src/runtime_perf.c tests/test_mac_perf_audio_gates.c
build/test_mac_fuzz_soak: src/mac_resource.c src/mac_icon.c src/mac_icon_palette.c src/volume.c src/volume_folders.c src/native_catalog.c assets/icons/c/ospi_icon_hd.c src/runtime_bridge.c tests/test_mac_fuzz_soak.c $(HDRS) assets/icons/c/ospi_icon_hd.h
	$(CC) $(CFLAGS) -o $@ src/mac_resource.c src/mac_icon.c src/mac_icon_palette.c src/volume.c src/volume_folders.c src/native_catalog.c assets/icons/c/ospi_icon_hd.c src/runtime_bridge.c tests/test_mac_fuzz_soak.c
build/test_platform_sandbox_audit: build/runtime/mac_cpu.o build/runtime/mac_resource.o src/hostinfo.c src/archive_fs.c src/volume.c src/volume_folders.c src/native_catalog.c src/mac_icon.c src/mac_icon_palette.c tests/test_platform_sandbox_audit.c $(CPUOBJ) $(HDRS) include/mac_cpu.h
	$(CC) $(CFLAGS) $(CPUFLAGS) build/runtime/mac_cpu.o build/runtime/mac_resource.o src/hostinfo.c src/archive_fs.c src/volume.c src/volume_folders.c src/native_catalog.c src/mac_icon.c src/mac_icon_palette.c tests/test_platform_sandbox_audit.c $(CPUOBJ) -lm -o $@

test: build/test_appearance build/test_mac_containers build/test_mac_writable_files build/test_mac_controls_textedit build/test_mac_quickdraw_accel build/test_desktop_themes build/test_mac_perf_audio_gates build/test_mac_fuzz_soak build/test_platform_sandbox_audit build/test_browser_stage $(PHASE_STANDALONE_TESTS)


build/test_appearance_panel: src/appearance_panel.c tests/test_appearance_panel.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ src/appearance_panel.c src/draw.c src/font.c src/geom.c src/menu.c src/text.c src/typeface.c src/typeface_metrics.c tests/test_appearance_panel.c $(FT_LIBS) -lm

test: build/test_appearance_panel

build/test_desktop_context: src/desktop_theme.c src/desktop_context.c src/geom.c tests/test_desktop_context.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ src/desktop_theme.c src/desktop_context.c src/geom.c tests/test_desktop_context.c

test: build/test_desktop_context

build/test_desktop_controls: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_desktop_controls.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_desktop_controls.c $(FT_LIBS) -lm $(NET_LIBS)

test: build/test_desktop_controls

build/test_desktop_icon_map: src/desktop_icon_map.c tests/test_desktop_icon_map.c include/desktop_assets.h
	$(CC) $(CFLAGS) -o $@ src/desktop_icon_map.c tests/test_desktop_icon_map.c

test: build/test_desktop_icon_map

build/test_desktop_theme: src/desktop_theme.c tests/test_desktop_theme.c include/desktop_theme.h
	$(CC) $(CFLAGS) -o $@ src/desktop_theme.c tests/test_desktop_theme.c

build/test_theme_integration: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_theme_integration.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_theme_integration.c $(FT_LIBS) -lm $(NET_LIBS)

test: build/test_desktop_theme build/test_theme_integration

build/test_desktop_commands: src/desktop_commands.c tests/test_desktop_commands.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ src/desktop_commands.c tests/test_desktop_commands.c

build/test_menu_navigation: src/menu_navigation.c tests/test_menu_navigation.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ src/menu_navigation.c tests/test_menu_navigation.c

test: build/test_desktop_commands build/test_menu_navigation

build/test_folder_view: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_folder_view.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_folder_view.c $(FT_LIBS) -lm $(NET_LIBS)

test: build/test_folder_view

build/test_menu_integration: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_menu_integration.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_menu_integration.c $(FT_LIBS) -lm $(NET_LIBS)

test: build/test_menu_integration


build/test_folder_navigation: src/folder_navigation.c src/geom.c src/font.c src/draw.c src/text.c src/typeface.c src/typeface_metrics.c src/menu.c src/desktop_theme.c src/desktop_wallpaper.c tests/test_folder_navigation.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ src/folder_navigation.c src/geom.c src/font.c src/draw.c src/text.c src/typeface.c src/typeface_metrics.c src/menu.c src/desktop_theme.c src/desktop_wallpaper.c tests/test_folder_navigation.c $(FT_LIBS) -lm

test: build/test_folder_navigation

# Storage stack: containers, disk images, HFS/HFS+, guest volumes (L02-L04).
include storage/storage.mk
test: test-storage
# Guest networking (net/net.mk, N01-N09): host-side MacTCP service.
include net/net.mk
test: test-net
# Bluetooth stack (bluetooth/bluetooth.mk): HCI, L2CAP, RFCOMM and BNEP.
include bluetooth/bluetooth.mk
test: test-bluetooth
build/test_terminal: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_terminal.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_terminal.c $(FT_LIBS) -lm $(NET_LIBS)

test: build/test_terminal

.PHONY: test-network
build/test_modern_network: src/modern_network.c tests/test_modern_network.c include/modern_network.h
	mkdir -p build
	$(CC) $(CFLAGS) tests/test_modern_network.c $(NET_LIBS) -o $@

test-network: build/test_modern_network
	./build/test_modern_network
	python3 tests/test_network_tls.py

test: build/test_modern_network

FINDER_VOLUME_SRC := src/native_catalog.c src/volume.c src/volume_folders.c src/mac_icon.c src/mac_icon_palette.c src/mac_resource.c
build/test_finder_files: $(FINDER_VOLUME_SRC) tests/test_finder_files.c src/finder_files.c src/finder_alias.c src/archive_fs.c include/finder_files.h include/ospi.h include/volume_directory.h
	mkdir -p build
	$(CC) $(CFLAGS) $(FINDER_VOLUME_SRC) tests/test_finder_files.c src/finder_files.c src/finder_alias.c src/archive_fs.c -o $@
test: build/test_finder_files

build/test_finder_desktop: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_desktop.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_desktop.c $(FT_LIBS) -lm $(NET_LIBS)
test: build/test_finder_desktop

build/test_finder_state: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_state.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_state.c $(FT_LIBS) -lm $(NET_LIBS)
test: build/test_finder_state

build/test_finder_interaction: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_interaction.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_interaction.c $(FT_LIBS) -lm $(NET_LIBS)
test: build/test_finder_interaction

ifeq ($(shell uname -s),Darwin)
build/test_desktop_shortcuts: src/desktop_shortcuts_macos.m tests/test_desktop_shortcuts.m include/desktop_shortcuts.h
	$(CC) $(CFLAGS) -o $@ src/desktop_shortcuts_macos.m tests/test_desktop_shortcuts.m -framework AppKit
.PHONY: test-desktop-shortcuts
test-desktop-shortcuts: build/test_desktop_shortcuts
	./build/test_desktop_shortcuts
test: test-desktop-shortcuts

build/test_print_pdf: src/desktop_print_macos.m tests/test_print_pdf.m include/finder_print.h include/ospi.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/desktop_print_macos.m tests/test_print_pdf.m -framework AppKit -framework CoreGraphics -framework PDFKit
.PHONY: test-print-pdf
test-print-pdf: build/test_print_pdf
	./build/test_print_pdf
endif

# Folder capacities, bulk selection and journal ownership.
build/test_folder_capacity: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_folder_capacity.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_folder_capacity.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-folder-capacity
test-folder-capacity: build/test_folder_capacity
	./build/test_folder_capacity
test: test-folder-capacity

build/test_search_clipboard_capacity: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_search_clipboard_capacity.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_search_clipboard_capacity.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-search-clipboard-capacity
test-search-clipboard-capacity: build/test_search_clipboard_capacity
	./build/test_search_clipboard_capacity
test: test-search-clipboard-capacity

build/test_app_switching: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_app_switching.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_app_switching.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-app-switching
test-app-switching: build/test_app_switching
	./build/test_app_switching
test: test-app-switching

build/test_app_stack: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_app_stack.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_app_stack.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-app-stack
test-app-stack: build/test_app_stack
	./build/test_app_stack
test: test-app-stack

build/test_app_switcher: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_app_switcher.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_app_switcher.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-app-switcher
test-app-switcher: build/test_app_switcher
	./build/test_app_switcher
test: test-app-switcher


build/test_finder_spring: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_spring.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_spring.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-finder-spring
test-finder-spring: build/test_finder_spring
	./build/test_finder_spring
test: test-finder-spring


build/test_finder_labels: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_labels.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_labels.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-finder-labels
test-finder-labels: build/test_finder_labels
	./build/test_finder_labels
test: test-finder-labels


build/test_finder_preferences: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_preferences.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_preferences.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-finder-preferences
test-finder-preferences: build/test_finder_preferences
	./build/test_finder_preferences
test: test-finder-preferences

build/test_desktop_surface: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_desktop_surface.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_desktop_surface.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-desktop-surface
test-desktop-surface: build/test_desktop_surface
	./build/test_desktop_surface
test: test-desktop-surface

build/test_icon_position: src/finder_icon_position.c tests/test_icon_position.c include/finder_icon_position.h include/volume_directory.h include/ospi.h
	$(CC) $(CFLAGS) -o $@ src/finder_icon_position.c tests/test_icon_position.c
.PHONY: test-icon-position
test-icon-position: build/test_icon_position
	./build/test_icon_position
test: test-icon-position

build/test_system_icon_positions: src/system_icon_positions.c tests/test_system_icon_positions.c include/system_icon_positions.h include/volume_directory.h include/ospi.h
	$(CC) $(CFLAGS) -o $@ src/system_icon_positions.c tests/test_system_icon_positions.c
build/test_system_icon_drag: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_system_icon_drag.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_system_icon_drag.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-system-icons
test-system-icons: build/test_system_icon_positions build/test_system_icon_drag
	./build/test_system_icon_positions
	./build/test_system_icon_drag
test: test-system-icons

build/test_quake_surface: src/control_panel_store.c include/native_settings.h include/control_panel_store.h $(STORAGE_A) src/application_probe.c include/application_probe.h tests/test_quake_surface.c src/quake_session.c src/native_launch.c src/runtime_bridge.c src/mac_resource.c src/quake_launch.c src/quake_data.c include/quake_data.h include/os999_quake_bridge.h include/native_launch.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_quake_surface.c src/control_panel_store.c src/native_launch.c src/application_probe.c src/runtime_bridge.c src/mac_resource.c src/quake_launch.c src/quake_data.c $(STORAGE_LINK) -pthread
.PHONY: test-quake-surface
test-quake-surface: build/test_quake_surface
	./build/test_quake_surface
test: test-quake-surface

# Desktop PAN backend; UI wiring is owned by the combined integration.
DESKTOP_BT_SRC := src/desktop_guestnet.c src/desktop_bluetooth.c src/desktop_bt_none.c
DESKTOP_BT_HDR := include/desktop_guestnet.h include/desktop_bluetooth.h
build/test_desktop_guestnet build/test_desktop_bluetooth: build/test_desktop_%: tests/test_desktop_%.c $(DESKTOP_BT_SRC) $(DESKTOP_BT_HDR) $(NET_OUT)/libos999net.a $(BT_OUT)/libos999bt.a
	$(CC) $(CFLAGS) -Inet/include -Ibluetooth/include -o $@ $< $(DESKTOP_BT_SRC) $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)

build/test_desktop_pan_guest: tests/test_desktop_pan_guest.c src/desktop_guestnet.c include/desktop_guestnet.h $(BT_SIM_SRC) $(BT_SIM_HDR) $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) $(BT_INC) -o $@ tests/test_desktop_pan_guest.c src/desktop_guestnet.c $(BT_SIM_SRC) $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)

.PHONY: test-desktop-pan
test-desktop-pan: build/test_desktop_guestnet build/test_desktop_bluetooth build/test_desktop_pan_guest
	./build/test_desktop_guestnet
	./build/test_desktop_bluetooth
	./build/test_desktop_pan_guest
test: test-desktop-pan
# OSA scripting components and the JSON Apple Event bridge.
build/test_applescript: src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa.c src/osa_script.c tests/test_applescript.c include/osa_events.h include/osa_json.h include/osa_bridge.h include/osa.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa.c src/osa_script.c tests/test_applescript.c
.PHONY: test-applescript
test-applescript: build/test_applescript
	./build/test_applescript
test: test-applescript

build/test_osa_boundaries: src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa.c src/osa_script.c tests/test_osa_boundaries.c include/osa.h include/osa_json.h include/osa_events.h include/osa_bridge.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa.c src/osa_script.c tests/test_osa_boundaries.c
.PHONY: test-osa-boundaries
test-osa-boundaries: build/test_osa_boundaries
	./build/test_osa_boundaries
test: test-osa-boundaries

build/test_osa_session: src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa.c src/osa_script.c src/osa_session.c src/osa_ipc.c src/desktop_theme.c tests/test_osa_session.c include/osa_session.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa.c src/osa_script.c src/osa_session.c src/osa_ipc.c src/desktop_theme.c tests/test_osa_session.c
.PHONY: test-osa-session
test-osa-session: build/test_osa_session
	./build/test_osa_session
test: test-osa-session

# Production driver exercised at a fake IOBluetooth boundary (no hardware).
build/test_desktop_bt_driver: tests/test_desktop_bt_driver.c src/desktop_bt_darwin.c src/desktop_bluetooth.c src/desktop_guestnet.c $(DESKTOP_BT_HDR) $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) $(BT_INC) -o $@ tests/test_desktop_bt_driver.c src/desktop_bt_darwin.c src/desktop_bluetooth.c src/desktop_guestnet.c $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)
.PHONY: test-desktop-bt-driver
test-desktop-bt-driver: build/test_desktop_bt_driver
	./build/test_desktop_bt_driver
test: test-desktop-bt-driver

build/test_desktop_bt_host: tests/test_desktop_bt_host.c src/desktop_bt_host.c include/desktop_bt_host.h $(DESKTOP_BT_HDR)
	$(CC) $(CFLAGS) $(BT_INC) -DOS999_DESKTOP_BLUETOOTH -o $@ tests/test_desktop_bt_host.c src/desktop_bt_host.c
.PHONY: test-desktop-bt-host
test-desktop-bt-host: build/test_desktop_bt_host
	./build/test_desktop_bt_host
test: test-desktop-bt-host

# Bluetooth panel and its manager-backed source; fake source and fake driver only.
build/test_cdev_bluetooth: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_cdev_bluetooth.c include/cdev_bluetooth.h $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_cdev_bluetooth.c $(FT_LIBS) -lm $(NET_LIBS)
build/test_desktop_bt_view: tests/test_desktop_bt_view.c src/desktop_bt_view.c src/desktop_bluetooth.c src/desktop_guestnet.c src/desktop_bt_none.c include/desktop_bt_view.h include/cdev_bluetooth.h $(DESKTOP_BT_HDR) $(NET_OUT)/libos999net.a $(BT_OUT)/libos999bt.a
	$(CC) $(CFLAGS) $(BT_INC) -DOS999_DESKTOP_BLUETOOTH -o $@ tests/test_desktop_bt_view.c src/desktop_bt_view.c src/desktop_bluetooth.c src/desktop_guestnet.c src/desktop_bt_none.c $(BT_OUT)/libos999bt.a $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)
.PHONY: test-cdev-bluetooth
test-cdev-bluetooth: build/test_cdev_bluetooth build/test_desktop_bt_view
	./build/test_cdev_bluetooth assets/icons/ospi-icons.icl8
	./build/test_desktop_bt_view
test: test-cdev-bluetooth
build/test_quicktime: src/quicktime_decode.c src/quicktime_decode.h src/quicktime_movie.c include/quicktime_movie.h $(AUDIO_SRC) $(AUDIO_HDRS) tests/test_quicktime.c
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ src/quicktime_decode.c src/quicktime_movie.c $(AUDIO_SRC) tests/test_quicktime.c $(SDL_LIBS) $(FFMPEG_LIBS) -pthread -lm
.PHONY: test-quicktime
test-quicktime: build/test_quicktime
	sh scripts/test-quicktime-fixtures.sh
build/fixture_cdev.so: tests/fixture_cdev_plugin.c include/os999_cdev.h include/control_panel_store.h include/draw.h include/ospi.h
	mkdir -p build
	$(CC) $(CFLAGS) -shared -fPIC tests/fixture_cdev_plugin.c -o $@

build/test_controlpanels: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_controlpanels.c $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_controlpanels.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_controlpanels_fx: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_controlpanels_fx.c $(HDRS) include/os999_startup.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_controlpanels_fx.c $(FT_LIBS) -lm $(NET_LIBS)

.PHONY: test-controlpanels
test-controlpanels: build/test_controlpanels build/test_controlpanels_fx build/test_mac_memory build/fixture_cdev.so
	./build/test_controlpanels build/fixture_cdev.so assets/icons/ospi-icons.icl8
	./build/test_controlpanels_fx assets/icons/ospi-icons.icl8
	./build/test_mac_memory
test: test-controlpanels

QT_LIFECYCLE_SRC := $(CORE) $(AUDIO_SRC) $(VIDEO_SRC) src/desktop_host.c src/desktop_sound.c src/media_player_panel.c src/winamp_skin.c $(STARTUP_SRC)
build/test_quicktime_lifecycle: $(QT_LIFECYCLE_SRC) $(STORAGE_A) tests/test_quicktime_lifecycle.c $(HDRS) $(AUDIO_HDRS) include/desktop_host.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ $(QT_LIFECYCLE_SRC) $(STORAGE_LINK) tests/test_quicktime_lifecycle.c $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) $(NET_LIBS) -pthread -lm
test-quicktime: build/test_quicktime_lifecycle

# Safety fixtures deliberately stub unrelated volume APIs; do not link CORE.
build/test_ramdisk_safety: tests/test_ramdisk_safety.c src/volume_folders.c include/ospi.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Werror tests/test_ramdisk_safety.c src/volume_folders.c -o $@
build/test_controlpanels_storage_safety: tests/test_controlpanels_storage_safety.c src/control_panel_store.c src/finder_files.c src/volume_folders.c $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Werror tests/test_controlpanels_storage_safety.c src/control_panel_store.c src/finder_files.c src/volume_folders.c -pthread -o $@
test-controlpanels: test-controlpanels-safety
.PHONY: test-controlpanels-safety
test-controlpanels-safety: build/test_ramdisk_safety build/test_controlpanels_storage_safety
	./build/test_ramdisk_safety
	./build/test_controlpanels_storage_safety

build/test_native_apps: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_native_apps.c $(HDRS) build/os999-tailnet
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_native_apps.c $(FT_LIBS) -lm $(NET_LIBS)
.PHONY: test-native-apps
test-native-apps: build/test_native_apps
	./build/test_native_apps
	./build/test_native_host
test: test-native-apps

build/test_native_host: $(QT_LIFECYCLE_SRC) $(STORAGE_A) tests/test_native_host.c $(HDRS) $(AUDIO_HDRS) include/desktop_host.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ $(QT_LIFECYCLE_SRC) $(STORAGE_LINK) tests/test_native_host.c $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) $(NET_LIBS) -pthread -lm
test-native-apps: build/test_native_host

build/test_documents: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_documents.c $(HDRS)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_documents.c $(FT_LIBS) -lm $(NET_LIBS)

test-documents: build/test_documents
	./build/test_documents

test: test-documents

build/test_desktop_info: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_desktop_info.c $(HDRS) include/desktop_info.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_desktop_info.c $(FT_LIBS) -lm $(NET_LIBS)

test-desktop-info: build/test_desktop_info
	./build/test_desktop_info

test: test-desktop-info
