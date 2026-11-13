#ifndef OS999_COMPAT_KNOWN_H
#define OS999_COMPAT_KNOWN_H

#include <stddef.h>
#include "app_classify.h"

/* Built-in list of executables whose observed behaviour differs from what their headers say. Keyed on the classifier
 * identity (a hash of the executable's code), never on a name; the entries carry no application names. A user's saved
 * override beats this list, and this list beats the classifier. Each entry needs a headless run behind it. */
typedef struct { uint64_t identity; AppProfile profile; const char *reason; } CompatKnown;

/* NULL when the identity is not listed. The returned entry is static. */
const CompatKnown *compat_known_lookup(uint64_t identity);

/* Test seam: replace the table (NULL restores the built-in one). */
void compat_known_set_table(const CompatKnown *table, size_t count);

#endif
