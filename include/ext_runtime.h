#ifndef OS999_EXT_RUNTIME_H
#define OS999_EXT_RUNTIME_H

#include "mac_runtime.h"

/* Load the volume's enabled extensions into a fresh guest runtime, before the
 * application's first instruction. The desktop hands over the volume through
 * OSPI_EXT_ROOT; OSPI_EXT_MODE=none skips loading for this boot (Shift held).
 * A boot-pending marker left by an earlier crash also skips loading, and the
 * desktop then opens the Extensions Manager. Returns the number of extensions
 * applied (0 when nothing applies), or -1 when the runtime is no longer usable. */
int ext_runtime_boot(MacRuntime *rt);

#endif
