/* Bluetooth control panel against a fake source: no radio, no display. */
#include "cdev_bluetooth.h"
#include "control_panels.h"
#include "control_panels_int.h"
#include "desktop_assets.h"
#include "finder_files.h"
#include "os999_cdev.h"
#include "ospi.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int f_avail = 1, f_state, f_scans, f_connects, f_disconnects, f_last_connect = -1, f_nd = 2;
static int f_avail_calls;
static int fa(void) { f_avail_calls++; return f_avail; }
static const char *fr(void) { return "No controller (fake)"; }
static int fs(void) { return f_state; }
static int fe(void) { return -4; }
static int fn(void) { return f_nd; }
static int fd(int i, char *name, size_t nn, char *addr, size_t an) {
    if (i < 0 || i >= f_nd) return -1;
    snprintf(name, nn, i ? "Keyboard" : "Phone");
    snprintf(addr, an, "00:11:22:33:44:%02X", i);
    return 0;
}
static int fscan(int sec) { f_scans++; return sec == 10 ? 0 : -1; }
static int fconn(int i) { f_connects++; f_last_connect = i; return 0; }
static void fdis(void) { f_disconnects++; }
static const CdevBtSource k_fake = {fa, fr, fs, fe, fn, fd, fscan, fconn, fdis};

static void click_at(Session *s, int x, int y) { session_down(s, x, y, 1); session_up(s, x, y); }
static unsigned hash_px(const uint32_t *p) {
    unsigned h = 2166136261u;
    for (int i = 0; i < OSPI_W * OSPI_H; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

int main(int argc, char **argv) {
    CHECK(desktop_assets_load(argc > 1 ? argv[1] : "assets/icons/ospi-icons.icl8"));
    char root[] = "/tmp/os999-cdev-bt-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    Session *s = session_create(root);
    CHECK(s != NULL);
    uint32_t *px = calloc((size_t)OSPI_W * OSPI_H, sizeof *px);
    uint32_t *px2 = calloc((size_t)OSPI_W * OSPI_H, sizeof *px2);
    if (!s || !px || !px2) return 1;

    const os999_cdev_t *m = os999_cdev_find("bluetooth");
    CHECK(m != NULL && m->category == OS999_CDEV_CAT_HARDWARE);

    /* No source: the panel opens, says so, and the buttons do nothing. */
    session_command(s, CMD_CP_BLUETOOTH);
    CHECK(control_panels_active() && control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "bluetooth") == 0);
    session_paint(s, px, OSPI_W, OSPI_H);
    Rect c = cp_content_rect(cp_window_rect(s));
    click_at(s, c.x + 30, c.y + 100);
    CHECK(f_scans == 0);

    /* Fake source: adapter present, scan, select, connect. */
    cdev_bluetooth_set_source(&k_fake);
    session_paint(s, px2, OSPI_W, OSPI_H);
    CHECK(hash_px(px) != hash_px(px2));
    CHECK(f_avail_calls == 1);
    session_paint(s, px2, OSPI_W, OSPI_H);
    CHECK(f_avail_calls == 1); /* availability is not polled per frame */

    click_at(s, c.x + 16 + 10, c.y + 92 + 10); /* Scan */
    CHECK(f_scans == 1);
    click_at(s, c.x + 136 + 10, c.y + 92 + 10); /* Connect without a selection */
    CHECK(f_connects == 0);
    click_at(s, c.x + 12 + 20, c.y + 152 + 22 + 8); /* second row */
    click_at(s, c.x + 136 + 10, c.y + 92 + 10);
    CHECK(f_connects == 1 && f_last_connect == 1);

    f_state = CDEV_BT_UP;
    click_at(s, c.x + 256 + 10, c.y + 92 + 10); /* Disconnect */
    CHECK(f_disconnects == 1);
    session_paint(s, px2, OSPI_W, OSPI_H);

    f_state = CDEV_BT_FAILED;
    session_paint(s, px2, OSPI_W, OSPI_H);

    /* Unavailable adapter: Scan must not reach the driver. */
    f_state = CDEV_BT_IDLE;
    f_avail = 0;
    click_at(s, c.x + 16 + 10, c.y + 92 + 10);
    CHECK(f_scans == 1);
    session_paint(s, px2, OSPI_W, OSPI_H);

    cdev_bluetooth_set_source(NULL);
    session_paint(s, px2, OSPI_W, OSPI_H);
    control_panels_close();

    /* The Control Panels menu lists it, enabled. */
    session_command(s, CMD_CP_BLUETOOTH);
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "bluetooth") == 0);
    control_panels_close();

    session_destroy(s);
    free(px); free(px2);
    printf("cdev_bluetooth: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
