#ifndef OS999_BROWSER_STAGE_H
#define OS999_BROWSER_STAGE_H
#include <stddef.h>

/* Chromium on macOS 27 crashes inside CefInitialize when its own app bundle sits
 * inside another .app (measured on a MacBook Neo, macOS 27.2: 0 of 30 standalone
 * launches died, nearly every nested launch did; the same bundle is fine on macOS
 * 26). The desktop therefore runs a clone of the helper from outside any .app. */

/* 1 when `exe` (.../X.app/Contents/MacOS/X) has another ".app" ancestor above its own bundle. */
int browser_stage_is_nested(const char *exe);

/* Resolve the helper to run. A helper that is not nested is returned unchanged.
 * A nested one is cloned (APFS clone, so no extra space) into
 * `support_dir`/OS999Browser.app and the clone's executable is returned; a stamp of
 * the source's size and times decides when to refresh it. support_dir NULL means
 * ~/Library/Application Support/OS999/Helpers. On any failure the original path is
 * returned and -1 is the result, so the caller can still try the nested helper. */
int browser_stage_helper(const char *exe, const char *support_dir, char *out, size_t n);
#endif
