#include "runtime_bridge.h"

#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BRIDGE_MAGIC UINT32_C(0x4f535042)
#define BRIDGE_VERSION 2u
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), "bridge close flag must be lock-free");

struct RuntimeBridge {
    uint32_t magic, version;
    uint32_t close_requested; /* lock-free teardown signal, outside the mutex */
    pthread_mutex_t lock;
    uint32_t state, width, height, input_read, input_write;
    uint64_t frame_sequence;
    char message[256];
    RuntimeBridgeInput inputs[RUNTIME_BRIDGE_INPUTS];
    uint32_t pixels[RUNTIME_BRIDGE_MAX_WIDTH * RUNTIME_BRIDGE_MAX_HEIGHT];
};

/* A peer may exit while owning a process-shared mutex. macOS does not
 * provide robust mutex recovery, so never wait indefinitely or steal its lock.
 * 10ms permits the bounded frame copy while keeping a dead peer responsive. */
static int bridge_lock(RuntimeBridge *bridge) {
    struct timespec start, now, pause = {0, 1000000};
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return -1;
    for (;;) {
        int rc = pthread_mutex_trylock(&bridge->lock);
        if (!rc) return 0;
        if (rc != EBUSY || clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
        int64_t elapsed = (int64_t)(now.tv_sec - start.tv_sec) * 1000000000 +
                          now.tv_nsec - start.tv_nsec;
        if (elapsed >= 10000000) return -1;
        nanosleep(&pause, NULL);
    }
}

static RuntimeBridge *map_fd(int fd) {
    struct stat st;
    if (fstat(fd, &st) || st.st_size < (off_t)sizeof(RuntimeBridge)) return NULL;
    RuntimeBridge *bridge = mmap(NULL, sizeof *bridge, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bridge == MAP_FAILED) return NULL;
    if (bridge->magic != BRIDGE_MAGIC || bridge->version != BRIDGE_VERSION) {
        munmap(bridge, sizeof *bridge);
        return NULL;
    }
    return bridge;
}

RuntimeBridge *runtime_bridge_create(char *name, size_t name_size) {
    if (!name || name_size < 16) return NULL;
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        struct timespec now = {0};
        clock_gettime(CLOCK_REALTIME, &now);
        unsigned long nonce = (unsigned long)now.tv_nsec ^ (unsigned long)getpid() ^ attempt;
        int n = snprintf(name, name_size, "/ospi-%lu-%lx", (unsigned long)getpid(), nonce);
        if (n < 0 || (size_t)n >= name_size) return NULL;
        int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) continue;
        if (ftruncate(fd, (off_t)sizeof(RuntimeBridge))) {
            close(fd); shm_unlink(name); return NULL;
        }
        RuntimeBridge *bridge = mmap(NULL, sizeof *bridge, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, 0);
        close(fd);
        if (bridge == MAP_FAILED) { shm_unlink(name); return NULL; }
        memset(bridge, 0, sizeof *bridge);
        pthread_mutexattr_t attr;
        int attr_ready = pthread_mutexattr_init(&attr) == 0;
        int ok = attr_ready;
        if (ok) ok = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) == 0;
        if (ok) ok = pthread_mutex_init(&bridge->lock, &attr) == 0;
        if (!ok) {
            if (attr_ready) pthread_mutexattr_destroy(&attr);
            munmap(bridge, sizeof *bridge); shm_unlink(name); return NULL;
        }
        pthread_mutexattr_destroy(&attr);
        bridge->version = BRIDGE_VERSION;
        bridge->state = RUNTIME_BRIDGE_STARTING;
        __sync_synchronize();
        bridge->magic = BRIDGE_MAGIC;
        return bridge;
    }
    return NULL;
}

RuntimeBridge *runtime_bridge_open(const char *name) {
    if (!name || name[0] != '/' || strchr(name + 1, '/')) return NULL;
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) return NULL;
    RuntimeBridge *bridge = map_fd(fd);
    close(fd);
    return bridge;
}

void runtime_bridge_close(RuntimeBridge *bridge) {
    if (bridge) munmap(bridge, sizeof *bridge);
}

void runtime_bridge_unlink(const char *name) {
    if (name && name[0] == '/' && !strchr(name + 1, '/')) shm_unlink(name);
}

int runtime_bridge_publish(RuntimeBridge *bridge, const uint32_t *pixels,
                           int width, int height) {
    if (!bridge || !pixels || width < 1 || height < 1 ||
        width > RUNTIME_BRIDGE_MAX_WIDTH || height > RUNTIME_BRIDGE_MAX_HEIGHT ||
        (size_t)width > SIZE_MAX / (size_t)height) return -1;
    if (bridge_lock(bridge)) return -1;
    memcpy(bridge->pixels, pixels, (size_t)width * (size_t)height * sizeof *pixels);
    bridge->width = (uint32_t)width;
    bridge->height = (uint32_t)height;
    bridge->frame_sequence++;
    pthread_mutex_unlock(&bridge->lock);
    return 0;
}

int runtime_bridge_read(RuntimeBridge *bridge, uint32_t *pixels, size_t capacity,
                        int *width, int *height, uint64_t *sequence) {
    if (!bridge || !pixels || !width || !height || !sequence) return -1;
    if (bridge_lock(bridge)) return 0;
    size_t count = (size_t)bridge->width * bridge->height;
    if (!bridge->frame_sequence || count > capacity) {
        pthread_mutex_unlock(&bridge->lock);
        return 0;
    }
    if (bridge->frame_sequence == *sequence) {
        pthread_mutex_unlock(&bridge->lock);
        return 0;
    }
    memcpy(pixels, bridge->pixels, count * sizeof *pixels);
    *width = (int)bridge->width;
    *height = (int)bridge->height;
    *sequence = bridge->frame_sequence;
    pthread_mutex_unlock(&bridge->lock);
    return 1;
}

int runtime_bridge_push(RuntimeBridge *bridge, RuntimeBridgeInput input) {
    if (!bridge) return -1;
    if (bridge_lock(bridge)) return 0;
    uint32_t used = bridge->input_write - bridge->input_read;
    if (used >= RUNTIME_BRIDGE_INPUTS) {
        pthread_mutex_unlock(&bridge->lock);
        return 0;
    }
    bridge->inputs[bridge->input_write % RUNTIME_BRIDGE_INPUTS] = input;
    bridge->input_write++;
    pthread_mutex_unlock(&bridge->lock);
    return 1;
}

int runtime_bridge_shortcut(RuntimeBridge *bridge, uint32_t key) {
    if (!bridge || (key != 'q' && key != 'w')) return -1;
    if (bridge_lock(bridge)) return 0;
    uint32_t used = bridge->input_write - bridge->input_read;
    if (used > RUNTIME_BRIDGE_INPUTS - 2) {
        pthread_mutex_unlock(&bridge->lock); return 0;
    }
    RuntimeBridgeInput pair[2] = {
        {RUNTIME_BRIDGE_KEY_DOWN, 0, 0, key, RUNTIME_BRIDGE_HOST_COMMAND},
        {RUNTIME_BRIDGE_KEY_UP, 0, 0, key, 0}
    };
    for (unsigned i = 0; i < 2; i++)
        bridge->inputs[bridge->input_write++ % RUNTIME_BRIDGE_INPUTS] = pair[i];
    pthread_mutex_unlock(&bridge->lock);
    return 1;
}

int runtime_bridge_pop(RuntimeBridge *bridge, RuntimeBridgeInput *input) {
    if (!bridge || !input) return -1;
    if (bridge_lock(bridge)) return 0;
    if (bridge->input_read == bridge->input_write) {
        pthread_mutex_unlock(&bridge->lock);
        return 0;
    }
    *input = bridge->inputs[bridge->input_read % RUNTIME_BRIDGE_INPUTS];
    bridge->input_read++;
    pthread_mutex_unlock(&bridge->lock);
    return 1;
}

void runtime_bridge_set_state(RuntimeBridge *bridge, uint32_t state,
                              const char *message) {
    if (!bridge) return;
    if (state == RUNTIME_BRIDGE_CLOSED)
        __atomic_store_n(&bridge->close_requested, 1u, __ATOMIC_RELEASE);
    if (bridge_lock(bridge)) return;
    bridge->state = state;
    snprintf(bridge->message, sizeof bridge->message, "%s", message ? message : "");
    pthread_mutex_unlock(&bridge->lock);
}

int runtime_bridge_get_state(RuntimeBridge *bridge, uint32_t *state,
                             char *message, size_t message_size) {
    if (!bridge || !state) return -1;
    if (__atomic_load_n(&bridge->close_requested, __ATOMIC_ACQUIRE)) {
        *state = RUNTIME_BRIDGE_CLOSED;
        if (message && message_size) message[0] = 0;
        return 0;
    }
    if (bridge_lock(bridge)) return -1;
    *state = bridge->state;
    if (message && message_size) snprintf(message, message_size, "%s", bridge->message);
    pthread_mutex_unlock(&bridge->lock);
    return 0;
}
