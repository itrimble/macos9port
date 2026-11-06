#ifndef OS999_DESKTOP_SOUND_H
#define OS999_DESKTOP_SOUND_H

#include <stdint.h>
enum { DESKTOP_SOUND_STARTUP, DESKTOP_SOUND_ALERT, DESKTOP_SOUND_MENU_OPEN,
       DESKTOP_SOUND_MENU_SELECT, DESKTOP_SOUND_COUNT };
/* Initialize once after SDL initialization; paths are explicit, no guest audio access. */
int desktop_sound_init(const char *sound_dir, const char *preference_file);
void desktop_sound_shutdown(void);
int desktop_sound_enabled(void);
int desktop_sound_set_enabled(int enabled);
/* Call each main-loop iteration, before events. Guest activation cancels host sounds. */
void desktop_sound_guest_active(int active);
/* Nonblocking, bounded, drops overlapping/repeated UI effects. */
int desktop_sound_play(int event);
uint32_t desktop_sound_queued_bytes(void);

#endif
