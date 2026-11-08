# OS999 on phones. Run from the repository root:
#   make -f mobile/mobile.mk test     headless checks (no SDL)
#   make -f mobile/mobile.mk shots    render every device and posture to mobile/assets/shots
#   make -f mobile/mobile.mk tablets  tablets with a keyboard case, folio or Bluetooth keyboard
#   make -f mobile/mobile.mk icons    regenerate app icons and splash art
#   make -f mobile/mobile.mk sim      build the SDL folding-phone simulator
#   make -f mobile/mobile.mk run-sim  build and launch it (DEVICE=pixel-10-pro-fold)
# The keyboards live in keyboard/ in this tree; point
# KBD_DIR elsewhere if needed.
KBD_DIR ?= keyboard
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
MCPPFLAGS := -Iinclude -Iassets/icons/c -Imobile/include -I$(KBD_DIR)/include -I$(KBD_DIR)/src -D_DEFAULT_SOURCE
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
FT_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null)
FT_LIBS := $(shell pkg-config --libs freetype2 2>/dev/null)
ifneq ($(FT_LIBS),)
MCPPFLAGS += $(FT_CFLAGS) -DOSPI_FREETYPE
endif
OUT := build/mobile

include config/core-sources.mk
include storage/storage.mk
STORAGE_A := $(STORAGE_OUT)/libos999storage.a
MCPPFLAGS += -Istorage/include
CURL_PKG_PATH := $(shell brew --prefix curl 2>/dev/null)/lib/pkgconfig
NET_CFLAGS := $(shell PKG_CONFIG_PATH=$(CURL_PKG_PATH):$(PKG_CONFIG_PATH) pkg-config --cflags libcurl 2>/dev/null)
NET_LIBS := $(shell PKG_CONFIG_PATH=$(CURL_PKG_PATH):$(PKG_CONFIG_PATH) pkg-config --libs libcurl 2>/dev/null)
MCPPFLAGS += $(NET_CFLAGS)
DESK := $(CORE) src/os999_startup.c
KBD := $(addprefix $(KBD_DIR)/src/,kbd_core.c kbd_keys.c kbd_layout.c kbd_text.c kbd_touch.c \
	kbd_pad.c kbd_motion.c kbd_anim.c kbd_gfx.c kbd_skin.c kbd_draw.c kbd_draw_over.c kbd_synth.c kbd_mixer.c)
MOBILE := $(wildcard mobile/src/*.c)
HDRS := $(wildcard include/*.h mobile/include/*.h $(KBD_DIR)/include/*.h $(KBD_DIR)/src/*.h)
SHARED := $(DESK) $(KBD) $(MOBILE)

.PHONY: test shots tablets icons sim run-sim lines clean

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_mobile: $(SHARED) $(STORAGE_A) mobile/tests/test_mobile.c mobile/tests/test_mobile_shell.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(MCPPFLAGS) $(SHARED) $(STORAGE_A) mobile/tests/test_mobile.c mobile/tests/test_mobile_shell.c $(FT_LIBS) $(NET_LIBS) $(STORAGE_LIBS) -pthread -lm -o $@

test: $(OUT)/test_mobile lines
	./$(OUT)/test_mobile

$(OUT)/mobile_shots: $(SHARED) $(STORAGE_A) mobile/tools/mobile_shots.c mobile/tools/mobile_png.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(MCPPFLAGS) $(SHARED) $(STORAGE_A) -Imobile/tools mobile/tools/mobile_shots.c mobile/tools/mobile_png.c $(FT_LIBS) $(NET_LIBS) $(STORAGE_LIBS) -pthread -lz -lm -o $@

shots: $(OUT)/mobile_shots wallpapers-if-mac
	./$(OUT)/mobile_shots mobile/assets/shots

$(OUT)/mobile_tablets: $(SHARED) $(STORAGE_A) mobile/tools/mobile_tablets.c mobile/tools/mobile_png.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(MCPPFLAGS) $(SHARED) $(STORAGE_A) -Imobile/tools mobile/tools/mobile_tablets.c mobile/tools/mobile_png.c $(FT_LIBS) $(NET_LIBS) $(STORAGE_LIBS) -pthread -lz -lm -o $@

tablets: $(OUT)/mobile_tablets wallpapers-if-mac
	./$(OUT)/mobile_tablets mobile/assets/shots/tablets

$(OUT)/mobile_icons: mobile/tools/mobile_icons.c mobile/tools/mobile_png.c mobile/src/mobile_strip.c $(KBD_DIR)/src/kbd_gfx.c src/font.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(MCPPFLAGS) -Imobile/tools mobile/tools/mobile_icons.c mobile/tools/mobile_png.c mobile/src/mobile_strip.c mobile/src/mobile_device.c $(KBD_DIR)/src/kbd_gfx.c src/font.c $(FT_LIBS) -lz -lm -o $@

icons: $(OUT)/mobile_icons
	./$(OUT)/mobile_icons mobile/assets

MOBILE_AUDIO := src/media_player_panel.c src/winamp_skin.c src/audio_decoder.c src/audio_dsp.c src/audio_midi.c src/audio_output.c src/audio_service.c
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libswresample libavutil libswscale 2>/dev/null)
FFMPEG_LIBS := $(shell pkg-config --libs libavformat libavcodec libswresample libavutil libswscale 2>/dev/null)
SIM := src/quicktime_decode.c src/quicktime_movie.c src/quicktime_backend.c $(MOBILE_AUDIO) mobile/sdl/mobile_main.c mobile/sdl/mobile_platform.c src/desktop_host.c src/desktop_sound.c $(KBD_DIR)/src/kbd_sdl.c $(KBD_DIR)/src/kbd_haptics.c
ifeq ($(shell uname -s),Darwin)
SDL_LIBS += -lobjc
endif
$(OUT)/os999-mobile-sim: $(SHARED) $(STORAGE_A) $(SIM) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(MCPPFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) $(SHARED) $(STORAGE_A) $(SIM) $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) $(NET_LIBS) $(STORAGE_LIBS) -pthread -lm -o $@

sim: $(OUT)/os999-mobile-sim

DEVICE ?= pixel-10-pro-fold
run-sim: sim wallpapers-if-mac
	./$(OUT)/os999-mobile-sim --device $(DEVICE) --volume "$(CURDIR)/build/mobile/Volume"

wallpapers-if-mac:
	@if [ "$$(uname -s)" = Darwin ]; then $(MAKE) -s wallpapers; fi

lines:
	@awk 'FNR==500{print FILENAME" reaches 500 lines"; bad=1} END{exit bad}' mobile/src/*.c mobile/include/*.h mobile/sdl/*.c mobile/tests/*.c mobile/tools/*.c

clean:
	rm -rf $(OUT)
