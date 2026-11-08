/* Only archive installer fixture tests link this synthetic transport. */
#include "modern_network.h"
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
int net_init(void) { return 0; }
int net_https_url(const char *url) { return url != 0; }
NetResult net_download(const char *url, const char *dest, const NetPolicy *policy) {
    (void)policy;
    char *args[] = {"curl", "-o", (char *)dest, (char *)url, 0};
    pid_t pid;
    int status = 0;
    if (posix_spawnp(&pid, args[0], 0, 0, args, environ) || waitpid(pid, &status, 0) < 0 ||
        !WIFEXITED(status) || WEXITSTATUS(status)) return (NetResult){.status = NET_TRANSPORT};
    return (NetResult){.status = NET_OK};
}
