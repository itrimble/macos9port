#ifndef MAC_SOUND_VOLUME_H
#define MAC_SOUND_VOLUME_H
#include "mac_runtime.h"
/* _SoundDispatch GetDefaultOutputVolume(VAR level: LongInt): OSErr. 1 handled, 0 not this call, -1 halted. */
int mac_sound_volume_service(MacRuntime *rt, uint16_t opcode);
#endif
