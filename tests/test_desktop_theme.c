#include "desktop_theme.h"
#include "draw.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static void write_text(const char *path, const char *value) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(value, 1, strlen(value), f) == strlen(value)); assert(!fclose(f));
}
int main(void) {
    assert(desktop_theme_count() == 16);
    assert(!desktop_theme_init(NULL) && desktop_theme_current() == 0);
    assert(!strcmp(desktop_theme_name(0), "Platinum"));
    assert(!strcmp(desktop_theme_name(15), "Midnight Pi"));
    assert(!strcmp(desktop_theme_name(-1), "") && !strcmp(desktop_theme_name(16), ""));
    const uint32_t defaults[] = {C_WHITE,C_MENU,C_BLACK,C_DISABLED,C_BLACK,C_TRACK,
        C_STRIPE_A,C_STRIPE_B,C_FOLDER,C_DESK,C_DESK2};
    for (int i = 0; i < THEME_ROLE_COUNT; i++) assert(desktop_theme_color(i) == defaults[i]);
    for (int t = 0; t < 16; t++) {
        assert(!desktop_theme_select(t) && desktop_theme_current() == t);
        for (int r = 0; r < THEME_ROLE_COUNT; r++) assert((desktop_theme_color(r) >> 24) == 255);
    }
    assert(!desktop_theme_next() && desktop_theme_current() == 0);
    assert(desktop_theme_select(-1) == -1 && desktop_theme_select(16) == -1);
    assert(desktop_theme_current() == 0);
    assert(desktop_theme_color(-1) == 0xff000000u && desktop_theme_color(99) == 0xff000000u);
    char dir[] = "/tmp/os999-theme-test.XXXXXX"; assert(mkdtemp(dir));
    char path[512], missing[512], link[512];
    snprintf(path,sizeof path,"%s/preference",dir);
    snprintf(missing,sizeof missing,"%s/missing/preference",dir);
    snprintf(link,sizeof link,"%s/link",dir);
    assert(!desktop_theme_init(path));
    assert(!desktop_theme_select(2));
    desktop_theme_shutdown(); assert(desktop_theme_current() == 0);
    assert(!desktop_theme_init(path) && desktop_theme_current() == 2);
    assert(!desktop_theme_next() && desktop_theme_current() == 3);
    assert(!desktop_theme_init(path) && desktop_theme_current() == 3);
    write_text(path,"unknown-theme\n"); assert(!desktop_theme_init(path) && desktop_theme_current() == 0);
    write_text(path,"platinum\nmidnight-pi"); assert(!desktop_theme_init(path) && desktop_theme_current() == 0);
    char oversized[100]; memset(oversized,'a',99); oversized[99]=0;
    write_text(path,oversized); assert(!desktop_theme_init(path) && desktop_theme_current() == 0);
    assert(!desktop_theme_init(missing));
    assert(desktop_theme_select(7) == -1 && desktop_theme_current() == 0);
    assert(desktop_theme_init(dir) == -1);
    assert(desktop_theme_select(7) == -1 && desktop_theme_current() == 0);
    assert(!symlink(path,link)); assert(desktop_theme_init(link) == -1);
    char long_path[4096]; memset(long_path,'x',sizeof long_path);
    assert(desktop_theme_init(long_path) == -1 && desktop_theme_current() == 0);
    assert(desktop_theme_init("") == -1);
    desktop_theme_shutdown();
    unlink(link); unlink(path); rmdir(dir);
    puts("desktop theme checks passed"); return 0;
}
