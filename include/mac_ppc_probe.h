#ifndef MAC_PPC_PROBE_H
#define MAC_PPC_PROBE_H

#include "mac_cfm.h"
#include "mac_resource.h"

#include <stdio.h>

/* Finds and reports the PowerPC fragment of an application: the 'cfrg' ID 0
 * resource names a PEF container in the data fork. Nothing here launches an
 * application; it reports honestly what the PowerPC runtime can and cannot do. */

/* 1: the fork's 'cfrg' 0 names a PowerPC fragment (application member preferred), written to `out`; 0: none. */
int mac_ppc_fork_member(const MacResourceFork *fork, MacCfrgMember *out);
/* Derives the data fork path from a `Name.rsrc` sidecar or a `Name/..namedfork/rsrc` path. 0 on success. */
int mac_ppc_data_fork_path(const char *fork_path, char *out, size_t out_size);
/* Reads the member's PEF bytes from the data fork. Caller frees. NULL with a reason in `err` on failure. */
uint8_t *mac_ppc_read_fragment(const char *fork_path, const MacCfrgMember *member, size_t *size, char *err,
                               size_t err_size);
/* The import table the probe binds against; the default is mac_ppc_host_imports. ospi-runtime passes the complete
 * table (mac_ppc_imports) so that --inspect reports what a launch would bind. */
void mac_ppc_probe_imports(const MacCfmHostImport *table, size_t count);
/* Trial-loads the fragment in scratch guest RAM against the import table and writes one line that says how many
 * imports have no host implementation and names the first. When `out` is not NULL the container, its sections,
 * libraries and imports are listed there too ("binding=host", "binding=null" for an absent weak import,
 * "binding=missing"). Nothing is executed. 0: parsed; -1: not a loadable fragment. */
int mac_ppc_probe(const uint8_t *pef, size_t size, FILE *out, char *message, size_t message_size);
/* --inspect: prints the cfrg member and the probe report. 1: a PowerPC fragment was reported; 0: none. */
int mac_ppc_inspect(const MacResourceFork *fork, const char *fork_path, FILE *out);
/* 1 when the application is PowerPC-only (cfrg member and no CODE 0) with the probe's summary in `message`; 0 when
 * the 68K path should proceed. The launch itself is mac_ppc_runtime_open (mac_ppc.h). */
int mac_ppc_launch_diagnostic(const MacResourceFork *fork, const char *fork_path, char *message, size_t message_size);

#endif
