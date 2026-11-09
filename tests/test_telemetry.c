#include "telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *slurp(const char *p) {
    FILE *f = fopen(p, "r"); if (!f) return NULL;
    char *b = calloc(1, 65536); fread(b, 1, 65535, f); fclose(f); return b;
}

int main(void) {
    char home[] = "/tmp/os999-telemetry-home-XXXXXX";
    CHECK(mkdtemp(home));
    setenv("HOME", home, 1);
    static Session off; memset(&off, 0, sizeof off); off.menu = -1;
    telemetry_command(&off, 1); telemetry_click(&off, 1, 1, 1, 1);     /* not enabled: no file, no directory */
    char lib[600]; snprintf(lib, sizeof lib, "%s/Library", home);
    CHECK(access(lib, F_OK) != 0);
    char path[] = "/tmp/os999-telemetry-XXXXXX";
    int fd = mkstemp(path); close(fd); unlink(path);
    telemetry_set_path(path);
    static Session s;
    memset(&s, 0, sizeof s);
    s.menu = -1;
    telemetry_frame(&s);                                  /* baseline, no state event */
    telemetry_click(&s, 300, 300, 1, 1);
    telemetry_key(&s, 'p', 0);                            /* typed char: value must not be logged */
    telemetry_key(&s, 'c', MOD_CMD);                      /* shortcut is logged */
    s.nwin = 1; snprintf(s.win[0].title, sizeof s.win[0].title, "Appli\"cations");
    telemetry_command(&s, 703);
    telemetry_frame(&s);                                  /* window opened: state event */
    telemetry_frame(&s);                                  /* unchanged: nothing */
    telemetry_close();
    char *log = slurp(path);
    CHECK(log != NULL);
    if (log) {
        CHECK(strstr(log, "\"ev\":\"click\"") != NULL);
        CHECK(strstr(log, "\"target\":\"desktop\"") != NULL);
        CHECK(strstr(log, "\"kind\":\"char\"") != NULL);
        CHECK(strstr(log, "\"key\":112") == NULL);        /* 'p' never recorded */
        CHECK(strstr(log, "\"key\":99") != NULL);
        CHECK(strstr(log, "\"cmd\":703") != NULL);
        CHECK(strstr(log, "\"front\":\"Appli\\\"cations\"") != NULL);
        int states = 0; for (char *p = log; (p = strstr(p, "\"ev\":\"state\"")); p++) states++;
        CHECK(states == 1);
        free(log);
    }
    unlink(path);
    setenv("OS999_TELEMETRY", "0", 1);
    return failures ? 1 : 0;
}
