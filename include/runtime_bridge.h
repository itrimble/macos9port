#ifndef OSPI_RUNTIME_BRIDGE_H
#define OSPI_RUNTIME_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_BRIDGE_MAX_WIDTH = 1280,
    RUNTIME_BRIDGE_MAX_HEIGHT = 960,
    RUNTIME_BRIDGE_INPUTS = 64,
    RUNTIME_BRIDGE_HOST_COMMAND = 0x0400 /* SDL2 left-GUI modifier on the wire */
};

typedef struct RuntimeBridge RuntimeBridge;

typedef struct {
    uint32_t kind;
    int32_t x, y;
    uint32_t message, modifiers;
} RuntimeBridgeInput;

enum {
    RUNTIME_BRIDGE_MOTION = 1,
    RUNTIME_BRIDGE_BUTTON_DOWN,
    RUNTIME_BRIDGE_BUTTON_UP,
    RUNTIME_BRIDGE_KEY_DOWN,
    RUNTIME_BRIDGE_KEY_UP,
    RUNTIME_BRIDGE_FOCUS_LOST
};

enum {
    RUNTIME_BRIDGE_STARTING = 0,
    RUNTIME_BRIDGE_READY,
    RUNTIME_BRIDGE_CLOSED,
    RUNTIME_BRIDGE_FAILED
};

/* Shared-memory bridge between the OS999 Finder process and one guest runtime.
 * Its private name is created by OS999 and passed only to that child process. */
RuntimeBridge *runtime_bridge_create(char *name, size_t name_size);
RuntimeBridge *runtime_bridge_open(const char *name);
void runtime_bridge_close(RuntimeBridge *bridge);
void runtime_bridge_unlink(const char *name);
int runtime_bridge_publish(RuntimeBridge *bridge, const uint32_t *pixels,
                           int width, int height);
int runtime_bridge_read(RuntimeBridge *bridge, uint32_t *pixels, size_t capacity,
                        int *width, int *height, uint64_t *sequence);
int runtime_bridge_push(RuntimeBridge *bridge, RuntimeBridgeInput input);
int runtime_bridge_pop(RuntimeBridge *bridge, RuntimeBridgeInput *input);
/* Atomically enqueue both shortcut edges; 0 means no event was queued. */
int runtime_bridge_shortcut(RuntimeBridge *bridge, uint32_t key);
void runtime_bridge_set_state(RuntimeBridge *bridge, uint32_t state,
                              const char *message);
int runtime_bridge_get_state(RuntimeBridge *bridge, uint32_t *state,
                             char *message, size_t message_size);

#endif
