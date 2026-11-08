#ifndef OS999_OSA_SESSION_H
#define OS999_OSA_SESSION_H
#include "osa.h"
/* Local desktop bridge, not the classic guest Apple Event/OSA trap ABI.
 * Supports name/version/theme queries, theme mutation, OS999-only quit and
 * Finder restart. No shell, host shutdown, or external application dispatch. */
int osa_session_json(Session *session, const char *request, char *reply, size_t n);
/* Explicit opt-in socket; existing paths are never replaced. Place the socket
 * under an existing private 0700 owned directory. Send one JSON object
 * per Unix datagram. Clients bind their own private reply socket. */
int osa_ipc_open(const char *path);
void osa_ipc_start_from_env(void);
void osa_ipc_poll(Session *session);
void osa_ipc_close(void);
#endif
