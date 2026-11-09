#include "desktop_assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(int kind, const char *name, const char *parent, const char *expected) {
    Item item = {0};
    item.kind = kind;
    snprintf(item.name, sizeof item.name, "%s", name);
    assert(!strcmp(desktop_asset_name(&item, parent), expected));
}
int main(void) {
    check(ITEM_SYSTEM, "System Folder", "", "system_folder");
    check(ITEM_FOLDER, "aPpLiCaTiOnS", "", "applications_folder");
    check(ITEM_FOLDER, "Documents", "", "documents_folder");
    check(ITEM_FOLDER, "Fonts", "System Folder", "fonts_folder");
    check(ITEM_FOLDER, "Control Panels", "System Folder", "control_panels_folder");
    check(ITEM_FOLDER, "Monkey Island", "Applications", "folder");
    check(ITEM_FOLDER, "Calculator.app", "Applications", "folder");
    check(ITEM_FILE, "Calculator", "Applications", "document");
    check(ITEM_APPLICATION, "Calculator", "Applications", "calculator");
    check(ITEM_APPLICATION, "STUFFIT EXPANDER 5.5", "Applications", "archive_utility");
    check(ITEM_APPLICATION, "StuffIt Expander Notes", "Applications", "application");
    check(ITEM_APPLICATION, "SimpleText.app", "Applications", "text_editor");
    check(ITEM_APPLICATION, "MacSurf", "Applications", "web_browser");
    /* Built-in applications get their own artwork, not the generic application icon. */
    check(ITEM_APPLICATION, "Web Browser", "Applications", "web_browser");
    check(ITEM_APPLICATION, "OS999 Amp", "Applications", "amp_player");
    check(ITEM_APPLICATION, "Movie Player", "Applications", "movie_player");
    check(ITEM_APPLICATION, "OS999 Console", "Applications", "terminal");
    check(ITEM_APPLICATION, "Tailscale", "Applications", "network");
    check(ITEM_APPLICATION, "Tailscale", "Utilities", "network");
    check(ITEM_APPLICATION, "Quake III Arena", "Applications", "quake3");
    check(ITEM_APPLICATION, "Monkey Island", "Applications", "application");
    check(ITEM_APPLICATION, "Sound", "System Folder/Control Panels", "control_panel");
    check(ITEM_FILE, "intro.MOV", "Applications/Game", "movie_document");
    check(ITEM_FILE, "song.aiff", "Music", "sound_document");
    check(ITEM_FILE, "Archive.SIT", "Downloads", "archive_sit");
    check(ITEM_FILE, "Archive.zip", "Downloads", "archive_zip");
    check(ITEM_FILE, "image.PNG", "Pictures", "image_document");
    check(ITEM_FILE, "readme.txt", "System Folder/Fonts", "text_document");
    check(ITEM_FILE, "Geneva", "System Folder/Fonts", "font_file");
    check(ITEM_FILE, "Thing", "System Folder/Preferences", "preferences_file");
    check(ITEM_FILE, "Thing", "System Folder/Extensions", "extension");
    check(ITEM_FILE, "source.c", "Documents", "source_code");
    check(ITEM_FILE, "script.scpt", "Documents", "script_document");
    check(ITEM_FILE, "movie.mov.txt", "", "text_document");
    check(ITEM_FILE, ".txt", "", "document");
    check(ITEM_FILE, "Read Me", NULL, "document");
    assert(!strcmp(desktop_asset_name(NULL, NULL), "document"));
    Item malformed; memset(&malformed, 'X', sizeof malformed);
    assert(!strcmp(desktop_asset_name(&malformed, NULL), "document"));
    char parent[OSPI_PATH]; memset(parent, 'X', sizeof parent);
    check(ITEM_FILE, "a.txt", parent, "text_document");
    puts("desktop icon map checks passed");
    return 0;
}
