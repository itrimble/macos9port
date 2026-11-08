/* Original OS999 interoperability protocol, 2026 Ian. MIT licensed.
 * Permission is granted to use, copy, modify and distribute this file with
 * this notice. Provided AS IS, without warranty of any kind. */
#ifndef OS999_QUAKE_BRIDGE_H
#define OS999_QUAKE_BRIDGE_H
#include <stdint.h>
#include <stdatomic.h>

#define OS999Q_MAGIC 0x51393939u
#define OS999Q_VERSION 2u
#define OS999Q_WIDTH 640u
#define OS999Q_HEIGHT 480u
#define OS999Q_EVENTS 256u
/* OS999Q_MOUSE carries relative motion (a, b) for looking around. OS999Q_MOUSE_ABS carries the pointer's
 * position inside the frame (a = x, b = y) so a menu cursor can sit exactly under it. Older engines
 * ignore kinds they do not know. */
enum { OS999Q_KEY = 1, OS999Q_MOUSE, OS999Q_BUTTON, OS999Q_WHEEL, OS999Q_KEY_SYMBOL, OS999Q_MOUSE_ABS };
typedef struct { uint32_t kind; int32_t a, b, c; } Os999QuakeEvent;
typedef struct {
    uint32_t magic, version, bytes, width, height;
    atomic_uint closing, ready, focus, capture, frame_lock, frames;
    atomic_uint input_read, input_write, reset_input;
    Os999QuakeEvent events[OS999Q_EVENTS];
    /* BGRA bytes, OpenGL bottom row first. Protected by frame_lock. */
    uint32_t pixels[OS999Q_WIDTH * OS999Q_HEIGHT];
} Os999QuakeBridge;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Bridge needs lock-free shared atomics");

/* Single producer and consumer; never wait for a stalled peer. */
static inline int os999q_push(Os999QuakeBridge *b, Os999QuakeEvent event) {
    unsigned w = atomic_load_explicit(&b->input_write, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&b->input_read, memory_order_acquire);
    if (w - r >= OS999Q_EVENTS) {
        atomic_store(&b->reset_input, 1);
        return 0;
    }
    b->events[w % OS999Q_EVENTS] = event;
    atomic_store_explicit(&b->input_write, w + 1, memory_order_release);
    return 1;
}
#endif
