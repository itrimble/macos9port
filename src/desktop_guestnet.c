/* The desktop's guest network service: one broker, one MacTCP layer and a
 * built-in policy, so no policy file has to be present for the guest to
 * reach a peer over a personal area network. */
#include "desktop_guestnet.h"

#include <stdlib.h>
#include <string.h>

/* The built-in policy: guest programs may use plaintext traffic, local
 * destinations are permitted, and every protocol to every host is allowed.
 * The policy file and the desktop policy window belong to the runtime hook
 * work and are out of scope here. */
static const char *k_builtin_policy =
    "legacy on\n"
    "local on\n"
    "allow both any * *\n";

struct DesktopGuestNet {
    GnPolicy policy;
    GnBroker *broker;
    GnResolver *resolver;
    GnMacTcp *mactcp;
};

DesktopGuestNet *desktop_guestnet_new(void) {
    DesktopGuestNet *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    gn_policy_init(&g->policy);
    if (gn_policy_parse(&g->policy, k_builtin_policy) != 0) {
        free(g);
        return NULL;
    }
    g->broker = gn_broker_new(&g->policy, "OS999", NULL, NULL, NULL);
    g->resolver = gn_resolver_new();
    if (!g->broker || !g->resolver) {
        desktop_guestnet_free(g);
        return NULL;
    }
    g->mactcp = gn_mactcp_new(g->broker, g->resolver, gn_broker_local_ip(g->broker));
    if (!g->mactcp) {
        desktop_guestnet_free(g);
        return NULL;
    }
    gn_mactcp_set_addr(g->mactcp, 0, 0);
    return g;
}

void desktop_guestnet_free(DesktopGuestNet *g) {
    if (!g) return;
    gn_mactcp_free(g->mactcp);
    gn_resolver_free(g->resolver);
    gn_broker_free(g->broker);
    free(g);
}

int desktop_guestnet_set_pan(DesktopGuestNet *g, GnPan *pan) {
    if (!g) return GN_EINVAL;
    int rc = gn_broker_set_pan(g->broker, pan);
    if (rc != GN_OK) return rc;
    /* The guest keeps the address it has on the link. */
    gn_mactcp_set_addr(g->mactcp, gn_broker_local_ip(g->broker), gn_broker_netmask(g->broker));
    return GN_OK;
}

uint32_t desktop_guestnet_local_ip(DesktopGuestNet *g) {
    return g ? gn_broker_local_ip(g->broker) : 0;
}

uint32_t desktop_guestnet_netmask(DesktopGuestNet *g) {
    return g ? gn_broker_netmask(g->broker) : 0;
}

GnBroker *desktop_guestnet_broker(DesktopGuestNet *g) { return g ? g->broker : NULL; }
GnMacTcp *desktop_guestnet_mactcp(DesktopGuestNet *g) { return g ? g->mactcp : NULL; }

void desktop_guestnet_pump(DesktopGuestNet *g) {
    if (!g) return;
    gn_pump(g->broker, 0);
}

/* The desktop keeps one service for its whole life. */
static DesktopGuestNet *g_service;

DesktopGuestNet *desktop_guestnet_service(void) {
    if (!g_service) g_service = desktop_guestnet_new();
    return g_service;
}

void desktop_guestnet_service_stop(void) {
    desktop_guestnet_free(g_service);
    g_service = NULL;
}
