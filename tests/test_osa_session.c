#include "osa_session.h"
#include "desktop_theme.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int command;
void session_command(Session *s, int cmd) { (void)s; command = cmd; }

int main(void) {
    Session *session = calloc(1, sizeof *session);
    assert(session && desktop_theme_init(NULL) == 0); /* memory-only preferences */
    char reply[2048];
    const char *get = "{\"class\":\"core\",\"event\":\"getd\",\"property\":\"theme\"}";
    assert(osa_session_json(session, get, reply, sizeof reply) == 0);
    assert(strstr(reply, "Platinum"));
    assert(osa_session_json(session, "{\"class\":\"core\",\"event\":\"setd\",\"property\":\"theme\",\"value\":\"Graphite\"}", reply, sizeof reply) == 0);
    assert(!strcmp(desktop_theme_name(desktop_theme_current()), "Graphite"));
    assert(osa_session_json(session, "{\"class\":\"misc\",\"event\":\"shut\",\"restart\":true}", reply, sizeof reply) == 0);
    assert(command == CMD_RESTART_DO);
    command = 0;
    assert(osa_session_json(session, "{\"class\":\"misc\",\"event\":\"shut\",\"target\":\"host\"}", reply, sizeof reply) != 0);
    assert(command == 0);
    char root[] = "/tmp/os999-osa-XXXXXX";
    assert(mkdtemp(root));
    char path[100], client[100];
    snprintf(path, sizeof path, "%s/server", root);
    snprintf(client, sizeof client, "%s/client", root);
    assert(chmod(root, 0755) == 0 && osa_ipc_open(path) == -1);
    assert(chmod(root, 0700) == 0 && osa_ipc_open(path) == 0);
    assert(osa_ipc_open(path) == -1);
    struct stat st; assert(!stat(path, &st) && !(st.st_mode & 0077));
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0); assert(fd >= 0);
    struct timeval timeout = {1, 0};
    assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout));
    struct sockaddr_un from = {0}, to = {0};
    from.sun_family = to.sun_family = AF_UNIX;
    snprintf(from.sun_path, sizeof from.sun_path, "%s", client);
    snprintf(to.sun_path, sizeof to.sun_path, "%s", path);
    assert(bind(fd, (struct sockaddr *)&from, sizeof from) == 0);
    assert(sendto(fd, get, strlen(get), 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)strlen(get));
    osa_ipc_poll(session);
    ssize_t n = recv(fd, reply, sizeof reply - 1, 0); assert(n > 0); reply[n] = 0;
    assert(strstr(reply, "Graphite"));
    char bad[512]; memset(bad, 'x', sizeof bad); bad[4] = 0;
    assert(sendto(fd, bad, sizeof bad, 0, (struct sockaddr *)&to, sizeof to) == sizeof bad);
    osa_ipc_poll(session);
    n = recv(fd, reply, sizeof reply - 1, 0); assert(n > 0); reply[n] = 0;
    assert(strstr(reply, "invalid datagram"));
    assert(unlink(path) == 0); /* Simulate a replacement before cleanup. */
    FILE *f = fopen(path, "w"); assert(f); fputs("preserve", f); fclose(f);
    osa_ipc_close(); assert(access(path, F_OK) == 0);
    assert(osa_ipc_open(path) == -1); osa_ipc_close(); assert(access(path, F_OK) == 0);
    close(fd); unlink(client); unlink(path); rmdir(root);
    desktop_theme_shutdown(); free(session);
    puts("OSA desktop theme backend and private datagram IPC passed; no external script actions");
    return 0;
}
