// OS999 Browser embed mode: opt-in video decoder diagnostics (OS999_DECODE_DIAG=1).
// The desktop spawns the helper with its own environment, so setting the variable
// when launching OS999 reaches the helper. Off (and free) when it is not set.
//
// A while after start, two extra windowless browsers open chrome://media-internals and
// chrome://gpu. They never produce frames for the desktop and have no tab id; a script
// reads their text once a second and decode_diag_host logs it under
// <profile>/diagnostics/decode-diag.log (the OS999 volume's .os999/Browser folder).
#ifndef OS999_EMBED_DIAG_H_
#define OS999_EMBED_DIAG_H_

#include <functional>

namespace embed_diag {
#if defined(__APPLE__)
// UI thread. No-op unless OS999_DECODE_DIAG=1. `on_closed` runs after a diagnostics browser closes.
void Start(std::function<void()> on_closed);
// UI thread, once a second.
void Tick();
// UI thread, when the session ends: writes the verdict now (before Chromium's
// teardown watchdog can end the process) and closes the diagnostics browsers.
void Stop();
// True while a diagnostics browser is still open (shutdown must wait for it).
bool Busy();
#else
inline void Start(std::function<void()>) {}
inline void Tick() {}
inline void Stop() {}
inline bool Busy() { return false; }
#endif
}  // namespace embed_diag

#endif
