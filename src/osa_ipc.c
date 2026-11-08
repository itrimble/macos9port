#include "osa_session.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int listener = -1;
static char owned_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static dev_t owned_device;
static ino_t owned_inode;

int osa_ipc_open(const char *path) {
    if (listener >= 0 || !path || path[0] != '/' || strlen(path) >= sizeof owned_path) return -1;
    struct stat st;
    char parent[sizeof owned_path]; snprintf(parent, sizeof parent, "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) return -1;
    *slash = 0;
    if (lstat(parent, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) return -1;
    if (!lstat(path, &st) || errno != ENOENT) return -1;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    mode_t mask = umask(0077);
    int result = bind(fd, (struct sockaddr *)&addr, sizeof addr);
    umask(mask);
    if (result) { close(fd); return -1; }
    if (lstat(path, &st)) { close(fd); return -1; }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        struct stat current;
        close(fd);
        if (!lstat(path, &current) && current.st_dev == st.st_dev && current.st_ino == st.st_ino) unlink(path);
        return -1;
    }
    listener = fd; owned_device = st.st_dev; owned_inode = st.st_ino;
    snprintf(owned_path, sizeof owned_path, "%s", path);
    return 0;
}

void osa_ipc_poll(Session *session) {
    if (listener < 0 || !session) return;
    for (int i = 0; i < 8; i++) {
        char request[4096], reply[2048] = "{\"ok\":false,\"error\":\"bridge failure\"}";
        struct sockaddr_un peer = {0};
        struct iovec io = {request, sizeof request - 1};
        struct msghdr msg = {0};
        msg.msg_name = &peer; msg.msg_namelen = sizeof peer;
        msg.msg_iov = &io; msg.msg_iovlen = 1;
        ssize_t len = recvmsg(listener, &msg, 0);
        if (len < 0) break;
        if ((msg.msg_flags & MSG_TRUNC) || memchr(request, 0, (size_t)len))
            snprintf(reply, sizeof reply, "{\"ok\":false,\"error\":\"invalid datagram\"}");
        else {
            request[len] = 0;
            (void)osa_session_json(session, request, reply, sizeof reply);
        }
        if (msg.msg_namelen > offsetof(struct sockaddr_un, sun_path) && peer.sun_path[0])
            (void)sendto(listener, reply, strlen(reply), 0, (struct sockaddr *)&peer, msg.msg_namelen);
        if (session->quit) break;
    }
}

void osa_ipc_close(void) {
    if (listener < 0) return;
    close(listener); listener = -1;
    struct stat st;
    if (!lstat(owned_path, &st) && st.st_dev == owned_device && st.st_ino == owned_inode)
        unlink(owned_path);
    owned_path[0] = 0;
}

void osa_ipc_start_from_env(void) {
    (void)osa_component_find(OSA_TYPE_APPLESCRIPT);
    const char *path = getenv("OS999_AUTOMATION_SOCKET");
    if (path && osa_ipc_open(path))
        fprintf(stderr, "OS999 automation: socket requires an unused path in a private owned directory\n");
}
