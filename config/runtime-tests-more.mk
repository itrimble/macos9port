build/test_mac_vbl: $(RUNTIME_OBJ) tests/test_mac_vbl.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_vbl.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_pixmap: $(RUNTIME_OBJ) tests/test_mac_pixmap.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_pixmap.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_pixels: $(RUNTIME_OBJ) tests/test_mac_pixels.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_pixels.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_volume: $(RUNTIME_OBJ) tests/test_mac_volume.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_volume.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_window: $(RUNTIME_OBJ) tests/test_mac_window.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_window_nogrow: $(RUNTIME_OBJ) tests/test_mac_window_nogrow.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window_nogrow.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_clock_globals: $(RUNTIME_OBJ) tests/test_mac_clock_globals.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_clock_globals.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_random: $(RUNTIME_OBJ) tests/test_mac_random.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_random.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_supervisor: $(RUNTIME_OBJ) tests/test_mac_supervisor.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_supervisor.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_text_state: $(RUNTIME_OBJ) tests/test_mac_text_state.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_text_state.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_sysfont: $(RUNTIME_OBJ) tests/test_mac_sysfont.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) tests/test_mac_sysfont.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_sound_volume: $(RUNTIME_OBJ) tests/test_mac_sound_volume.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) tests/test_mac_sound_volume.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_scrap: $(RUNTIME_OBJ) tests/test_mac_scrap.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) tests/test_mac_scrap.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_handcopy: $(RUNTIME_OBJ) tests/test_mac_handcopy.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) tests/test_mac_handcopy.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_colorindex: $(RUNTIME_OBJ) tests/test_mac_colorindex.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) tests/test_mac_colorindex.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_files_rf: $(RUNTIME_OBJ) tests/test_mac_files_rf.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) -Isrc $(RUNTIME_OBJ) tests/test_mac_files_rf.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_text: $(RUNTIME_OBJ) tests/test_mac_text.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_text.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_textedit: $(RUNTIME_OBJ) tests/test_mac_textedit.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_textedit.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_rgn: $(RUNTIME_OBJ) tests/test_mac_rgn.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_rgn.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_stdprocs: $(RUNTIME_OBJ) tests/test_mac_stdprocs.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_stdprocs.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_shape: $(RUNTIME_OBJ) tests/test_mac_shape.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_shape.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_menu_color: $(RUNTIME_OBJ) tests/test_mac_menu_color.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menu_color.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_devmgr: $(RUNTIME_OBJ) tests/test_mac_devmgr.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_devmgr.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_dlgmgr: $(RUNTIME_OBJ) tests/test_mac_dlgmgr.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dlgmgr.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_stdput: $(RUNTIME_OBJ) tests/test_mac_stdput.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_stdput.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_toolutil: $(RUNTIME_OBJ) tests/test_mac_toolutil.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_toolutil.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_menuselect: $(RUNTIME_OBJ) tests/test_mac_menuselect.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menuselect.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_script: $(RUNTIME_OBJ) tests/test_mac_script.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_script.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_gworld: $(RUNTIME_OBJ) tests/test_mac_gworld.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_gworld.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_print: $(RUNTIME_OBJ) tests/test_mac_print.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_print.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_mdef: $(RUNTIME_OBJ) tests/test_mac_mdef.c tests/mdef_fixture.h tests/fork_builder.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_mdef.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_controls: $(RUNTIME_OBJ) tests/test_mac_controls.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_controls.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_cdef: $(RUNTIME_OBJ) tests/test_mac_cdef.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_cdef.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_cicn: $(RUNTIME_OBJ) tests/test_mac_cicn.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_cicn.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_dlgwindow: $(RUNTIME_OBJ) tests/test_mac_dlgwindow.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dlgwindow.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_files_flush: $(RUNTIME_OBJ) tests/test_mac_files_flush.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_files_flush.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_sysbeep: $(RUNTIME_OBJ) tests/test_mac_sysbeep.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_sysbeep.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_partition: $(RUNTIME_OBJ) tests/test_mac_partition.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_partition.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_dlgedit: $(RUNTIME_OBJ) tests/test_mac_dlgedit.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dlgedit.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_window_inval: $(RUNTIME_OBJ) tests/test_mac_window_inval.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window_inval.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
build/test_mac_menu_setitem: $(RUNTIME_OBJ) tests/test_mac_menu_setitem.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menu_setitem.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_multi_window: $(RUNTIME_OBJ) tests/test_mac_multi_window.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_multi_window.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_window_resource: $(RUNTIME_OBJ) tests/test_mac_window_resource.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_window_resource.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_paintbehind: $(RUNTIME_OBJ) tests/test_mac_paintbehind.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_paintbehind.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_menu: $(RUNTIME_OBJ) tests/test_mac_menu.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_menu.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_drive_queue: $(RUNTIME_OBJ) tests/test_mac_drive_queue.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_drive_queue.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_reservation: $(RUNTIME_OBJ) tests/test_mac_reservation.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_reservation.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_color_pen: $(RUNTIME_OBJ) tests/test_mac_color_pen.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color_pen.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_system_volume: $(RUNTIME_OBJ) tests/test_mac_system_volume.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_system_volume.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_regions: $(RUNTIME_OBJ) tests/test_mac_regions.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_regions.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_dlgedit build/test_mac_partition build/test_mac_sysbeep build/test_mac_files_flush build/test_mac_dlgwindow build/test_mac_cicn build/test_mac_cdef build/test_mac_controls build/test_mac_menu_records build/test_mac_multi_window build/test_mac_window_resource build/test_mac_paintbehind build/test_mac_menu build/test_mac_drive_queue build/test_mac_reservation build/test_mac_color_pen build/test_mac_system_volume build/test_mac_regions build/test_mac_window build/test_mac_window_nogrow build/test_mac_clock_globals build/test_mac_random build/test_mac_supervisor build/test_mac_text_state build/test_mac_text build/test_mac_sound_volume build/test_mac_scrap build/test_mac_handcopy build/test_mac_colorindex build/test_mac_files_rf build/test_mac_sysfont build/test_mac_textedit build/test_mac_rgn build/test_mac_stdprocs build/test_mac_shape build/test_mac_menu_color build/test_mac_devmgr build/test_mac_dlgmgr build/test_mac_stdput build/test_mac_menuselect build/test_mac_mdef build/test_mac_print build/test_mac_gworld build/test_mac_script build/test_mac_toolutil build/test_mac_window_inval build/test_mac_menu_setitem build/test_mac_volume build/test_mac_pixels build/test_mac_pixmap build/test_mac_vbl

build/test_mac_video_config: $(RUNTIME_OBJ) tests/test_mac_video_config.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_video_config.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

test: build/test_mac_video_config

build/test_mac_palette: $(RUNTIME_OBJ) tests/test_mac_palette.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_palette.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_color_raster: $(RUNTIME_OBJ) tests/test_mac_color_raster.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color_raster.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_copybits: $(RUNTIME_OBJ) tests/test_mac_copybits.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_copybits.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_color_port: $(RUNTIME_OBJ) tests/test_mac_color_port.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color_port.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_dialog: $(RUNTIME_OBJ) tests/test_mac_dialog.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_dialog.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_runtime_cursor: tests/test_runtime_cursor.c src/runtime_video.c $(RUNTIME_HDRS)
	$(CC) $(CFLAGS) -Wno-unused-function $(SDL_CFLAGS) tests/test_runtime_cursor.c -o $@

build/test_mac_cursor: $(RUNTIME_OBJ) tests/test_mac_cursor.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_cursor.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_ccursor: $(RUNTIME_OBJ) tests/test_mac_ccursor.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_ccursor.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_color: $(RUNTIME_OBJ) tests/test_mac_color.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_color.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_files: $(RUNTIME_OBJ) tests/test_mac_files.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_files.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_disk_capacity: $(RUNTIME_OBJ) tests/test_mac_disk_capacity.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_disk_capacity.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_volume_capacity: tests/test_volume_capacity.c src/volume_capacity.c include/volume_capacity.h
	@mkdir -p build
	$(CC) $(CFLAGS) tests/test_volume_capacity.c src/volume_capacity.c -o $@

test: build/test_volume_capacity build/test_mac_disk_capacity

build/test_mac_files_write: $(RUNTIME_OBJ) tests/test_mac_files_write.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_files_write.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

# FSSpec calls (HighLevelFSDispatch) and Gestalt 'fs  '; `test` depends on this aggregate.
.PHONY: test-fsspec
test-fsspec: build/test_mac_fsspec build/test_mac_fsspec_write build/test_mac_fsspec_folders
	./build/test_mac_fsspec
	./build/test_mac_fsspec_write
	./build/test_mac_fsspec_folders
test: test-fsspec

build/test_mac_fsspec: $(RUNTIME_OBJ) tests/test_mac_fsspec.c tests/fsspec_test.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_fsspec.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_fsspec_folders: $(RUNTIME_OBJ) tests/test_mac_fsspec_folders.c tests/fsspec_test.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_fsspec_folders.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_fsspec_write: $(RUNTIME_OBJ) tests/test_mac_fsspec_write.c tests/fsspec_test.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_fsspec_write.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_geometry: $(RUNTIME_OBJ) tests/test_mac_geometry.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_geometry.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_system: $(RUNTIME_OBJ) tests/test_mac_system.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_system.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_mac_raster: $(RUNTIME_OBJ) tests/test_mac_raster.c $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_raster.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

# Compressed resources, a program's own resource load hook, truncated CODE 0 tables, the one-process Process Manager (synthetic forks and guest code only).
build/test_mac_rescomp: $(RUNTIME_OBJ) tests/test_mac_rescomp.c tests/fork_builder.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_mac_rescomp.c $(CPUOBJ) -lm $(FT_LIBS) -o $@
test: build/test_mac_rescomp
