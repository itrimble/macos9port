/* The desktop guest network service: the broker, the MacTCP layer and the
 * built-in policy, with a personal area network attached. */
#include "desktop_guestnet.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* A link that keeps every frame, so the test can look at what the guest
 * stack sends. */
#define FRAMES 16
static uint8_t frames[FRAMES][128];
static size_t frame_len[FRAMES];
static int nframes;

static int link_tx(void *ctx, const void *frame, size_t len) {
    (void)ctx;
    if (nframes >= FRAMES || len > sizeof frames[0]) return GN_EIO;
    memcpy(frames[nframes], frame, len);
    frame_len[nframes] = len;
    nframes++;
    return GN_OK;
}

int main(void) {
    DesktopGuestNet *g = desktop_guestnet_new();
    CHECK(g != NULL);
    if (!g) return 1;
    CHECK(desktop_guestnet_broker(g) != NULL);
    CHECK(desktop_guestnet_mactcp(g) != NULL);
    CHECK(desktop_guestnet_local_ip(g) == 0);
    CHECK(desktop_guestnet_netmask(g) == 0);

    /* A personal area network gives the guest its own address. */
    GnPanLink link = {.tx = link_tx, .ctx = NULL};
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t peer_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x07};
    GnPan *pan = gn_pan_new(&link, 0xC0A80A01u, 0xFFFFFF00u, mac);
    CHECK(pan != NULL);
    CHECK(gn_pan_set_peer(pan, 0xC0A80A02u, peer_mac) == GN_OK);
    CHECK(desktop_guestnet_set_pan(g, pan) == GN_OK);
    CHECK(desktop_guestnet_local_ip(g) == 0xC0A80A01u);
    CHECK(desktop_guestnet_netmask(g) == 0xFFFFFF00u);
    CHECK(gn_broker_local_ip(desktop_guestnet_broker(g)) == 0xC0A80A01u);

    /* The guest asks for its address and gets the one on the link. */
    uint8_t ram[1 << 16];
    GnMem mem = {ram, sizeof ram};
    memset(ram, 0, sizeof ram);
    uint32_t pb = 0x1000;
    memset(ram + pb, 0, 128);
    ram[pb + 27] = GN_IPCTL_GETADDR;
    gn_mactcp_control(desktop_guestnet_mactcp(g), &mem, pb, 0, 1000);
    CHECK((uint16_t)((ram[pb + 16] << 8) | ram[pb + 17]) == 0);
    CHECK(((uint32_t)ram[pb + 28] << 24 | (uint32_t)ram[pb + 29] << 16 |
           (uint32_t)ram[pb + 30] << 8 | ram[pb + 31]) == 0xC0A80A01u);
    CHECK(((uint32_t)ram[pb + 32] << 24 | (uint32_t)ram[pb + 33] << 16 |
           (uint32_t)ram[pb + 34] << 8 | ram[pb + 35]) == 0xFFFFFF00u);

    struct sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(0xC0A80A02u);
    peer.sin_port = htons(8080);
    int connection = -1;
    CHECK(gn_tcp_connect(desktop_guestnet_broker(g), (struct sockaddr *)&peer,
                        sizeof peer, "192.168.10.2", &connection) == GN_OK);
    /* Detaching the link leaves the guest without a link address. */
    CHECK(desktop_guestnet_set_pan(g, NULL) == GN_OK);
    CHECK(desktop_guestnet_local_ip(g) == 0);

    memset(ram + pb, 0, 128);
    ram[pb + 27] = GN_IPCTL_GETADDR;
    gn_mactcp_control(desktop_guestnet_mactcp(g), &mem, pb, 0, 1000);
    CHECK(ram[pb + 28] == 0 && ram[pb + 29] == 0 && ram[pb + 30] == 0 && ram[pb + 31] == 0);
    CHECK(ram[pb + 32] == 0 && ram[pb + 33] == 0 && ram[pb + 34] == 0 && ram[pb + 35] == 0);
    gn_pan_free(pan);
    GnInfo info;
    CHECK(gn_info(desktop_guestnet_broker(g), connection, &info) == GN_OK);
    CHECK(info.state == GN_S_FAILED && info.error == GN_ECLOSED);
    CHECK(gn_send(desktop_guestnet_broker(g), connection, "x", 1) == GN_ECLOSED);
    desktop_guestnet_pump(g);
    CHECK(gn_release(desktop_guestnet_broker(g), connection) == GN_OK);
    desktop_guestnet_free(g);
    printf("desktop guest network: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
