#ifndef MAC_MENU_INT_H
#define MAC_MENU_INT_H
#include "mac_runtime.h"
MacHandle *mac_menu_record(MacRuntime *rt,uint32_t handle,unsigned *items);
MacHandle *mac_menu_list(MacRuntime *rt,unsigned *count);
int mac_menu_records_service(MacRuntime *rt,uint16_t opcode);
#endif
