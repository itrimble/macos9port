# Additional runtime, desktop and asset test targets.
build/test_mac_menu_records: $(RUNTIME_OBJ) tests/test_mac_menu_records.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menu_records.c $(CPUOBJ) -lm -o $@

build/test_mac_palette_manager: $(RUNTIME_OBJ) tests/test_mac_palette_manager.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_palette_manager.c $(CPUOBJ) -lm -o $@

test: build/test_mac_palette_manager

build/test_mac_dialog_frame: $(RUNTIME_OBJ) tests/test_mac_dialog_frame.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dialog_frame.c $(CPUOBJ) -lm -o $@

test: build/test_mac_dialog_frame

build/test_desktop_assets: tests/test_desktop_assets.c src/desktop_assets.c src/mac_icon_palette.c assets/icons/c/ospi_icon_set.c include/desktop_assets.h
	$(CC) $(CFLAGS) tests/test_desktop_assets.c src/desktop_assets.c src/mac_icon_palette.c assets/icons/c/ospi_icon_set.c -o $@

test: build/test_desktop_assets

build/test_desktop_sound: tests/test_desktop_sound.c src/desktop_sound.c src/sound_preferences.c $(AUDIO_SRC) include/sound_preferences.h include/desktop_sound.h $(AUDIO_HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) tests/test_desktop_sound.c src/desktop_sound.c src/sound_preferences.c $(AUDIO_SRC) $(SDL_LIBS) $(FFMPEG_LIBS) -pthread -lm -o $@

test: build/test_desktop_sound

build/test_audio_engine: tests/test_audio_engine.c tests/test_media_app.c $(CORE) $(STORAGE_A) $(AUDIO_SRC) src/winamp_skin.c src/media_player_panel.c src/desktop_sound.c $(STARTUP_SRC) $(HDRS) $(AUDIO_HDRS) include/winamp_skin.h include/media_player_panel.h include/desktop_sound.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) tests/test_audio_engine.c tests/test_media_app.c $(CORE) $(STORAGE_LINK) $(AUDIO_SRC) src/winamp_skin.c src/media_player_panel.c src/desktop_sound.c $(STARTUP_SRC) $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) -pthread -lm -o $@ $(NET_LIBS)

test: build/test_audio_engine

build/test_desktop_guest: $(CORE) $(STORAGE_A) src/desktop_guest.c $(STARTUP_SRC) tests/test_desktop_guest.c $(HDRS) include/desktop_guest.h $(STARTUP_HDR)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) src/desktop_guest.c $(STARTUP_SRC) tests/test_desktop_guest.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_archive_fs: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_archive_fs.c $(HDRS) $(STARTUP_HDR)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_archive_fs.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_desktop_session_ui: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_desktop_session_ui.c $(HDRS) $(STARTUP_HDR)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_desktop_session_ui.c $(FT_LIBS) -lm $(NET_LIBS)

build/test_runtime_audio: $(RUNTIME_OBJ) src/runtime_audio.c tests/test_runtime_audio.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(SDL_CFLAGS) $(RUNTIME_OBJ) src/runtime_audio.c tests/test_runtime_audio.c $(CPUOBJ) $(SDL_LIBS) -lm -o $@

test: build/test_desktop_sound build/test_desktop_guest build/test_archive_fs build/test_desktop_session_ui build/test_runtime_audio


.PHONY: test-desktop-sound wallpapers test-wallpapers
test-desktop-sound: build/test_desktop_sound
	./build/test_desktop_sound assets/sounds/classic9
wallpapers:
	zsh scripts/prepare-wallpapers.sh assets build/wallpapers
build/test_desktop_wallpaper: src/desktop_wallpaper.c src/desktop_wallpaper_catalog.h include/desktop_wallpaper.h tests/test_desktop_wallpaper.c
	mkdir -p build
	$(CC) $(CFLAGS) src/desktop_wallpaper.c tests/test_desktop_wallpaper.c -o $@
test-wallpapers: wallpapers build/test_desktop_wallpaper
	./build/test_desktop_wallpaper build/wallpapers

include config/desktop-tests.mk

build/test_finder_edit: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_finder_edit.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_finder_edit.c $(FT_LIBS) -lm $(NET_LIBS)

test: build/test_finder_edit

build/os999-tailnet: src/tailnet_main.c src/tailnet_json.c src/tailnet_process.c include/tailnet.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/tailnet_main.c src/tailnet_json.c src/tailnet_process.c

.PHONY: test-tailnet
test-tailnet: build/os999-tailnet
	python3 tests/test_tailnet.py


build/test_quake_launch: src/quake_launch.c src/quake_data.c tests/test_quake_launch.c include/quake_launch.h include/quake_data.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/quake_launch.c src/quake_data.c tests/test_quake_launch.c

test: build/test_quake_launch
include config/extensions-tests.mk
