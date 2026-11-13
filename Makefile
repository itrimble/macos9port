APP_BUNDLE ?= build/OS999.app
# Quake III (GPL-2) is built in its own repository and only copied into the bundle.
QUAKE_SRC ?= ../macos9port-quake-embedded
QUAKE_BUILD ?= $(QUAKE_SRC)/build/darwin
CC ?= cc
# Prefer the maintained, separately installed curl on macOS; never replace /usr/bin/curl.
CURL_PKG_PATH := $(firstword $(wildcard /opt/homebrew/opt/curl/lib/pkgconfig /usr/local/opt/curl/lib/pkgconfig))
NET_LIBS := $(shell PKG_CONFIG_PATH=$(CURL_PKG_PATH):$(PKG_CONFIG_PATH) pkg-config --libs libcurl) -pthread $(if $(filter Darwin,$(shell uname -s)),,-ldl)
CURL_CFLAGS := $(shell PKG_CONFIG_PATH=$(CURL_PKG_PATH):$(PKG_CONFIG_PATH) pkg-config --cflags libcurl)

CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude -Iassets/icons/c -D_DEFAULT_SOURCE
CFLAGS += $(CURL_CFLAGS)
ifeq ($(OS999_ENABLE_TEST_HOOKS),1)
CFLAGS += -DOS999_ENABLE_TEST_HOOKS=1
endif
ifneq ($(strip $(OS999_BUILD_NUMBER)),)
CFLAGS += -DOS999_BUILD_NUMBER=$(OS999_BUILD_NUMBER)
endif
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libswresample libavutil libswscale 2>/dev/null)
FFMPEG_LIBS := $(shell pkg-config --libs libavformat libavcodec libswresample libavutil libswscale 2>/dev/null)
FT_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null)
FT_LIBS := $(shell pkg-config --libs freetype2 2>/dev/null)
ifneq ($(FT_LIBS),)
CFLAGS += $(FT_CFLAGS) -DOSPI_FREETYPE
endif
HB_CFLAGS := $(shell pkg-config --cflags harfbuzz 2>/dev/null)
HB_LIBS := $(shell pkg-config --libs harfbuzz 2>/dev/null)
FRIBIDI_CFLAGS := $(shell pkg-config --cflags fribidi 2>/dev/null)
FRIBIDI_LIBS := $(shell pkg-config --libs fribidi 2>/dev/null)
UNIBREAK_CFLAGS := $(shell pkg-config --cflags libunibreak 2>/dev/null)
UNIBREAK_LIBS := $(shell pkg-config --libs libunibreak 2>/dev/null)
ifneq ($(and $(FT_LIBS),$(HB_LIBS)),)
CFLAGS += $(HB_CFLAGS) -DOS999_TEXT_HARFBUZZ
FT_LIBS += $(HB_LIBS)
endif
ifneq ($(FRIBIDI_LIBS),)
CFLAGS += $(FRIBIDI_CFLAGS) -DOS999_TEXT_FRIBIDI
FT_LIBS += $(FRIBIDI_LIBS)
endif
ifneq ($(UNIBREAK_LIBS),)
CFLAGS += $(UNIBREAK_CFLAGS) -DOS999_TEXT_UNIBREAK
FT_LIBS += $(UNIBREAK_LIBS)
endif
# Storage stack (storage/storage.mk, L02-L06): archive installs link it.
STORAGE_OUT ?= build/storage
STORAGE_A := $(STORAGE_OUT)/libos999storage.a
STORAGE_LINK = $(STORAGE_A) $(STORAGE_LIBS)
CFLAGS += -Istorage/include

VIDEO_SRC := src/quicktime_decode.c src/quicktime_movie.c src/quicktime_backend.c
AUDIO_SRC := src/audio_decoder.c src/audio_dsp.c src/audio_midi.c src/audio_output.c src/audio_service.c
AUDIO_HDRS := include/sound_preferences.h include/native_apps.h include/movie_host.h include/quicktime_movie.h src/quicktime_decode.h include/osa.h include/osa_bridge.h include/osa_events.h include/osa_json.h include/osa_session.h include/volume_directory.h include/audio_decoder.h include/audio_dsp.h include/audio_midi.h include/audio_output.h include/audio_service.h

# Boot startup renderer and the original pixel-art fruit sprite it draws.
STARTUP_SRC := src/os999_startup.c src/os999_startup_sprite.c
STARTUP_HDR := include/os999_startup.h include/os999_startup_sprite.h

TEXT_RENDER_SRC := src/text.c src/typeface.c src/typeface_discovery.c src/typeface_metrics.c \
	src/text_system.c src/text_layout.c src/text_layout_break.c

include config/core-sources.mk
HDRS := include/display_modes.h include/ext_ui.h include/ext_registry.h include/desktop_info.h include/notes.h src/volume_documents.h include/sound_preferences.h include/native_apps.h include/native_settings.h include/control_panel_store.h include/os999_cdev.h include/control_panels.h src/cdev_ui.h src/control_panels_int.h include/movie_host.h src/install_int.h include/application_probe.h include/quake_launch.h include/term_font.h include/tailnet.h include/finder_edit.h include/app_switcher.h include/desktop_gesture.h include/finder_apps.h include/finder_print.h include/finder_recent.h include/finder_batch.h include/finder_drag.h include/finder_selection.h include/finder_popup.h include/finder_state.h include/finder_settings.h include/finder_search.h include/finder_files.h include/desktop_finder.h include/modern_network.h include/folder_navigation.h include/desktop_commands.h include/desktop_help.h include/desktop_menu_status.h include/menu_navigation.h include/folder_view.h include/desktop_theme.h include/appearance_panel.h include/desktop_context.h include/desktop_wallpaper.h src/desktop_wallpaper_catalog.h include/desktop_assets.h assets/icons/c/ospi_icon_set.h include/ospi.h include/draw.h src/session_int.h include/native_launch.h include/mac_resource.h include/archive_fs.h include/mac_icon.h include/runtime_bridge.h include/term.h include/mac_movie.h include/movie_window.h include/font_book.h include/font_book_window.h include/text_layout.h include/text_system.h

PRINT_HOST := src/desktop_print_stub.c
SHORTCUT_HOST := src/desktop_shortcuts_stub.c
ifeq ($(shell uname -s),Darwin)
SDL_LIBS += -Wl,-headerpad_max_install_names
PRINT_HOST := src/desktop_print_macos.m
SHORTCUT_HOST := src/desktop_shortcuts_macos.m
endif

.PHONY: all test app run clean runtime test-memory

all: build/ospi build/ospi-runtime build/os999-tailnet

# Embedded Chromium window (browser/). macOS presents its GPU frames zero-copy.
BROWSER_SRC := src/browser_window.c src/browser_input.c src/browser_host.c src/browser_stage.c src/browser_paint.c src/browser_script.c src/browser_occlusion.c
BROWSER_HDRS := include/browser_window.h include/browser_overlay.h include/os999_browser_ipc.h src/browser_window_int.h
ifeq ($(shell uname -s),Darwin)
BROWSER_OVERLAY := build/browser_overlay_mac.o
BROWSER_LIBS := -framework Cocoa -framework QuartzCore -framework IOSurface
build/browser_overlay_mac.o: src/browser_overlay_mac.m $(BROWSER_HDRS) include/ospi.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -fobjc-arc -c src/browser_overlay_mac.m -o $@
else
BROWSER_OVERLAY := src/browser_overlay_linux.c
endif
include config/desktop-bluetooth.mk
build/ospi: $(DESKTOP_BT_APP_SRC) $(DESKTOP_BT_APP_HDR) $(DESKTOP_BT_APP_HOST) $(DESKTOP_BT_APP_ARCHIVES) $(SHORTCUT_HOST) include/desktop_shortcuts.h $(PRINT_HOST) $(CORE) $(STORAGE_A) $(AUDIO_SRC) $(VIDEO_SRC) src/winamp_skin.c src/media_player_panel.c $(STARTUP_SRC) src/ext_boot.c src/main.c src/desktop_display.c src/desktop_guest.c src/desktop_host.c src/desktop_sound.c $(HDRS) $(AUDIO_HDRS) include/winamp_skin.h include/media_player_panel.h $(STARTUP_HDR) include/ext_boot.h include/desktop_display.h include/desktop_guest.h include/desktop_host.h include/desktop_sound.h $(BROWSER_SRC) $(BROWSER_OVERLAY) $(BROWSER_HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) $(DESKTOP_BT_APP_INC) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ $(DESKTOP_BT_APP_SRC) $(DESKTOP_BT_APP_HOST) $(SHORTCUT_HOST) $(PRINT_HOST) $(CORE) $(STORAGE_LINK) $(AUDIO_SRC) $(VIDEO_SRC) src/winamp_skin.c src/media_player_panel.c $(STARTUP_SRC) src/ext_boot.c src/main.c src/desktop_display.c src/desktop_guest.c src/desktop_host.c src/desktop_sound.c $(BROWSER_SRC) $(BROWSER_OVERLAY) $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) $(BROWSER_LIBS) -pthread -lm $(NET_LIBS) $(DESKTOP_BT_APP_ARCHIVES) $(DESKTOP_BT_APP_LIBS)

build/test_os999_startup: $(STARTUP_SRC) src/font.c tests/test_os999_startup.c $(STARTUP_HDR) include/draw.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(STARTUP_SRC) src/font.c tests/test_os999_startup.c -lm

build/test_os999_startup_bar: $(STARTUP_SRC) src/font.c tests/test_os999_startup_bar.c $(STARTUP_HDR) include/draw.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(STARTUP_SRC) src/font.c tests/test_os999_startup_bar.c -lm

build/test_ospi: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_ospi.c $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_ospi.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_fontbook: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_fontbook.c $(HDRS) include/os999_startup.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_fontbook.c $(FT_LIBS) -lm $(NET_LIBS)

.PHONY: test-fontbook
build/test_fontbook_host: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_fontbook_host.c $(HDRS) include/os999_startup.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_fontbook_host.c $(FT_LIBS) -lm $(NET_LIBS)

test-fontbook: build/test_fontbook build/test_fontbook_host build/test_text_layout
	./build/test_fontbook
	./build/test_fontbook_host
	./build/test_text_layout
test: test-fontbook

build/test_text_layout: src/text_layout.c src/text_layout_break.c tests/test_text_layout.c include/text_layout.h include/draw.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/text_layout.c src/text_layout_break.c src/draw.c src/font.c src/typeface.c src/typeface_discovery.c src/typeface_metrics.c tests/test_text_layout.c $(FT_LIBS) -lm

.PHONY: test-text-layout
test-text-layout: build/test_text_layout
	./build/test_text_layout

build/test_mac_movie: src/mac_movie.c src/mac_movie_play.c tests/test_mac_movie.c tests/movie_fixture.h include/mac_movie.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/mac_movie.c src/mac_movie_play.c tests/test_mac_movie.c

build/test_movie_window: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_movie_window.c tests/movie_fixture.h $(HDRS) $(STARTUP_HDR) include/mac_movie.h include/movie_window.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_movie_window.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_archive_install: tests/install_network_stub.c $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_archive_install.c $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(filter-out src/modern_network.c,$(CORE) $(STORAGE_LINK)) $(STORAGE_LINK) tests/install_network_stub.c $(STARTUP_SRC) tests/test_archive_install.c $(FT_LIBS) -pthread $(if $(filter Darwin,$(shell uname -s)),,-ldl) -lm

build/test_sit_expand: tests/install_network_stub.c $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_sit_expand.c $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(filter-out src/modern_network.c,$(CORE) $(STORAGE_LINK)) $(STORAGE_LINK) tests/install_network_stub.c $(STARTUP_SRC) tests/test_sit_expand.c $(FT_LIBS) -pthread $(if $(filter Darwin,$(shell uname -s)),,-ldl) -lm

# Independent classic runtime. Musashi supplies only CPU instruction execution.
# Its pinned source and generated tables remain in ignored build/deps/.
MUSASHI := build/deps/Musashi-313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd
CPUFLAGS := -DM68K_ILLG_HAS_CALLBACK=1 -DM68K_INSTRUCTION_HOOK=1 -I$(MUSASHI)
CPUOBJ := build/m68kcpu.o build/m68kops.o build/softfloat.o
RUNTIME := src/mac_resource.c src/mac_cpu.c src/mac_runtime.c src/mac_services.c src/mac_memory.c src/os999_heap.c src/mac_heap_bridge.c src/mac_resource_services.c src/mac_resource_edits.c src/mac_events.c src/mac_quickdraw.c src/mac_toolbox.c src/mac_raster.c src/mac_system.c src/mac_geometry.c src/mac_files.c src/mac_cursor.c src/mac_ccursor.c src/mac_color.c src/mac_dialog.c src/mac_color_port.c src/mac_copybits.c src/mac_palette.c src/mac_color_raster.c
RUNTIME += src/mac_video_config.c src/mac_vbl.c src/mac_pixmap.c src/mac_pixels.c src/mac_volume.c src/mac_window.c src/mac_window_draw.c src/mac_regions.c src/mac_system_volume.c src/mac_color_pen.c src/mac_drive_queue.c src/mac_menu.c src/mac_menu_records.c src/mac_palette_manager.c src/mac_window_resource.c src/mac_window_registry.c src/font.c
RUNTIME_HDRS := include/folder_navigation.h src/runtime_video_pixels.h src/runtime_frame.h src/runtime_pacing.h include/mac_resource.h include/mac_cpu.h include/mac_runtime.h include/mac_window_int.h include/mac_system_volume.h include/mac_drive_queue.h include/mac_menu_int.h include/runtime_bridge.h
RUNTIME += src/mac_resource_catalog.c src/mac_resource_files.c
RUNTIME += src/mac_pict_decode.c src/mac_picture.c
RUNTIME += src/mac_sane.c src/mac_sane_arith.c
RUNTIME += src/mac_numconv.c src/mac_help.c src/mac_appleevents.c
RUNTIME += src/mac_sound.c src/mac_sound_render.c
RUNTIME += src/mac_sound_double.c
RUNTIME += src/mac_sound_resample.c
RUNTIME += src/mac_time.c src/mac_delay.c src/mac_guestcall.c src/mac_ditl.c src/mac_dlgdraw.c src/mac_dlgwindow.c src/mac_stdfile_list.c src/mac_window_inval.c src/mac_random.c src/mac_text_state.c src/mac_font.c src/mac_text.c src/mac_textedit.c src/mac_sysfont.c src/mac_dlgswallow.c src/mac_sound_volume.c src/mac_scrap.c src/mac_resload.c src/mac_handcopy.c src/mac_colorindex.c src/mac_window_title.c src/mac_files_rf.c src/mac_files_hvol.c src/mac_files_crf.c src/mac_cmpstring.c src/mac_alias.c src/mac_controls.c src/mac_cdef.c src/mac_mdef.c src/mac_script.c src/mac_gworld.c src/mac_print.c src/mac_cicn.c src/mac_control_draw.c src/mac_qdcolor.c src/mac_window_stack.c src/mac_menuselect.c src/mac_toolutil.c src/mac_stdprocs.c src/mac_rgn.c src/mac_appfork.c src/mac_dialog_draw.c src/mac_alert.c src/mac_stdput.c src/mac_dlgmgr.c src/mac_devmgr.c src/mac_shape.c src/mac_shapes.c src/mac_stdfile.c src/mac_stdfile_draw.c src/mac_init.c src/mac_env.c src/ext_init.c src/ext_runtime.c src/ext_scan.c src/ext_state.c
RUNTIME += src/mac_files_write.c src/mac_files_hidden.c src/mac_files_flush.c src/mac_files_volparms.c src/mac_sysbeep.c src/mac_textbox.c src/mac_dlgedit.c src/mac_partition.c src/mac_files_dircreate.c src/mac_dlgpict.c src/mac_files_info.c src/mac_fsspec.c src/mac_files_ops.c src/mac_dirs.c src/mac_dirs_name.c src/mac_catinfo.c src/mac_fcbinfo.c src/mac_rescomp.c src/mac_process.c src/mac_rescomp_fork.c
RUNTIME_HDRS += include/mac_mdef.h include/mac_files_int.h include/volume_capacity.h include/mac_rescomp.h include/mac_codefmt.h
RUNTIME += src/volume_capacity.c
RUNTIME_HDRS += include/mac_time.h include/mac_init.h include/ext_init.h include/ext_registry.h include/ext_runtime.h
RUNTIME_HDRS += include/mac_sound_double.h
RUNTIME_HDRS += include/mac_sound.h include/mac_sound_int.h include/runtime_audio.h include/runtime_perf.h include/runtime_keys.h
RUNTIME_HDRS += include/mac_pict.h
RUNTIME_HDRS += include/mac_resource_chain.h include/mac_resource_chain_int.h include/mac_handles.h include/mac_pixpat.h

include config/ppc-tests.mk
# Toolbox calls added while taking real applications further (docs/REAL-APP-COMPATIBILITY.md).
REALAPP_SRC := src/mac_fontnames.c src/mac_handles.c src/mac_pixpat.c src/mac_dlguser.c src/mac_dlgtitle.c
REALAPP_TESTS := test_mac_fontnames test_mac_loadseg_loop
RUNTIME += $(REALAPP_SRC)

# Compile shared runtime code once per revision, then link each independent test.
# Header and recipe changes invalidate the objects; generated dependency files
# also track nested headers. Keep CPU-only vendor objects in their existing path.
RUNTIME_OBJ := $(patsubst src/%.c,build/runtime/%.o,$(RUNTIME))
-include $(RUNTIME_OBJ:.o=.d)
$(REALAPP_TESTS:%=build/%): build/%: tests/%.c tests/fork_builder.h $(RUNTIME_OBJ) $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) $< $(CPUOBJ) -lm $(FT_LIBS) -o $@
test: $(REALAPP_TESTS:%=build/%)

build/runtime/%.o: src/%.c $(RUNTIME_HDRS) Makefile $(MUSASHI)/m68kops.c
	mkdir -p build/runtime
	$(CC) $(CFLAGS) $(CPUFLAGS) -MMD -MP -c $< -o $@

build/test_mac_resource_chain: $(RUNTIME_OBJ) tests/test_mac_resource_chain.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_resource_chain.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_resource_chain
test: build/test_guest_memory

build/test_mac_picture: $(RUNTIME_OBJ) tests/test_mac_picture.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_picture.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_pict_v1: $(RUNTIME_OBJ) tests/test_mac_pict_v1.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_pict_v1.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

include config/runtime-tests.mk
include config/runtime-tests-more.mk

test: build/test_os999_startup build/test_os999_startup_bar build/test_ospi build/test_archive_install build/test_sit_expand build/test_mac_icon build/test_runtime_bridge build/test_mac_resource build/test_mac_cpu build/test_mac_runtime build/test_mac_resource_services build/test_mac_memory build/test_os999_heap build/test_mac_heap_bridge build/test_mac_resource_edits build/test_mac_menubar build/test_mac_traps build/test_mac_unloadseg build/test_mac_guestcall build/test_mac_ditl build/test_mac_files_entries build/test_mac_stdfile_list build/test_mac_stdfile build/test_mac_alert build/test_mac_appfork build/test_native_launch build/test_mac_events build/test_mac_quickdraw build/test_runtime_video build/test_runtime_perf build/test_runtime_keys build/test_mac_raster build/test_mac_system build/test_mac_geometry build/test_mac_files build/test_mac_cursor build/test_mac_ccursor build/test_mac_color build/test_mac_dialog build/test_runtime_cursor build/test_mac_color_port build/test_mac_copybits build/test_mac_palette build/test_mac_color_raster build/test_mac_movie build/test_movie_window build/test_mac_files_write
	./build/test_desktop_assets assets/icons/ospi-icons.icl8
	./build/test_appearance
	./build/test_appearance_panel
	./build/test_desktop_context
	./build/test_desktop_controls
	./build/test_finder_edit
	./build/test_quake_launch
	./build/test_quake_pick
	./build/test_desktop_icon_map
	./build/test_desktop_theme
	./build/test_theme_integration
	./build/test_desktop_commands
	./build/test_finder_interaction
	./build/test_finder_state
	./build/test_finder_desktop
	./build/test_finder_files
	./build/test_folder_navigation
	./build/test_menu_navigation
	./build/test_folder_view
	./build/test_menu_integration
	./build/test_desktop_menus
	./build/test_terminal
	./build/test_terminal_history
	./build/test_telemetry
	./build/test_movie_picker
	./build/test_file_dialog
	./build/test_audio_engine assets/sounds/classic9
	./build/test_os999_startup
	./build/test_os999_startup_bar
	./build/test_modern_network
	./build/test_ospi
	./build/test_archive_install
	./build/test_classic_apps
	./build/test_classic_progress
	./build/test_sit_expand
	./build/test_mac_icon
	./build/test_runtime_bridge
	./build/test_mac_resource
	./build/test_mac_cpu
	./build/test_mac_runtime
	./build/test_mac_resource_services
	./build/test_mac_resource_chain
	./build/test_mac_picture
	./build/test_mac_pict_v1
	./build/test_mac_sane
	./build/test_mac_sane_arith
	./build/test_mac_numconv
	./build/test_mac_help
	./build/test_mac_delay
	./build/test_mac_appleevents
	./build/test_mac_window_colors
	./build/test_mac_setorigin
	./build/test_mac_getnewpalette
	./build/test_mac_palette_activation
	./build/test_mac_pict_depths
	./build/test_mac_setportbits
	./build/test_mac_sound
	./build/test_mac_sound_double
	./build/test_mac_sound_resample
	./build/test_mac_sound_dispose
	./build/test_mac_time
	./build/test_mac_memory
	./build/test_os999_heap
	./build/test_mac_heap_bridge
	./build/test_guest_memory
	./build/test_app_classify
	./build/test_launch_profile
	./build/test_legacy_pointers
	./build/test_app_mode
	./build/test_mac_resource_edits
	./build/test_mac_menubar
	./build/test_mac_traps
	./build/test_mac_unloadseg
	./build/test_mac_rescomp
	./build/test_mac_guestcall
	./build/test_mac_ditl
	./build/test_mac_files_entries
	./build/test_mac_stdfile_list
	./build/test_mac_stdfile
	./build/test_mac_alert
	./build/test_mac_appfork
	./build/test_native_launch
	./build/test_mac_events
	./build/test_mac_quickdraw
	./build/test_runtime_video
	./build/test_runtime_perf
	./build/test_runtime_keys
	./build/test_mac_raster
	./build/test_mac_system
	./build/test_mac_geometry
	./build/test_mac_files
	./build/test_mac_files_write
	./build/test_volume_capacity
	./build/test_mac_disk_capacity
	./build/test_mac_cursor
	./build/test_mac_ccursor
	./build/test_mac_color
	./build/test_mac_dialog
	./build/test_runtime_cursor
	./build/test_mac_color_port
	./build/test_mac_copybits
	./build/test_mac_palette
	./build/test_mac_color_raster
	./build/test_mac_video_config
	./build/test_mac_vbl
	./build/test_mac_pixmap
	./build/test_mac_pixels
	./build/test_mac_volume
	./build/test_mac_window
	./build/test_mac_window_nogrow
	./build/test_mac_clock_globals
	./build/test_mac_random
	./build/test_mac_supervisor
	./build/test_mac_text_state
	./build/test_mac_text
	./build/test_mac_sysfont
	./build/test_mac_sound_volume
	./build/test_mac_scrap
	./build/test_mac_handcopy
	./build/test_mac_colorindex
	./build/test_mac_files_rf
	./build/test_mac_textedit
	./build/test_mac_rgn
	./build/test_mac_stdprocs
	./build/test_mac_shape
	./build/test_mac_menu_color
	./build/test_mac_devmgr
	./build/test_mac_dlgmgr
	./build/test_mac_stdput
	./build/test_mac_menuselect
	./build/test_mac_mdef
	./build/test_mac_print
	./build/test_mac_gworld
	./build/test_mac_script
	./build/test_mac_toolutil
	./build/test_mac_window_inval
	./build/test_mac_controls
	./build/test_mac_cdef
	./build/test_mac_cicn
	./build/test_mac_dlgwindow
	./build/test_mac_files_flush
	./build/test_mac_sysbeep
	./build/test_mac_partition
	./build/test_mac_dlgedit
	./build/test_mac_regions
	./build/test_mac_system_volume
	./build/test_mac_color_pen
	./build/test_mac_reservation
	./build/test_mac_drive_queue
	./build/test_mac_menu
	./build/test_mac_paintbehind
	./build/test_mac_window_resource
	./build/test_mac_multi_window
	./build/test_mac_dialog_frame
	./build/test_mac_palette_manager
	./build/test_mac_menu_records
	./build/test_mac_menu_setitem
	./build/test_desktop_sound assets/sounds/classic9
	./build/test_desktop_guest
	./build/test_archive_fs
	./build/test_desktop_session_ui
	./build/test_runtime_audio
	./build/test_mac_containers
	./build/test_mac_writable_files
	./build/test_mac_controls_textedit
	./build/test_mac_quickdraw_accel
	./build/test_desktop_themes
	./build/test_mac_quicktime
	./build/test_browser_ipc
	./build/test_browser_stage
	./build/test_frame_dump
	./build/test_mac_network_policy
	./build/test_mac_pef_cfm
	./build/test_ppc_cpu
	./build/test_ppc_fp
	./build/test_mac_pef
	./build/test_mac_cfm_reloc
	./build/test_mac_cfm
	./build/test_mac_ppc_probe
	./build/test_mac_ppc_bridge
	./build/test_mac_ppc_mdef
	./build/test_mac_ppc_gworld
	./build/test_mac_ppc_libs
	for t in $(REALAPP_TESTS); do ./build/$$t || exit 1; done
	./build/test_mac_perf_audio_gates
	./build/test_browser_media_gateway
	./build/test_mac_fuzz_soak
	./build/test_platform_sandbox_audit
	./build/test_cleanroom_release_gate
	./build/test_mac_movie
	./build/test_movie_window

app: build/ospi build/ospi-runtime build/os999-tailnet macos/Info.plist wallpapers
	rm -rf "$(APP_BUNDLE)"
	mkdir -p "$(APP_BUNDLE)/Contents/MacOS" "$(APP_BUNDLE)/Contents/Resources/Fonts"
	cp build/ospi "$(APP_BUNDLE)/Contents/MacOS/ospi"
	cp build/ospi-runtime "$(APP_BUNDLE)/Contents/MacOS/ospi-runtime"
	cp build/os999-tailnet "$(APP_BUNDLE)/Contents/MacOS/os999-tailnet"
	@if [ -x "$(QUAKE_BUILD)/quake3" ]; then mkdir -p "$(APP_BUNDLE)/Contents/Resources/Quake3" && cp -R "$(QUAKE_BUILD)/quake3" "$(QUAKE_BUILD)/baseq3" "$(APP_BUNDLE)/Contents/Resources/Quake3/" && cp "$(QUAKE_SRC)/COPYING.txt" "$(APP_BUNDLE)/Contents/Resources/Quake3/COPYING-GPL2.txt" && echo "Packaged Quake III from $(QUAKE_BUILD)"; else echo "Quake III not packaged: $(QUAKE_BUILD)/quake3 missing"; fi
	cp $(MUSASHI)/readme.txt "$(APP_BUNDLE)/Contents/Resources/Musashi-license.txt"
	sed -n '1,32p' $(MUSASHI)/softfloat/softfloat.c > "$(APP_BUNDLE)/Contents/Resources/SoftFloat-notice.txt"
	cp fonts/ChicagoFLF.ttf fonts/LICENSE.txt "$(APP_BUNDLE)/Contents/Resources/Fonts/"
	cp -R fonts/terminal "$(APP_BUNDLE)/Contents/Resources/Fonts/Terminal"
	@if [ -d fonts/system ]; then cp -R fonts/system "$(APP_BUNDLE)/Contents/Resources/Fonts/System"; fi
	@mkdir -p "$(APP_BUNDLE)/Contents/Resources/Fonts/Dashboard" && cp assets/dashboard/plex-*.ttf assets/dashboard/FONT-LICENSE.txt "$(APP_BUNDLE)/Contents/Resources/Fonts/Dashboard/"
	mkdir -p "$(APP_BUNDLE)/Contents/Resources/sounds/classic9"
	cp assets/sounds/classic9/*.wav "$(APP_BUNDLE)/Contents/Resources/sounds/classic9/"
	mkdir -p "$(APP_BUNDLE)/Contents/Resources/wallpapers"
	cp build/wallpapers/*.bmp "$(APP_BUNDLE)/Contents/Resources/wallpapers/"
	cp assets/icons/ospi-icons.icl8 "$(APP_BUNDLE)/Contents/Resources/"
	cp macos/Info.plist "$(APP_BUNDLE)/Contents/Info.plist"
	printf 'APPL????' > "$(APP_BUNDLE)/Contents/PkgInfo"
	@if [ -f "$(QUAKE_BUILD)/arena111-package.json" ]; then python3 scripts/quake_package.py "$(QUAKE_BUILD)" "$(APP_BUNDLE)/Contents/Resources/Quake3"; elif [ -d "$(APP_BUNDLE)/Contents/Resources/Quake3" ]; then codesign --force --sign - "$(APP_BUNDLE)/Contents/Resources/Quake3/baseq3/"*.so; fi
	@if [ -d "$(APP_BUNDLE)/Contents/Resources/Quake3" ]; then codesign --force --sign - "$(APP_BUNDLE)/Contents/Resources/Quake3/quake3"; fi
	codesign --force --sign - "$(APP_BUNDLE)"

ifeq ($(shell uname -s),Darwin)
run: app
	open "$(APP_BUNDLE)"
else
run: build/ospi
	./build/ospi --volume "$(CURDIR)/volume"
endif

clean:
	rm -rf build

include config/extra-tests.mk
include config/classic-apps.mk
