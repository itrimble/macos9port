#ifndef MAC_SYSTEM_VOLUME_H
#define MAC_SYSTEM_VOLUME_H
#include "mac_runtime.h"

typedef struct MacSystemVolume MacSystemVolume;
/* Own authored bytes only. Publishes SysMap=64 and SysResName after validation.
 * The caller owns lifetime; destroy before destroying the CPU. */
MacSystemVolume *mac_system_volume_create(MacRuntime *rt);
void mac_system_volume_destroy(MacSystemVolume *volume);
/* 0 not routed here, 1 handled with OSErr in D0/ioResult, -1 guest fault.
 * Call before the mounted app-data volume dispatcher. */
int mac_system_volume_service(MacRuntime *rt, MacSystemVolume *volume, uint16_t opcode);
#endif
