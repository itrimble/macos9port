# OS999 keyboards and trackpads. Run from this folder:  make -f keyboard.mk test
# (or rename this file to Makefile and drop the -f).
#   make test       headless checks (no SDL)
#   make test-sdl   the SDL event path, with SDL's dummy video driver
#   make demo       folding-phone simulator (SDL2)
#   make run        build and launch the simulator
#   make shots      render every posture and skin to build/shots/*.png
#   make audition   type a sentence on each switch family to build/audition/*.wav
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CPPFLAGS += -Iinclude -I../include -Isrc -D_DEFAULT_SOURCE
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
FT_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null)
FT_LIBS := $(shell pkg-config --libs freetype2 2>/dev/null)
ifneq ($(FT_LIBS),)
CPPFLAGS += $(FT_CFLAGS) -DOSPI_FREETYPE
endif

CORE := src/kbd_core.c src/kbd_keys.c src/kbd_layout.c src/kbd_text.c src/kbd_touch.c \
        src/kbd_pad.c src/kbd_motion.c src/kbd_anim.c src/kbd_gfx.c src/kbd_skin.c src/kbd_draw.c \
        src/kbd_draw_over.c ../src/font.c
AUDIO := src/kbd_synth.c src/kbd_mixer.c
HDRS := $(wildcard include/*.h src/*.h)
FONT := ../fonts/ChicagoFLF.ttf
SDL_SRC := src/kbd_sdl.c src/kbd_haptics.c
ifeq ($(shell uname),Darwin)
SDL_LIBS += -lobjc
endif


.PHONY: all test test-sdl demo run shots audition clean lines

all: build/kbd_demo build/test_kbd

build:
	mkdir -p build

build/test_kbd: $(CORE) $(AUDIO) tests/test_kbd.c $(HDRS) | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE) $(AUDIO) tests/test_kbd.c $(FT_LIBS) -lm -o $@

build/test_pad: $(CORE) tests/test_pad.c $(HDRS) | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE) tests/test_pad.c $(FT_LIBS) -lm -o $@

test: build/test_kbd build/test_pad lines
	./build/test_kbd
	./build/test_pad

build/test_kbd_sdl: $(CORE) $(AUDIO) $(SDL_SRC) tests/test_kbd_sdl.c $(HDRS) | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) $(CORE) $(AUDIO) $(SDL_SRC) tests/test_kbd_sdl.c \
		$(SDL_LIBS) $(FT_LIBS) -lm -o $@

test-sdl: build/test_kbd_sdl
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_kbd_sdl

build/kbd_demo: $(CORE) $(AUDIO) $(SDL_SRC) demo/kbd_demo.c demo/demo_scene.c demo/demo.h $(HDRS) | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) $(CORE) $(AUDIO) $(SDL_SRC) demo/kbd_demo.c \
		demo/demo_scene.c $(SDL_LIBS) $(FT_LIBS) -lm -o $@

demo: build/kbd_demo

run: build/kbd_demo
	./build/kbd_demo --font $(FONT)

build/kbd_shots: $(CORE) tools/kbd_shots.c tools/png.c $(HDRS) | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE) tools/kbd_shots.c tools/png.c $(FT_LIBS) -lm -o $@

shots: build/kbd_shots
	mkdir -p build/shots
	./build/kbd_shots build/shots $(FONT)

build/kbd_audition: $(AUDIO) tools/kbd_audition.c $(HDRS) | build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(AUDIO) tools/kbd_audition.c -lm -o $@

audition: build/kbd_audition
	mkdir -p build/audition
	./build/kbd_audition build/audition

# Project rule: every source file stays under 500 lines.
lines:
	@awk 'FNR==1{if(n>=500)print f": "n" lines";f=FILENAME;n=0}{n++}END{if(n>=500)print f": "n" lines"}' \
		src/*.c src/*.h include/*.h tests/*.c demo/*.c demo/*.h tools/*.c | \
		{ if grep .; then echo "files must stay under 500 lines"; exit 1; fi; true; }

clean:
	rm -rf build
