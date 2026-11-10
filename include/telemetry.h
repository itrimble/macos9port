#ifndef OS999_TELEMETRY_H
#define OS999_TELEMETRY_H
/* Local-only interaction log: one JSON object per line in
 * ~/Library/Application Support/OS999/Telemetry/events-YYYYMMDD.jsonl.
 * Nothing leaves the machine. Typed characters are never recorded (only
 * "char"); shortcuts and special keys are. It is off until the user turns it
 * on (Help, Record Local Usage Log); OS999_TELEMETRY=0 forces it off. */
#include "ospi.h"

void telemetry_click(Session *s, int x, int y, int button, int clicks);
void telemetry_key(Session *s, int key, int mods);
void telemetry_command(Session *s, int cmd);
void telemetry_frame(Session *s);   /* once per frame: logs what changed */
void telemetry_close(void);
/* Test hooks. */
void telemetry_set_path(const char *path);   /* NULL = default location */
int telemetry_enabled(void);
void telemetry_start(void);    /* desktop app: read the saved consent once */
int telemetry_consent(void);   /* 1 when the user has turned recording on */
int telemetry_set_consent(int on);   /* save the choice; 0 on success */
#endif
