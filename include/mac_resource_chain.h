#ifndef MAC_RESOURCE_CHAIN_H
#define MAC_RESOURCE_CHAIN_H
#include "mac_runtime.h"
#define MAC_APP_RES_REF 1
#define MAC_SYSTEM_RES_REF 64
const MacResourceFork *mac_resource_owner_fork(MacRuntime *rt, int16_t owner);
int mac_resource_chain_find(MacRuntime *rt, uint32_t type, int16_t id,
                           int one, MacResource *out, int16_t *owner);
int mac_resource_chain_at(MacRuntime *rt, uint32_t type, unsigned index,
                         int one, MacResource *out, int16_t *owner);
int mac_resource_chain_type_exists(MacRuntime *rt, uint32_t type, int one);
unsigned mac_resource_chain_count(MacRuntime *rt, uint32_t type, int one);
uint32_t mac_runtime_resource_owned(MacRuntime *rt, uint32_t type, int16_t id, int16_t owner);
int mac_resource_file_service(MacRuntime *rt, uint16_t opcode);
void mac_resource_chain_destroy(MacRuntime *rt);
int mac_files_directory_fd(const MacRuntime *rt);
/* Opens the resource file named by the Pascal string at guest `pointer` (the overlay's writable sidecar first, else the application volume's export). */
int mac_resource_open_by_pointer(MacRuntime *rt,uint32_t pointer,int16_t *ref);
#endif
