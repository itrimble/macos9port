/* macOS Bluetooth accelerator (IOBluetooth).
 *
 * IOBluetooth does not expose raw HCI packets, so this backend cannot
 * replace the controller engine. It does two things the operating
 * system does better than we can: it finds nearby devices, and it opens
 * an L2CAP channel that the system has already framed. The channel is
 * handed to the stack as a byte pipe, so RFCOMM and BNEP run unchanged
 * on top of it.
 *
 * Objective-C against IOBluetooth; the rest of the stack is plain C. */
#ifndef BT_DARWIN_H
#define BT_DARWIN_H

#include "os999_bluetooth.h"

/* A byte pipe to a channel the system opened for us. */
typedef struct {
    long (*write)(void *ctx, const void *p, size_t n);
    long (*read)(void *ctx, void *p, size_t cap);
    void *ctx;
} BtDarwinPipe;

typedef struct BtDarwin BtDarwin;

/* Reported for every device an inquiry finds. */
typedef void (*BtDarwinDeviceFn)(void *ctx, const BtAddress *addr, const char *name,
                                 uint32_t class_of_device);
/* Reported once the L2CAP channel is up; the pipe stays valid until the
 * channel closes. */
typedef void (*BtDarwinOpenFn)(void *ctx, uint16_t handle, const BtDarwinPipe *pipe);
typedef void (*BtDarwinDataFn)(void *ctx, const uint8_t *p, size_t n);
typedef void (*BtDarwinCloseFn)(void *ctx);

BtDarwin *bt_darwin_new(void);
void bt_darwin_free(BtDarwin *d);
/* Process pending callbacks on the caller (main) run loop without waiting. */
void bt_darwin_poll(BtDarwin *d);
/* Run a standard inquiry for the given number of seconds. */
typedef void (*BtDarwinDoneFn)(void *ctx, int status);
int bt_darwin_discover(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn, void *ctx);
/* Completion-aware variant; cancellation suppresses callbacks. */
int bt_darwin_discover_with_done(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn,
                                BtDarwinDoneFn done, void *ctx);
int bt_darwin_local_address(BtDarwin *d, BtAddress *out);
int bt_darwin_stop_discover(BtDarwin *d);
/* Open an L2CAP channel to a peer and report the pipe when it is up. */
int bt_darwin_open_l2cap(BtDarwin *d, const BtAddress *peer, uint16_t psm,
                         BtDarwinOpenFn on_open, BtDarwinDataFn on_data, BtDarwinCloseFn on_close,
                         void *ctx);
int bt_darwin_close_l2cap(BtDarwin *d);
/* 1 when the system Bluetooth stack is present and usable. */
int bt_darwin_available(void);

#endif
