#ifndef MAC_TIME_H
#define MAC_TIME_H
#include "mac_runtime.h"
/* CPU-thread only, explicit monotonic microseconds; independent of TickCount.
 * Poll never advances time. Call before each bounded CPU execution slice. */
int mac_time_set(MacRuntime *rt, uint64_t microseconds);
int mac_time_service(MacRuntime *rt, uint16_t opcode);
size_t mac_time_poll(MacRuntime *rt, size_t budget);
void mac_time_destroy(MacRuntime *rt);
#endif
