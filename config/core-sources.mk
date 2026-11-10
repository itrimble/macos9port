# Desktop core sources shared by the desktop Makefile and mobile/mobile.mk.
CORE := src/system_icon_positions.c src/finder_system_icons.c src/finder_icon_position.c src/finder_desktop_positions.c src/finder_desktop_surface.c src/finder_preferences.c src/finder_labels.c src/finder_spring.c src/app_switcher_state.c src/app_switcher.c src/desktop_gesture.c src/app_stack.c src/desktop_paint.c src/notes.c src/session_text.c src/folder_storage.c src/finder_refresh.c src/finder_apps.c src/finder_print.c src/finder_recent.c src/menu_paint.c src/menu_hierarchy.c src/finder_batch.c src/finder_open.c src/finder_drag.c src/finder_selection.c src/finder_popup.c src/finder_state.c src/finder_settings.c src/finder_search.c src/finder_files.c src/finder_alias.c src/desktop_finder.c src/modern_network.c src/folder_navigation.c src/desktop_commands.c src/desktop_help.c src/menu_navigation.c src/folder_view.c src/desktop_theme.c src/desktop_icon_map.c src/appearance_panel.c src/desktop_context.c src/desktop_wallpaper.c src/session_state.c src/typeface_metrics.c src/typeface_discovery.c src/text_layout.c src/text_layout_break.c src/text_system.c src/font_book_parse.c src/font_book_registry.c src/font_book_catalog.c src/font_book_host.c src/font_book_session.c src/font_book_preview.c src/font_book_window.c src/desktop_assets.c assets/icons/c/ospi_icon_set.c src/font.c src/draw.c src/geom.c src/menu.c src/volume.c src/volume_folders.c src/archive_fs.c src/session_sit.c src/mac_icon.c src/mac_icon_palette.c src/runtime_bridge.c \
	src/hostinfo.c src/text.c src/paint.c src/session.c src/input.c src/finder_edit.c \
	src/application_probe.c src/install.c src/install_local.c src/classic.c src/typeface.c src/native_launch.c src/mac_resource.c \
	src/term_tailnet.c src/term_font.c src/term_vt.c src/term_pty.c src/term_console.c src/term_ui.c \
	src/mac_movie.c src/mac_movie_play.c src/movie_window.c src/quake_launch.c src/quake_data.c src/quake_session.c

CORE += src/osa.c src/osa_events.c src/osa_json.c src/osa_bridge.c src/osa_script.c src/osa_session.c src/osa_ipc.c

CORE += src/sound_preferences.c src/control_panel_store.c src/cdev_registry.c src/cdev_ui.c src/cdev_general.c src/cdev_sound.c src/cdev_appearance.c src/cdev_monitors.c src/cdev_keyboard.c src/cdev_energy.c src/cdev_memory.c src/cdev_network.c src/cdev_bluetooth.c src/control_panels.c src/control_panels_input.c src/cdev_extensions.c src/ext_scan.c src/ext_state.c src/ext_ui.c

CORE += src/native_catalog.c src/native_apps.c

CORE += src/desktop_info.c src/os999_private.c src/telemetry.c src/movie_picker.c src/about_panel.c
