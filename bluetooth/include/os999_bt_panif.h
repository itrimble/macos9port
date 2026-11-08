/* Binding a Bluetooth personal area network to the guest IP stack.
 *
 * The Bluetooth stack carries Ethernet frames over BNEP; the guest network
 * stack in net/ owns IP, ARP and TCP. This header is the seam between the
 * two: it is the only place where the Bluetooth stack names a guest
 * networking type, and it does so through net/'s public header only. */
#ifndef OS999_BT_PANIF_H
#define OS999_BT_PANIF_H

#include "os999_bluetooth.h"
#include "os999_guestnet.h"

/* A Bluetooth personal area network presented to the guest IP stack as a
 * link that carries Ethernet frames. The guest keeps its own address on
 * the PAN, so its TCP runs over the Bluetooth link. */
typedef struct BtPanIf BtPanIf;

/* Bind an open PAN socket to the guest IP stack. mac may be NULL to use
 * the address the PAN socket already carries. */
BtPanIf *bt_panif_new(BtPan *pan, int id, uint32_t local_ip, uint32_t netmask, const uint8_t mac[6]);
void bt_panif_free(BtPanIf *p);
/* The guest IP stack behind the link, ready for gn_broker_set_pan. */
GnPan *bt_panif_pan(BtPanIf *p);
/* Name the peer so its frames can be addressed without waiting for ARP. */
int bt_panif_set_peer(BtPanIf *p, uint32_t peer_ip, const uint8_t peer_mac[6]);
/* Move the frames that arrived on the link into the guest IP stack.
 * Returns how many frames were handed over. */
int bt_panif_pump(BtPanIf *p);

#endif
