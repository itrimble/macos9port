#include "ospi.h"

#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

uint64_t host_ram(void) {
#if defined(__APPLE__)
    uint64_t mem = 0;
    size_t len = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0 && mem > 0) {
        return mem;
    }
#endif
    long pages = sysconf(_SC_PHYS_PAGES);
    long psz = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && psz > 0) {
        return (uint64_t)pages * (uint64_t)psz;
    }
    return 0;
}

const char *host_arch(void) {
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x86_64";
#else
    return "unknown";
#endif
}
