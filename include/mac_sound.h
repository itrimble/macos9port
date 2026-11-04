#ifndef MAC_SOUND_H
#define MAC_SOUND_H
#include "mac_runtime.h"
#define MAC_SOUND_RATE 48000u
int mac_sound_service(MacRuntime *rt,uint16_t opcode);
size_t mac_sound_render(MacRuntime *rt,int16_t *stereo,size_t frames);
size_t mac_sound_poll(MacRuntime *rt,size_t budget);
void mac_sound_destroy(MacRuntime *rt);
#endif
