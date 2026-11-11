#ifndef OS999_CONTROL_PANELS_H
#define OS999_CONTROL_PANELS_H

#include "draw.h"
#include "ospi.h"
#include "os999_cdev.h"

typedef struct {
    void (*set_volume)(int percent);
    void (*set_muted)(int muted);
    void (*play_sound)(const char *sound_name);
} ControlPanelsAudioHooks;

int control_panels_sound_mute_changed(Session *s, int muted);

void control_panels_set_audio_hooks(const ControlPanelsAudioHooks *hooks);

int control_panels_session_init(Session *s);
void control_panels_session_shutdown(Session *s);
os999_cp_store_t *control_panels_store(Session *s);
const os999_cdev_host_t *control_panels_host(Session *s);

void control_panels_open(Session *s);
int control_panels_open_module(Session *s, const char *module_id);
void control_panels_return_to_grid(Session *s);
const char *control_panels_active_module_id(void);
int control_panels_selected_index(void);
int control_panels_category_filter(void);
void control_panels_set_category_filter(int category);

int control_panels_active(void);
int control_panels_can_open(void);
int control_panels_regions(const Session *s, Rect out[2]);
void control_panels_close(void);
void control_panels_deactivate(void);
void control_panels_blur(void);
/* The desktop size changed: forget the dragged position and re-centre. */
void control_panels_display_changed(void);

void control_panels_draw(Session *s, Fb *fb);
int control_panels_down(Session *s, int x, int y, int clicks);
int control_panels_move(Session *s, int x, int y);
int control_panels_up(Session *s, int x, int y);
int control_panels_wheel(Session *s, int dy);
int control_panels_context(Session *s, int x, int y);
int control_panels_key(Session *s, int key, int mods);

#endif
