/* The desktop's guest network service.
 *
 * The desktop owns one broker and one MacTCP layer: the service the runtime
 * hook will hand to programs. A Bluetooth personal area network attaches to
 * that broker, so a guest TCP connection to the peer travels over the
 * Bluetooth link and the guest keeps its own address there. */
#ifndef OS999_DESKTOP_GUESTNET_H
#define OS999_DESKTOP_GUESTNET_H

#include "os999_guestnet.h"

typedef struct DesktopGuestNet DesktopGuestNet;

DesktopGuestNet *desktop_guestnet_new(void);
void desktop_guestnet_free(DesktopGuestNet *g);

/* Attach a personal area network, or detach it with NULL. */
int desktop_guestnet_set_pan(DesktopGuestNet *g, GnPan *pan);
/* The address the guest has on the link; 0 without one. */
uint32_t desktop_guestnet_local_ip(DesktopGuestNet *g);
uint32_t desktop_guestnet_netmask(DesktopGuestNet *g);

GnBroker *desktop_guestnet_broker(DesktopGuestNet *g);
GnMacTcp *desktop_guestnet_mactcp(DesktopGuestNet *g);
/* Move bytes on the guest links. */
void desktop_guestnet_pump(DesktopGuestNet *g);

/* The desktop's one service, created on first use. */
DesktopGuestNet *desktop_guestnet_service(void);
void desktop_guestnet_service_stop(void);

#endif
