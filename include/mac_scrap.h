#ifndef MAC_SCRAP_H
#define MAC_SCRAP_H
#include "mac_runtime.h"
/* Scrap Manager subset: ZeroScrap $A9FC, PutScrap $A9FE, GetScrap $A9FD (an absent type only). 1 handled, 0 not ours, -1 halted. */
int mac_scrap_service(MacRuntime *rt, uint16_t opcode);
void mac_scrap_destroy(MacRuntime *rt);
#endif
