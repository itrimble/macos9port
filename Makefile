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
	src/install.c src/classic.c src/typeface.c src/native_launch.c src/mac_resource.c
HDRS := include/ospi.h include/draw.h src/session_int.h include/native_launch.h include/mac_resource.h

.PHONY: all test app run clean runtime

all: build/ospi build/ospi-runtime

build/ospi: $(CORE) src/main.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE) src/main.c $(SDL_LIBS) $(FT_LIBS)

build/test_ospi: $(CORE) tests/test_ospi.c $(HDRS)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/test_ospi.c $(FT_LIBS)

# Independent classic runtime. Musashi supplies only CPU instruction execution.
# Its pinned source and generated tables remain in ignored build/deps/.
MUSASHI := build/deps/Musashi-313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd
CPUFLAGS := -DM68K_ILLG_HAS_CALLBACK=1 -I$(MUSASHI)
CPUOBJ := build/m68kcpu.o build/m68kops.o build/softfloat.o
RUNTIME := src/mac_resource.c src/mac_cpu.c src/mac_runtime.c src/mac_services.c src/mac_memory.c src/mac_resource_services.c src/mac_events.c src/mac_quickdraw.c src/mac_toolbox.c src/mac_raster.c src/mac_system.c src/mac_geometry.c src/mac_files.c src/mac_cursor.c src/mac_color.c src/mac_dialog.c src/mac_color_port.c src/mac_copybits.c src/mac_palette.c src/mac_color_raster.c
RUNTIME += src/mac_video_config.c src/mac_vbl.c src/mac_pixmap.c src/mac_pixels.c src/mac_volume.c src/mac_window.c src/mac_window_draw.c src/mac_regions.c src/mac_system_volume.c src/mac_color_pen.c src/mac_drive_queue.c src/mac_menu.c src/mac_menu_records.c src/mac_palette_manager.c src/mac_window_resource.c src/mac_window_registry.c src/font.c
RUNTIME_HDRS := include/mac_resource.h include/mac_cpu.h include/mac_runtime.h include/mac_window_int.h include/mac_system_volume.h include/mac_drive_queue.h include/mac_menu_int.h
RUNTIME += src/mac_resource_catalog.c src/mac_resource_files.c
RUNTIME += src/mac_pict_decode.c src/mac_picture.c
RUNTIME += src/mac_sane.c
RUNTIME += src/mac_sound.c src/mac_sound_render.c
RUNTIME += src/mac_sound_double.c
RUNTIME += src/mac_sound_resample.c
RUNTIME += src/mac_time.c
RUNTIME_HDRS += include/mac_time.h
RUNTIME_HDRS += include/mac_sound_double.h
RUNTIME_HDRS += include/mac_sound.h include/mac_sound_int.h include/runtime_audio.h
RUNTIME_HDRS += include/mac_pict.h
RUNTIME_HDRS += include/mac_resource_chain.h include/mac_resource_chain_int.h

# Compile shared runtime code once per revision, then link each independent test.
# Header and recipe changes invalidate the objects; generated dependency files
# also track nested headers. Keep CPU-only vendor objects in their existing path.
RUNTIME_OBJ := $(patsubst src/%.c,build/runtime/%.o,$(RUNTIME))
-include $(RUNTIME_OBJ:.o=.d)

build/runtime/%.o: src/%.c $(RUNTIME_HDRS) Makefile $(MUSASHI)/m68kops.c
	mkdir -p build/runtime
	$(CC) $(CFLAGS) $(CPUFLAGS) -MMD -MP -c $< -o $@

build/test_mac_resource_chain: $(RUNTIME_OBJ) tests/test_mac_resource_chain.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_resource_chain.c $(CPUOBJ) -lm -o $@

test: build/test_mac_resource_chain

build/test_mac_picture: $(RUNTIME_OBJ) tests/test_mac_picture.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_picture.c $(CPUOBJ) -lm -o $@

test: build/test_mac_picture

build/test_mac_sane: $(RUNTIME_OBJ) tests/test_mac_sane.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sane.c $(CPUOBJ) -lm -o $@

test: build/test_mac_sane

build/test_mac_sound: $(RUNTIME_OBJ) tests/test_mac_sound.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound.c $(CPUOBJ) -lm -o $@

test: build/test_mac_sound

build/test_mac_sound_double: $(RUNTIME_OBJ) tests/test_mac_sound_double.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound_double.c $(CPUOBJ) -lm -o $@

test: build/test_mac_sound_double

build/test_mac_sound_resample: $(RUNTIME_OBJ) tests/test_mac_sound_resample.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound_resample.c $(CPUOBJ) -lm -o $@

test: build/test_mac_sound_resample

build/test_mac_sound_dispose: $(RUNTIME_OBJ) tests/test_mac_sound_dispose.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sound_dispose.c $(CPUOBJ) -lm -o $@

test: build/test_mac_sound_dispose

build/test_mac_time: $(RUNTIME_OBJ) tests/test_mac_time.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_time.c $(CPUOBJ) -lm -o $@

test: build/test_mac_time

$(MUSASHI)/m68kops.c: scripts/prepare-musashi.sh
	sh scripts/prepare-musashi.sh

build/m68kcpu.o: $(MUSASHI)/m68kops.c
	$(CC) -O2 $(CPUFLAGS) -c $(MUSASHI)/m68kcpu.c -o $@

build/m68kops.o: $(MUSASHI)/m68kops.c
	$(CC) -O2 $(CPUFLAGS) -c $(MUSASHI)/m68kops.c -o $@

build/softfloat.o: $(MUSASHI)/m68kops.c
	$(CC) -O2 $(CPUFLAGS) -c $(MUSASHI)/softfloat/softfloat.c -o $@

build/ospi-runtime: $(RUNTIME_OBJ) src/runtime_main.c src/runtime_video.c src/runtime_audio.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(SDL_CFLAGS) $(RUNTIME_OBJ) src/runtime_main.c src/runtime_video.c src/runtime_audio.c $(CPUOBJ) $(SDL_LIBS) -lm -o $@

build/test_mac_resource: src/mac_resource.c tests/test_mac_resource.c include/mac_resource.h
	mkdir -p build
	$(CC) $(CFLAGS) src/mac_resource.c tests/test_mac_resource.c -o $@

build/test_mac_cpu: src/mac_cpu.c tests/test_mac_cpu.c include/mac_cpu.h $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) src/mac_cpu.c tests/test_mac_cpu.c $(CPUOBJ) -lm -o $@

build/test_mac_runtime: $(RUNTIME_OBJ) tests/test_mac_runtime.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_runtime.c $(CPUOBJ) -lm -o $@

build/test_mac_resource_services: $(RUNTIME_OBJ) tests/test_mac_resource_services.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_resource_services.c $(CPUOBJ) -lm -o $@

build/test_mac_memory: $(RUNTIME_OBJ) tests/test_mac_memory.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_memory.c $(CPUOBJ) -lm -o $@

build/test_mac_traps: $(RUNTIME_OBJ) tests/test_mac_traps.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_traps.c $(CPUOBJ) -lm -o $@

build/test_native_launch: src/native_launch.c src/mac_resource.c tests/test_native_launch.c include/native_launch.h include/mac_resource.h
	$(CC) $(CFLAGS) src/native_launch.c src/mac_resource.c tests/test_native_launch.c -o $@

build/test_mac_events: $(RUNTIME_OBJ) tests/test_mac_events.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_events.c $(CPUOBJ) -lm -o $@

build/test_mac_quickdraw: $(RUNTIME_OBJ) tests/test_mac_quickdraw.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_quickdraw.c $(CPUOBJ) -lm -o $@

build/test_runtime_video: tests/test_runtime_video.c src/runtime_video.c $(RUNTIME_HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/test_runtime_video.c -o $@

runtime: build/ospi-runtime

build/test_mac_vbl: $(RUNTIME_OBJ) tests/test_mac_vbl.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_vbl.c $(CPUOBJ) -lm -o $@

build/test_mac_pixmap: $(RUNTIME_OBJ) tests/test_mac_pixmap.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_pixmap.c $(CPUOBJ) -lm -o $@

build/test_mac_pixels: $(RUNTIME_OBJ) tests/test_mac_pixels.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_pixels.c $(CPUOBJ) -lm -o $@

build/test_mac_volume: $(RUNTIME_OBJ) tests/test_mac_volume.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_volume.c $(CPUOBJ) -lm -o $@

build/test_mac_window: $(RUNTIME_OBJ) tests/test_mac_window.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window.c $(CPUOBJ) -lm -o $@

build/test_mac_multi_window: $(RUNTIME_OBJ) tests/test_mac_multi_window.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_multi_window.c $(CPUOBJ) -lm -o $@

build/test_mac_window_resource: $(RUNTIME_OBJ) tests/test_mac_window_resource.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window_resource.c $(CPUOBJ) -lm -o $@

build/test_mac_paintbehind: $(RUNTIME_OBJ) tests/test_mac_paintbehind.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_paintbehind.c $(CPUOBJ) -lm -o $@

build/test_mac_menu: $(RUNTIME_OBJ) tests/test_mac_menu.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menu.c $(CPUOBJ) -lm -o $@

build/test_mac_drive_queue: $(RUNTIME_OBJ) tests/test_mac_drive_queue.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_drive_queue.c $(CPUOBJ) -lm -o $@

build/test_mac_reservation: $(RUNTIME_OBJ) tests/test_mac_reservation.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_reservation.c $(CPUOBJ) -lm -o $@

build/test_mac_color_pen: $(RUNTIME_OBJ) tests/test_mac_color_pen.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color_pen.c $(CPUOBJ) -lm -o $@

build/test_mac_system_volume: $(RUNTIME_OBJ) tests/test_mac_system_volume.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_system_volume.c $(CPUOBJ) -lm -o $@

build/test_mac_regions: $(RUNTIME_OBJ) tests/test_mac_regions.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_regions.c $(CPUOBJ) -lm -o $@

test: build/test_mac_menu_records build/test_mac_multi_window build/test_mac_window_resource build/test_mac_paintbehind build/test_mac_menu build/test_mac_drive_queue build/test_mac_reservation build/test_mac_color_pen build/test_mac_system_volume build/test_mac_regions build/test_mac_window build/test_mac_volume build/test_mac_pixels build/test_mac_pixmap build/test_mac_vbl

build/test_mac_video_config: $(RUNTIME_OBJ) tests/test_mac_video_config.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_video_config.c $(CPUOBJ) -lm -o $@

test: build/test_mac_video_config

build/test_mac_palette: $(RUNTIME_OBJ) tests/test_mac_palette.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_palette.c $(CPUOBJ) -lm -o $@

build/test_mac_color_raster: $(RUNTIME_OBJ) tests/test_mac_color_raster.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color_raster.c $(CPUOBJ) -lm -o $@

build/test_mac_copybits: $(RUNTIME_OBJ) tests/test_mac_copybits.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_copybits.c $(CPUOBJ) -lm -o $@

build/test_mac_color_port: $(RUNTIME_OBJ) tests/test_mac_color_port.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color_port.c $(CPUOBJ) -lm -o $@

build/test_mac_dialog: $(RUNTIME_OBJ) tests/test_mac_dialog.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dialog.c $(CPUOBJ) -lm -o $@

build/test_runtime_cursor: tests/test_runtime_cursor.c src/runtime_video.c $(RUNTIME_HDRS)
	$(CC) $(CFLAGS) -Wno-unused-function $(SDL_CFLAGS) tests/test_runtime_cursor.c -o $@

build/test_mac_cursor: $(RUNTIME_OBJ) tests/test_mac_cursor.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_cursor.c $(CPUOBJ) -lm -o $@

build/test_mac_color: $(RUNTIME_OBJ) tests/test_mac_color.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color.c $(CPUOBJ) -lm -o $@

build/test_mac_files: $(RUNTIME_OBJ) tests/test_mac_files.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_files.c $(CPUOBJ) -lm -o $@

build/test_mac_geometry: $(RUNTIME_OBJ) tests/test_mac_geometry.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_geometry.c $(CPUOBJ) -lm -o $@

build/test_mac_system: $(RUNTIME_OBJ) tests/test_mac_system.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_system.c $(CPUOBJ) -lm -o $@

build/test_mac_raster: $(RUNTIME_OBJ) tests/test_mac_raster.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_raster.c $(CPUOBJ) -lm -o $@

test: build/test_ospi build/test_mac_resource build/test_mac_cpu build/test_mac_runtime build/test_mac_resource_services build/test_mac_memory build/test_mac_traps build/test_native_launch build/test_mac_events build/test_mac_quickdraw build/test_runtime_video build/test_mac_raster build/test_mac_system build/test_mac_geometry build/test_mac_files build/test_mac_cursor build/test_mac_color build/test_mac_dialog build/test_runtime_cursor build/test_mac_color_port build/test_mac_copybits build/test_mac_palette build/test_mac_color_raster
	./build/test_ospi
	./build/test_mac_resource
	./build/test_mac_cpu
	./build/test_mac_runtime
	./build/test_mac_resource_services
	./build/test_mac_resource_chain
	./build/test_mac_picture
	./build/test_mac_sane
	./build/test_mac_sound
	./build/test_mac_sound_double
	./build/test_mac_sound_resample
	./build/test_mac_sound_dispose
	./build/test_mac_time
	./build/test_mac_memory
	./build/test_mac_traps
	./build/test_native_launch
	./build/test_mac_events
	./build/test_mac_quickdraw
	./build/test_runtime_video
	./build/test_mac_raster
	./build/test_mac_system
	./build/test_mac_geometry
	./build/test_mac_files
	./build/test_mac_cursor
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

app: build/ospi build/ospi-runtime macos/Info.plist
	rm -rf "build/OS Pi.app"
	mkdir -p "build/OS Pi.app/Contents/MacOS" "build/OS Pi.app/Contents/Resources/Fonts"
	cp build/ospi "build/OS Pi.app/Contents/MacOS/ospi"
	cp build/ospi-runtime "build/OS Pi.app/Contents/MacOS/ospi-runtime"
	cp $(MUSASHI)/readme.txt "build/OS Pi.app/Contents/Resources/Musashi-license.txt"
	sed -n '1,32p' $(MUSASHI)/softfloat/softfloat.c > "build/OS Pi.app/Contents/Resources/SoftFloat-notice.txt"
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

build/test_mac_menu_records: $(RUNTIME_OBJ) tests/test_mac_menu_records.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menu_records.c $(CPUOBJ) -lm -o $@

build/test_mac_palette_manager: $(RUNTIME_OBJ) tests/test_mac_palette_manager.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_palette_manager.c $(CPUOBJ) -lm -o $@

test: build/test_mac_palette_manager

build/test_mac_dialog_frame: $(RUNTIME_OBJ) tests/test_mac_dialog_frame.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dialog_frame.c $(CPUOBJ) -lm -o $@

test: build/test_mac_dialog_frame
