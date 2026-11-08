#ifndef OS999_TAILNET_H
#define OS999_TAILNET_H
#include <stddef.h>
enum { TAILNET_MAX_PEERS = 256, TAILNET_JSON_MAX = 1048576 };
typedef struct {
    char name[128], address[64], os[32];
    int online;
} TailnetPeer;
typedef struct {
    TailnetPeer peers[TAILNET_MAX_PEERS];
    int count;
    char state[64], self[128];
} Tailnet;
int tailnet_parse(const char *json, size_t length, Tailnet *net);
int tailnet_prefs(const char *json, size_t length, int *dns, int *routes, int *shields);
int tailnet_valid_user(const char *user);
const char *tailnet_cli(void);
int tailnet_capture(char *const argv[], char *out, size_t size);
int tailnet_run(char *const argv[]);
#endif
