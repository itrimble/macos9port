#include "menu_navigation.h"
#include <stdio.h>

/* Fixture menu APIs test the production navigation module, not production
 * catalog availability or session-command behavior. Integration tests cover those. */
static const MenuItem fixture[] = {
    {"Disabled", CMD_QUIT, MI_DISABLED, NULL, 0},
    {NULL, CMD_QUIT, 0, NULL, 0},
    {"Open", CMD_OPEN, 0, NULL, 0},
    {"Close", CMD_CLOSE, 0, NULL, 0}
};
static const MenuItem unavailable[] = {
    {"Unavailable", CMD_QUIT, MI_DISABLED, NULL, 0}, {NULL, 0, 0, NULL, 0}
};
int menu_submenu(int command) { (void)command; return 0; }
int menu_count(void) { return 3; }
int menu_nitems(int menu) { return menu == 0 ? 4 : menu == 1 ? 2 : 0; }
const MenuItem *menu_at(int menu, int row) {
    if (row < 0 || row >= menu_nitems(menu)) return NULL;
    return menu == 0 ? &fixture[row] : &unavailable[row];
}
static int dispatched, calls, dismissed_before_dispatch;
void session_command(Session *s, int command) {
    dispatched = command; calls++; dismissed_before_dispatch = s->menu == -1;
}
static int failed, checks;
#define CHECK(c) do { checks++; if (!(c)) { failed++; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); } } while (0)
int main(void) {
    Session s = {.menu = -1, .menu_hot = -1};
    CHECK(!menu_navigation_key(NULL, MENU_KEY_DOWN, 0));
    CHECK(!menu_navigation_key(&s, MENU_KEY_DOWN, 0) && s.menu == -1);
    s.menu = 0;
    CHECK(menu_navigation_key(&s, MENU_KEY_DOWN, 0) && s.menu_hot == 2);
    CHECK(menu_navigation_key(&s, MENU_KEY_DOWN, 0) && s.menu_hot == 3);
    CHECK(menu_navigation_key(&s, MENU_KEY_DOWN, 0) && s.menu_hot == 2);
    CHECK(menu_navigation_key(&s, MENU_KEY_UP, 0) && s.menu_hot == 3);
    s.menu_hot = -1;
    CHECK(menu_navigation_key(&s, MENU_KEY_UP, 0) && s.menu_hot == 3);
    CHECK(menu_navigation_key(&s, '\r', 0) && calls == 1 && dispatched == CMD_CLOSE && dismissed_before_dispatch);
    s.menu = 0; s.menu_hot = 1;
    CHECK(menu_navigation_key(&s, '\r', 0) && calls == 1 && s.menu == -1);
    s.menu = 0; s.menu_hot = 0;
    CHECK(menu_navigation_key(&s, '\r', 0) && calls == 1);
    s.menu = 0;
    CHECK(menu_navigation_key(&s, MENU_KEY_RIGHT, 0) && s.menu == 1 && s.menu_hot == -1);
    CHECK(menu_navigation_key(&s, MENU_KEY_DOWN, 0) && s.menu_hot == -1);
    CHECK(menu_navigation_key(&s, MENU_KEY_RIGHT, 0) && s.menu == 2 && s.menu_hot == -1);
    CHECK(menu_navigation_key(&s, MENU_KEY_UP, 0) && s.menu_hot == -1);
    CHECK(menu_navigation_key(&s, MENU_KEY_RIGHT, 0) && s.menu == 0 && s.menu_hot == 2);
    CHECK(menu_navigation_key(&s, MENU_KEY_LEFT, 0) && s.menu == 2);
    s.down = s.drag = s.click_valid = 1;
    CHECK(!menu_navigation_key(&s, 'q', MOD_CMD) && s.menu == -1 && !s.down && !s.drag && !s.click_valid && calls == 1);
    s.menu = 0;
    CHECK(menu_navigation_key(&s, 'x', 0) && s.menu == 0 && calls == 1);
    CHECK(menu_navigation_key(&s, 27, 0) && s.menu == -1);
    s.menu = 999;
    CHECK(menu_navigation_key(&s, MENU_KEY_DOWN, 0) && s.menu == -1);
    printf("Menu navigation: %d checks, %d failures\n", checks, failed);
    return failed != 0;
}
