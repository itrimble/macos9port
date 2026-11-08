#ifndef OS999_APPLICATION_PROBE_H
#define OS999_APPLICATION_PROBE_H
#include "os999_storage.h"
typedef enum { MAC_APP_NONE, MAC_APP_68K, MAC_APP_PPC, MAC_APP_CFM } MacApplicationKind;
/* Structural type/resource signature validation, not publisher authentication.
 * ST_OK means a CODE 0 application for ospi-runtime; the runtime still checks
 * its supported jump-table layout. Unsupported architectures fail explicitly. */
int mac_application_probe(const StMacFile *file, MacApplicationKind *kind, StErr *err);
/* Exact archive path, or a sole application. Never choose a neighboring app. */
int mac_application_select(const StArchive *archive, const char *exact_path,
                            size_t *index, MacApplicationKind *kind, StErr *err);
#endif
