#ifndef OS999_MEDIA_PLAYER_PANEL_H
#define OS999_MEDIA_PLAYER_PANEL_H

#include "ospi.h"
#include "draw.h"
#include "audio_service.h"

enum { MEDIA_PANEL_PLAYER_TAB = 0, MEDIA_PANEL_FORMATS_TAB = 1 };

void media_panel_init_hooks(void);
void media_panel_shutdown(void);
void media_panel_tick(void);
int media_panel_is_media_file(const char *full_path);
int media_panel_open_file(Session *s, const char *full_path);
void media_panel_open(Session *s, int selected_tab);
void media_panel_close(void);
int media_panel_active(void);
void media_panel_draw(Session *s, Fb *fb);
int media_panel_down(Session *s, int x, int y);
int media_panel_key(Session *s, int key, int mods);
AudioMediaPlayer *media_panel_current_player(void);
int media_panel_launch_vlc(const char *full_path);

#endif
