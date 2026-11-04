#ifndef MAC_DRIVE_QUEUE_H
#define MAC_DRIVE_QUEUE_H
#include "mac_runtime.h"
/* Logical readonly drives: 1 app data, 2 authored System. Capacity in 512-byte
 * blocks. No disk-driver refnum or raw block I/O is provided. */
int mac_drive_mount(MacRuntime *rt, unsigned drive, uint32_t blocks);
unsigned mac_drive_number(const MacRuntime *rt, unsigned drive);
void mac_drive_unmount(MacRuntime *rt, unsigned drive);
/* Clear guest links even after a halt, then release host ownership. */
void mac_drive_destroy(MacRuntime *rt);
#endif
