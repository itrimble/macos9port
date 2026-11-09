/* Standard File list model: ordering, selection, scrolling, double-click and type-ahead. */
#include "mac_stdfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static MacFileEntry named(const char *n) { MacFileEntry e; memset(&e, 0, sizeof e); strcpy(e.name, n); return e; }
static void fill(MacSfList *l, unsigned rows, unsigned n) {
    MacFileEntry e[40]; CHECK(n <= 40);
    for (unsigned i = 0; i < n; i++) { char b[16]; snprintf(b, sizeof b, "file%02u", i); e[n - 1 - i] = named(b); } /* reverse order in */
    mac_sflist_init(l, rows); CHECK(mac_sflist_set(l, e, n) == 0);
}
int main(void) {
    MacSfList l;
    /* Case-insensitive order, ties by bytes, input order irrelevant. */
    MacFileEntry mix[] = { named("zebra"), named("Apple"), named("apple"), named("Banana"), named("aardvark") };
    mac_sflist_init(&l, 4); CHECK(mac_sflist_set(&l, mix, 5) == 0 && l.count == 5);
    const char *order[] = { "aardvark", "Apple", "apple", "Banana", "zebra" };
    for (unsigned i = 0; i < 5; i++) CHECK(!strcmp(l.entries[i].name, order[i]));
    CHECK(l.selected == -1 && l.top == 0);
    /* Equal ignoring case: byte order decides, whichever way they arrive. One name that is a prefix of another sorts first. */
    MacFileEntry tie1[] = { named("apple"), named("Apple"), named("Game 2"), named("Game") };
    MacFileEntry tie2[] = { named("Game"), named("Game 2"), named("Apple"), named("apple") };
    MacSfList t; mac_sflist_init(&t, 4);
    CHECK(mac_sflist_set(&t, tie1, 4) == 0 && !strcmp(t.entries[0].name, "Apple") && !strcmp(t.entries[1].name, "apple") &&
          !strcmp(t.entries[2].name, "Game") && !strcmp(t.entries[3].name, "Game 2"));
    CHECK(mac_sflist_set(&t, tie2, 4) == 0 && !strcmp(t.entries[0].name, "Apple") && !strcmp(t.entries[1].name, "apple") &&
          !strcmp(t.entries[2].name, "Game") && !strcmp(t.entries[3].name, "Game 2"));
    /* A new list forgets the old click and scroll position. */
    MacFileEntry again[] = { named("a"), named("b"), named("c"), named("d"), named("e"), named("f") };
    fill(&t, 2, 10); mac_sflist_scroll_to(&t, 5); CHECK(t.top == 5);
    CHECK(mac_sflist_set(&t, again, 6) == 0 && t.top == 0 && t.selected == -1);   /* scroll forgotten */
    CHECK(mac_sflist_click(&t, 1, 100, 30) == 0 && t.last_row == 1);
    CHECK(mac_sflist_set(&t, again, 6) == 0);
    CHECK(mac_sflist_click(&t, 1, 105, 30) == 0 && t.selected == 1); /* same row, in time, but a different list */
    mac_sflist_free(&t);
    mix[0] = named("changed"); CHECK(!strcmp(l.entries[4].name, "zebra")); /* copied, not aliased */
    mac_sflist_free(&l);

    /* Clicks select; a second click on the same row within the double time opens; slower or elsewhere does not. */
    fill(&l, 4, 10);
    CHECK(mac_sflist_click(&l, 2, 100, 30) == 0 && l.selected == 2);
    CHECK(mac_sflist_click(&l, 2, 130, 30) == 1 && l.selected == 2);   /* exactly the double time still counts */
    CHECK(mac_sflist_click(&l, 2, 140, 30) == 0);                      /* the double click was consumed */
    CHECK(mac_sflist_click(&l, 3, 150, 30) == 0 && l.selected == 3);
    CHECK(mac_sflist_click(&l, 3, 181, 30) == 0);                      /* one tick too slow */
    CHECK(mac_sflist_click(&l, 1, 182, 30) == 0 && mac_sflist_click(&l, 2, 183, 30) == 0); /* different rows */
    CHECK(mac_sflist_click(&l, 4, 190, 30) == 0 && l.selected == 2);   /* row 4 is outside the 4 shown rows */
    mac_sflist_scroll_to(&l, 3);
    CHECK(mac_sflist_click(&l, 0, 300, 30) == 0 && l.selected == 3);   /* row is relative to the scroll position */
    mac_sflist_free(&l);
    fill(&l, 8, 3);
    CHECK(mac_sflist_click(&l, 5, 10, 30) == 0 && l.selected == -1);   /* past the last entry: no change */
    mac_sflist_free(&l);

    /* Arrow keys: start from the ends, clamp, and scroll to keep the selection visible. */
    fill(&l, 4, 10);
    mac_sflist_move(&l, 1); CHECK(l.selected == 0 && l.top == 0);
    for (int i = 0; i < 5; i++) mac_sflist_move(&l, 1);
    CHECK(l.selected == 5 && l.top == 2);                              /* row 5 is the bottom row of rows 2..5 */
    for (int i = 0; i < 20; i++) mac_sflist_move(&l, 1);
    CHECK(l.selected == 9 && l.top == 6);
    for (int i = 0; i < 20; i++) mac_sflist_move(&l, -1);
    CHECK(l.selected == 0 && l.top == 0);
    mac_sflist_move(&l, -1); CHECK(l.selected == 0);   /* clamps at the first entry */
    mac_sflist_free(&l);
    fill(&l, 4, 10); mac_sflist_move(&l, -1); CHECK(l.selected == 9 && l.top == 6); mac_sflist_free(&l); /* up from nothing */

    /* Paging moves the view by rows-1 and clamps; the selection stays. */
    fill(&l, 4, 10);
    mac_sflist_click(&l, 0, 1, 30);
    mac_sflist_page(&l, 1); CHECK(l.top == 3 && l.selected == 0);
    mac_sflist_page(&l, 1); CHECK(l.top == 6);
    mac_sflist_page(&l, 1); CHECK(l.top == 6);                         /* max top = 10 - 4 */
    mac_sflist_page(&l, -1); CHECK(l.top == 3);
    mac_sflist_page(&l, -1); mac_sflist_page(&l, -1); CHECK(l.top == 0);
    mac_sflist_scroll_to(&l, 99); CHECK(l.top == 6);
    mac_sflist_free(&l);
    fill(&l, 4, 3); mac_sflist_page(&l, 1); CHECK(l.top == 0); mac_sflist_scroll_to(&l, 2); CHECK(l.top == 0); mac_sflist_free(&l);

    /* Type-ahead: letters within the window extend one prefix; later letters start a new one; no match keeps selection. */
    MacFileEntry names[] = { named("Alpha"), named("alps"), named("Beta"), named("Bravo"), named("Charlie"), named("delta") };
    mac_sflist_init(&l, 2); CHECK(mac_sflist_set(&l, names, 6) == 0);
    mac_sflist_type(&l, 'b', 100); CHECK(l.selected == 2 && l.top == 1);
    mac_sflist_type(&l, 'R', 110); CHECK(l.selected == 3);              /* "br" -> Bravo, case-insensitive */
    mac_sflist_type(&l, 'x', 120); CHECK(l.selected == 3);              /* "brx" matches nothing: unchanged */
    mac_sflist_type(&l, 'd', 120 + MAC_SF_TYPE_WINDOW + 1); CHECK(l.selected == 5 && l.top == 4); /* new word */
    mac_sflist_type(&l, 'a', 125 + MAC_SF_TYPE_WINDOW + 1); CHECK(l.selected == 5);  /* "da" matches nothing */
    mac_sflist_type(&l, 'a', 1000); CHECK(l.selected == 0 && l.top == 0);
    mac_sflist_type(&l, 'l', 1010); mac_sflist_type(&l, 'p', 1015); CHECK(l.selected == 0); /* "alp": Alpha before alps */
    mac_sflist_type(&l, 's', 1020); CHECK(l.selected == 1);              /* "alps" */
    mac_sflist_type(&l, '\t', 1021); mac_sflist_type(&l, 0, 1021); CHECK(l.selected == 1); /* control codes ignored */
    mac_sflist_type(&l, 'b', 2000); CHECK(l.selected == 2);                  /* fresh word: Beta */
    mac_sflist_type(&l, '\t', 2001); mac_sflist_type(&l, 'r', 2002);
    CHECK(l.selected == 3);                                                  /* "br": the tab did not break the word */
    mac_sflist_free(&l);
    /* The prefix buffer is bounded. */
    mac_sflist_init(&l, 2); CHECK(mac_sflist_set(&l, names, 6) == 0);
    for (int i = 0; i < 100; i++) mac_sflist_type(&l, 'z', 1 + (uint32_t)i);
    CHECK(l.typed_length < sizeof l.typed);
    mac_sflist_free(&l);
    /* An empty list is inert. */
    mac_sflist_init(&l, 4); CHECK(mac_sflist_set(&l, NULL, 0) == 0);
    mac_sflist_move(&l, 1); mac_sflist_type(&l, 'a', 1); mac_sflist_page(&l, 1);
    CHECK(mac_sflist_click(&l, 0, 1, 30) == 0 && l.selected == -1 && l.top == 0);
    mac_sflist_free(&l);
    puts("Standard File list model checks passed");
    return 0;
}
