#ifndef OS999_OSA_H
#define OS999_OSA_H

#include <stdint.h>
#include "osa_bridge.h"
#include "osa_events.h"

enum {
    OSA_MAX_COMPONENTS = 8,
    OSA_MAX_STATEMENTS = 16,
    OSA_TYPE_APPLESCRIPT = OSA_4CC('a', 's', 'c', 'r'),
};

/* A compiled script is the ordered event list its statements produced;
 * lines[i] is the source line statements[i] came from. */
typedef struct OsaScript {
    AppleEvent events[OSA_MAX_STATEMENTS];
    int lines[OSA_MAX_STATEMENTS];
    int count;
} OsaScript;

/* One scripting component. Registered storage must remain valid for the
 * process lifetime; use static or heap-lifetime components. */
typedef struct {
    uint32_t scripting_type;
    const char *name;
    int (*compile)(OsaScript *out, const char *source, char *error, size_t error_size);
    int (*execute)(const OsaScript *script, const OsaBridgeBackend *bridge,
                   AppleEventReply *last, char *error, size_t error_size);
} OsaComponent;

/* 0 on success; -1 for NULL, a zero type, a duplicate type or a full table. */
int osa_component_register(const OsaComponent *component);
const OsaComponent *osa_component_find(uint32_t scripting_type);
int osa_component_count(void);

/* OS999-only statement dialect: get PROPERTY, set PROPERTY to "VALUE",
 * shut down, restart, blank lines and -- comments. No tell blocks, variables,
 * expressions, compiled AppleScript format, OSA ABI or external app execution.
 * Compilation is atomic; errors leave an empty script. Backend callbacks are
 * explicit and never invoke host automation or shell commands here. */
int osa_script_compile(OsaScript *out, const char *source, char *error, size_t error_size);
int osa_script_execute(const OsaScript *script, const OsaBridgeBackend *bridge,
                       AppleEventReply *last, char *error, size_t error_size);

#endif
