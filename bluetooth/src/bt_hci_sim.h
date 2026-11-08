/* Controller simulator: public interface.
 *
 * Only the tests use this; it is not part of the shipped stack. */
#ifndef BT_HCI_SIM_H
#define BT_HCI_SIM_H

#include "os999_bluetooth.h"

typedef struct BtSim BtSim;

BtSim *bt_sim_new(void);
void bt_sim_free(BtSim *s);
const BtHciTransport *bt_sim_transport(BtSim *s);
/* Hand the simulator the peer stack so the ACL link reaches it. */
void bt_sim_set_peer_l2cap(BtSim *s, BtL2cap *peer);
/* Register a device that inquiry reports and connections can reach. */
int bt_sim_add_device(BtSim *s, const BtAddress *addr, const char *name, uint32_t cod,
                      int needs_pin);
const BtAddress *bt_sim_local_addr(BtSim *s);
int bt_sim_link_count(BtSim *s);
/* Peer side ACL sender, to hand to bt_l2cap_set_acl_sender. */
long bt_sim_send_acl(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len);

#endif
