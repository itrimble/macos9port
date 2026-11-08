#include "sound_preferences.h"
#include "ospi.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int sound_preference_enabled(const char *path) {
    if (!path || !*path) return 0;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    char data[3] = {0};
    ssize_t n = -1;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size <= 2)
        n = read(fd, data, sizeof data);
    close(fd);
    return (n == 1 || (n == 2 && data[1] == '\n')) && data[0] == '1';
}

int sound_preference_for_volume(const char *root) {
    char path[OSPI_PATH + 32];
    if (!root || !*root) return 0;
    int n = snprintf(path, sizeof path, "%s/.os999-sound", root);
    return n > 0 && (size_t)n < sizeof path && sound_preference_enabled(path);
}
