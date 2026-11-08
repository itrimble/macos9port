#ifndef OS999_SOUND_PREFERENCES_H
#define OS999_SOUND_PREFERENCES_H
/* Interface effects only. Media and guest audio retain independent controls. */
enum { OS999_EFFECT_VOLUME_PERCENT = 20 };
int sound_preference_enabled(const char *path);
int sound_preference_for_volume(const char *root);
#endif
