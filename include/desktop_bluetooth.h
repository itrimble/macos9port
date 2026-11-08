#ifndef OS999_DESKTOP_BLUETOOTH_H
#define OS999_DESKTOP_BLUETOOTH_H

#include "os999_bluetooth.h"
#include "os999_guestnet.h"
#include "desktop_guestnet.h"

/* The link states the panel shows. */
enum { DBT_IDLE = 0, DBT_SCANNING, DBT_CONNECTING, DBT_UP, DBT_FAILED };
enum { DBT_MAX_DEVICES = 32, DBT_NAME_MAX = 64 };

/* The address the desktop has on the link, and the peer's. */
#define DBT_PAN_LOCAL_IP 0xC0A80A01u  /* 192.168.10.1 */
#define DBT_PAN_PEER_IP  0xC0A80A02u  /* 192.168.10.2 */
#define DBT_PAN_NETMASK  0xFFFFFF00u  /* 255.255.255.0 */

typedef struct {
    BtAddress addr;
    char name[DBT_NAME_MAX];
    uint32_t class_of_device;
} DesktopBtDevice;

typedef struct DesktopBtDriver DesktopBtDriver;

/* Callbacks run on the caller's thread from scan/link_up/poll. A driver must
 * cancel pending work in link_down/free; it must report down/failed before
 * destroying the PAN so the manager can detach its broker endpoints first.
 * The caller owns net and must keep it alive until the manager is freed. */
typedef struct {
    void (*found)(void *ctx, const BtAddress *addr, const char *name, uint32_t cod);
    void (*scan_done)(void *ctx);
    void (*up)(void *ctx);
    void (*down)(void *ctx);
    void (*failed)(void *ctx, int error);
    void *ctx;
} DesktopBtCallbacks;

/* What the driver does. link_up returns BT_OK when the link is already up,
 * BT_EAGAIN when it is still coming up (the driver then reports up), or a
 * negative BT_* code when it failed. */
typedef struct {
    DesktopBtDriver *(*create)(const DesktopBtCallbacks *cb);
    int  (*scan)(DesktopBtDriver *d, int seconds);
    int  (*link_up)(DesktopBtDriver *d, const BtAddress *peer);
    void (*link_down)(DesktopBtDriver *d);
    void (*poll)(DesktopBtDriver *d);
    GnPan *(*pan)(DesktopBtDriver *d);
    void (*free)(DesktopBtDriver *d);
} DesktopBtDriverOps;

typedef struct DesktopBtManager DesktopBtManager;

DesktopBtManager *desktop_bt_new(const DesktopBtDriverOps *ops, DesktopGuestNet *net);
void desktop_bt_free(DesktopBtManager *m);

/* Start a scan. The devices that answer arrive through the driver. */
int desktop_bt_scan(DesktopBtManager *m, int seconds);
/* Bring the link up to a peer. */
int desktop_bt_connect(DesktopBtManager *m, const BtAddress *peer);
void desktop_bt_disconnect(DesktopBtManager *m);
/* Move the frames that arrived on the link into the guest stack. */
void desktop_bt_poll(DesktopBtManager *m);

int desktop_bt_state(const DesktopBtManager *m);
const char *desktop_bt_state_text(const DesktopBtManager *m);
int desktop_bt_error(const DesktopBtManager *m);
int desktop_bt_device_count(const DesktopBtManager *m);
const DesktopBtDevice *desktop_bt_device_at(const DesktopBtManager *m, int index);
const DesktopBtDevice *desktop_bt_device_find(const DesktopBtManager *m, const BtAddress *addr);
GnPan *desktop_bt_pan(const DesktopBtManager *m);
const BtAddress *desktop_bt_peer(const DesktopBtManager *m);
/* The address the guest has on the link. */
uint32_t desktop_bt_local_ip(const DesktopBtManager *m);

/* The desktop's one manager. ops NULL means the platform driver. */
int desktop_bt_start(const DesktopBtDriverOps *ops, DesktopGuestNet *net);
void desktop_bt_stop(void);
DesktopBtManager *desktop_bt_manager(void);

/* The driver for this platform, and the reason when there is none. */
const DesktopBtDriverOps *desktop_bt_platform_ops(void);
int desktop_bt_available(void);
const char *desktop_bt_unavailable_reason(void);

/* The drivers themselves. */
const DesktopBtDriverOps *desktop_bt_none_ops(void);

#endif
