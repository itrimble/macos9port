CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude -D_DEFAULT_SOURCE
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
FT_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null)
FT_LIBS := $(shell pkg-config --libs freetype2 2>/dev/null)
ifneq ($(FT_LIBS),)
CFLAGS += $(FT_CFLAGS) -DOSPI_FREETYPE
endif

CORE := src/font.c src/draw.c src/geom.c src/menu.c src/volume.c \
	src/hostinfo.c src/text.c src/paint.c src/session.c src/input.c \
	src/install.c src/classic.c src/typeface.c
HDRS := include/ospi.h include/draw.h src/session_int.h

.PHONY: all test app run clean

all: build/ospi

build/ospi: $(CORE) src/main.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE) src/main.c $(SDL_LIBS) $(FT_LIBS)

build/test_ospi: $(CORE) tests/test_ospi.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/test_ospi.c $(FT_LIBS)

test: build/test_ospi
	./build/test_ospi

app: build/ospi macos/Info.plist
	rm -rf "build/OS Pi.app"
	mkdir -p "build/OS Pi.app/Contents/MacOS" "build/OS Pi.app/Contents/Resources/Fonts"
	cp build/ospi "build/OS Pi.app/Contents/MacOS/ospi"
	cp fonts/ChicagoFLF.ttf fonts/LICENSE.txt "build/OS Pi.app/Contents/Resources/Fonts/"
	cp macos/Info.plist "build/OS Pi.app/Contents/Info.plist"
	printf 'APPL????' > "build/OS Pi.app/Contents/PkgInfo"
	codesign --force --sign - "build/OS Pi.app"

ifeq ($(shell uname -s),Darwin)
run: app
	open "build/OS Pi.app"
else
run: build/ospi
	./build/ospi --volume "$(CURDIR)/volume"
endif

clean:
	rm -rf build
