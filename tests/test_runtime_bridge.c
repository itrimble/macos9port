#include "runtime_bridge.h"

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Test the versioned shared-memory prefix, without a production lock API. */
typedef struct {
    uint32_t magic, version, close_requested;
    pthread_mutex_t lock;
} BridgePrefix;

static double monotonic_seconds(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return now.tv_sec + now.tv_nsec / 1000000000.0;
}

static void blocked_peer(int die_holding_lock) {
    char name[64];
    RuntimeBridge *host = runtime_bridge_create(name, sizeof name);
    assert(host);
    int ready[2], release[2];
    assert(pipe(ready) == 0 && pipe(release) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(ready[0]); close(release[1]);
        uint32_t version;
        memcpy(&version, (char *)host + offsetof(BridgePrefix, version), sizeof version);
        pthread_mutex_t *lock = (pthread_mutex_t *)((char *)host + offsetof(BridgePrefix, lock));
        if (version != 2 || pthread_mutex_lock(lock)) _exit(2);
        if (write(ready[1], "x", 1) != 1) _exit(3);
        if (die_holding_lock) _exit(0);
        char byte;
        if (read(release[0], &byte, 1) != 1) _exit(4);
        pthread_mutex_unlock(lock);
        _exit(0);
    }
    close(ready[1]); close(release[0]);
    char byte;
    assert(read(ready[0], &byte, 1) == 1);
    int status;
    if (die_holding_lock)
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    alarm(5); /* Regression fails instead of hanging the test suite. */
    double start = monotonic_seconds();
    uint32_t pixel = 0, state = 99;
    int w = 0, h = 0;
    uint64_t sequence = 0;
    RuntimeBridgeInput input = {RUNTIME_BRIDGE_KEY_UP, 0, 0, 0, 0};
    assert(runtime_bridge_read(host, &pixel, 1, &w, &h, &sequence) == 0);
    assert(runtime_bridge_push(host, input) == 0);
    assert(runtime_bridge_shortcut(host, 'q') == 0);
    assert(runtime_bridge_pop(host, &input) == 0);
    assert(runtime_bridge_publish(host, &pixel, 1, 1) == -1);
    assert(runtime_bridge_get_state(host, &state, NULL, 0) == -1 && state == 99);
    if (die_holding_lock) {
        runtime_bridge_set_state(host, RUNTIME_BRIDGE_CLOSED, "");
        assert(runtime_bridge_get_state(host, &state, NULL, 0) == 0);
        assert(state == RUNTIME_BRIDGE_CLOSED);
    } else {
        assert(write(release[1], "x", 1) == 1);
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        assert(runtime_bridge_publish(host, &pixel, 1, 1) == 0);
        assert(runtime_bridge_read(host, &pixel, 1, &w, &h, &sequence) == 1);
    }
    runtime_bridge_close(host); /* No mutex destroy or lock during detach. */
    runtime_bridge_unlink(name);
    assert(monotonic_seconds() - start < 1.0);
    alarm(0);
    close(ready[0]); close(release[1]);
}

int main(void) {
    char name[64];
    RuntimeBridge *host = runtime_bridge_create(name, sizeof name);
    assert(host);
    RuntimeBridge *guest = runtime_bridge_open(name);
    assert(guest);
    uint32_t state = RUNTIME_BRIDGE_STARTING;
    char message[64];
    assert(runtime_bridge_get_state(host, &state, message, sizeof message) == 0);
    assert(state == RUNTIME_BRIDGE_STARTING && message[0] == 0);

    uint32_t source[12], dest[12] = {0};
    for (unsigned i = 0; i < 12; i++) source[i] = 0xff000000u | i * 0x010101u;
    assert(runtime_bridge_publish(guest, source, 4, 3) == 0);
    int width = 0, height = 0;
    uint64_t sequence = 0;
    assert(runtime_bridge_read(host, dest, 12, &width, &height, &sequence) == 1);
    assert(width == 4 && height == 3 && sequence == 1);
    assert(memcmp(source, dest, sizeof source) == 0);
    assert(runtime_bridge_read(host, dest, 12, &width, &height, &sequence) == 0);
    assert(runtime_bridge_publish(guest, source, 1281, 3) == -1);

    RuntimeBridgeInput sent = {RUNTIME_BRIDGE_BUTTON_DOWN, 3, 2, 0, 0x80};
    RuntimeBridgeInput received = {0};
    assert(runtime_bridge_push(host, sent) == 1);
    assert(runtime_bridge_pop(guest, &received) == 1);
    assert(memcmp(&sent, &received, sizeof sent) == 0);
    assert(runtime_bridge_pop(guest, &received) == 0);

    assert(runtime_bridge_shortcut(NULL, 'q') == -1);
    assert(runtime_bridge_shortcut(host, 'x') == -1);
    for (int i = 0; i < RUNTIME_BRIDGE_INPUTS - 1; i++) assert(runtime_bridge_push(host, sent) == 1);
    assert(runtime_bridge_shortcut(host, 'q') == 0); /* no partial key down */
    for (int i = 0; i < RUNTIME_BRIDGE_INPUTS - 1; i++) {
        assert(runtime_bridge_pop(guest, &received) == 1);
        assert(received.kind == RUNTIME_BRIDGE_BUTTON_DOWN);
    }
    assert(runtime_bridge_pop(guest, &received) == 0);
    for (int i = 0; i < 2; i++) {
        uint32_t key = i ? 'w' : 'q';
        assert(runtime_bridge_shortcut(host, key) == 1);
        assert(runtime_bridge_pop(guest, &received) == 1);
        assert(received.kind == RUNTIME_BRIDGE_KEY_DOWN && received.message == key &&
               received.modifiers == RUNTIME_BRIDGE_HOST_COMMAND);
        assert(runtime_bridge_pop(guest, &received) == 1);
        assert(received.kind == RUNTIME_BRIDGE_KEY_UP && received.message == key && !received.modifiers);
        assert(runtime_bridge_pop(guest, &received) == 0);
    }
    runtime_bridge_set_state(guest, RUNTIME_BRIDGE_FAILED, "controlled test failure");
    assert(runtime_bridge_get_state(host, &state, message, sizeof message) == 0);
    assert(state == RUNTIME_BRIDGE_FAILED && strcmp(message, "controlled test failure") == 0);

    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        RuntimeBridge *other = runtime_bridge_open(name);
        if (!other) _exit(2);
        uint32_t child_state;
        int ok = runtime_bridge_get_state(other, &child_state, NULL, 0) == 0 &&
                 child_state == RUNTIME_BRIDGE_FAILED;
        runtime_bridge_close(other);
        _exit(ok ? 0 : 3);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    runtime_bridge_close(guest);
    runtime_bridge_close(host);
    runtime_bridge_unlink(name);
    blocked_peer(0);
    blocked_peer(1);
    puts("runtime bridge checks passed");
    return 0;
}
