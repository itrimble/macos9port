#include "desktop_assets.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>

typedef struct { const char *name, *icon; } IconName;
static const IconName folders[] = {
    {"System Folder", "system_folder"}, {"Applications", "applications_folder"},
    {"Documents", "documents_folder"}, {"Downloads", "downloads_folder"},
    {"Macintosh Garden", "garden_folder"}, {"Garden", "garden_folder"},
    {"Preferences", "preferences_folder"}, {"Extensions", "extensions_folder"},
    {"Control Panels", "control_panels_folder"}, {"Fonts", "fonts_folder"},
    {"Startup Items", "startup_items_folder"}, {"Home", "home_folder"},
    {"Music", "music_folder"}, {"Pictures", "pictures_folder"},
    {"Movies", "movies_folder"}, {"Utilities", "utilities_folder"},
    {"Shared", "shared_folder"}
};
static const IconName extensions[] = {
    {"txt", "text_document"}, {"text", "text_document"}, {"md", "text_document"},
    {"log", "text_document"}, {"csv", "text_document"},
    {"rtf", "rich_text_document"}, {"doc", "rich_text_document"},
    {"docx", "rich_text_document"}, {"wpd", "rich_text_document"},
    {"png", "image_document"}, {"jpg", "image_document"}, {"jpeg", "image_document"},
    {"gif", "image_document"}, {"bmp", "image_document"}, {"pict", "image_document"},
    {"pct", "image_document"}, {"tif", "image_document"}, {"tiff", "image_document"},
    {"icns", "image_document"}, {"svg", "image_document"},
    {"wav", "sound_document"}, {"aif", "sound_document"}, {"aiff", "sound_document"},
    {"mp3", "sound_document"}, {"m4a", "sound_document"}, {"mid", "sound_document"},
    {"midi", "sound_document"}, {"snd", "sound_document"},
    {"mov", "movie_document"}, {"mp4", "movie_document"}, {"m4v", "movie_document"},
    {"avi", "movie_document"}, {"mpeg", "movie_document"}, {"mpg", "movie_document"},
    {"pdf", "portable_document"}, {"sit", "archive_sit"}, {"sitx", "archive_sit"},
    {"sea", "archive_sit"}, {"zip", "archive_zip"}, {"gz", "archive_zip"},
    {"tar", "archive_zip"}, {"tgz", "archive_zip"}, {"7z", "archive_zip"},
    {"hqx", "archive_sit"}, {"cpt", "archive_sit"},
    {"ttf", "font_file"}, {"otf", "font_file"}, {"ttc", "font_file"},
    {"dfont", "font_file"}, {"pfa", "font_file"}, {"pfb", "font_file"},
    {"pref", "preferences_file"}, {"prefs", "preferences_file"},
    {"plist", "preferences_file"}, {"ini", "preferences_file"}, {"cfg", "preferences_file"},
    {"scpt", "script_document"}, {"applescript", "script_document"},
    {"sh", "script_document"}, {"zsh", "script_document"}, {"py", "script_document"},
    {"c", "source_code"}, {"h", "source_code"}, {"cpp", "source_code"},
    {"cc", "source_code"}, {"m", "source_code"}, {"mm", "source_code"},
    {"swift", "source_code"}, {"pas", "source_code"}, {"rs", "source_code"},
    {"html", "web_document"}, {"htm", "web_document"}, {"url", "web_document"},
    {"webloc", "web_document"}, {"img", "disk_image"}, {"dsk", "disk_image"},
    {"dmg", "disk_image"}, {"iso", "disk_image"}, {"toast", "disk_image"}
};
static const IconName applications[] = {
    {"SimpleText", "text_editor"}, {"TeachText", "text_editor"}, {"TextEdit", "text_editor"},
    {"BBEdit", "text_editor"}, {"WordPerfect", "text_editor"}, {"Microsoft Word", "text_editor"},
    {"Calculator", "calculator"}, {"Clock", "clock"}, {"Alarm Clock", "clock"},
    {"Terminal", "terminal"}, {"MacTerminal", "terminal"},
    {"Control Panels", "settings"}, {"Settings", "settings"}, {"Help Viewer", "help"},
    {"Garden Installer", "garden_installer"}, {"iTunes", "music_player"},
    {"SoundApp", "music_player"}, {"SimpleImage", "photo_viewer"},
    {"PictureViewer", "photo_viewer"}, {"JPEGView", "photo_viewer"},
    {"MacSurf", "web_browser"}, {"Netscape", "web_browser"},
    {"Netscape Navigator", "web_browser"}, {"Internet Explorer", "web_browser"},
    {"Chromium", "web_browser"}, {"iCab", "web_browser"},
    {"Mail", "mail"}, {"Eudora", "mail"}, {"Outlook Express", "mail"},
    {"Chat", "chat"}, {"IRCle", "chat"}, {"Calendar", "calendar"},
    {"Note Pad", "notes"}, {"Stickies", "notes"}, {"Notes", "notes"},
    {"Puzzle", "puzzle_game"}, {"Sokoban", "puzzle_game"},
    {"MacPaint", "paint"}, {"Paint", "paint"}, {"Scrapbook", "scrapbook"},
    {"StuffIt Expander", "archive_utility"}, {"StuffIt Deluxe", "archive_utility"},
    {"DropStuff", "archive_utility"}, {"Disk Utility", "disk_utility"},
    {"Disk First Aid", "disk_utility"}, {"Drive Setup", "disk_utility"},
    {"Apple System Profiler", "system_profiler"}, {"System Profiler", "system_profiler"},
    {"OS999 Runtime", "runtime_68k"}, {"OS Pi Runtime", "runtime_68k"},
    {"OS Pi", "ospi_desktop"}, {"OS999 Stylized", "ospi_desktop"}
};

static const char *exact(const IconName *map, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) if (!strcasecmp(name, map[i].name)) return map[i].icon;
    return NULL;
}
static const char *parent_name(const char *path) {
    if (!path || strnlen(path, OSPI_PATH) == OSPI_PATH) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const char *desktop_asset_name(const Item *item, const char *parent_rel) {
    if (!item || strnlen(item->name, sizeof item->name) == sizeof item->name) return "document";
    const char *name = item->name, *icon;
    const char *parent = parent_name(parent_rel);
    if (item->kind == ITEM_SYSTEM) return "system_folder";
    if (item->kind == ITEM_FOLDER) {
        if (item->special[0]) return item->special;   /* kept through a rename */
        icon = exact(folders, sizeof folders / sizeof *folders, name);
        return icon ? icon : "folder";
    }
    if (item->kind == ITEM_APPLICATION) {
        if (!strcasecmp(parent, "Control Panels")) return "control_panel";
        char app[OSPI_NAME];
        memcpy(app, name, strlen(name) + 1);
        size_t n = strlen(app);
        if (n > 4 && !strcasecmp(app + n - 4, ".app")) app[n - 4] = 0;
        icon = exact(applications, sizeof applications / sizeof *applications, app);
        if (icon) return icon;
        /* Accept a conventional numeric version suffix, not arbitrary substrings. */
        for (size_t i = 0; i < sizeof applications / sizeof *applications; i++) {
            size_t len = strlen(applications[i].name);
            if (strlen(app) <= len + 1 || strncasecmp(app, applications[i].name, len) ||
                app[len] != ' ' || !isdigit((unsigned char)app[len + 1])) continue;
            size_t j = len + 1;
            while (isdigit((unsigned char)app[j]) || app[j] == '.') j++;
            if (!app[j]) return applications[i].icon;
        }
        return "application";
    }
    const char *dot = strrchr(name, '.');
    if (dot && dot != name && dot[1]) {
        icon = exact(extensions, sizeof extensions / sizeof *extensions, dot + 1);
        if (icon) return icon;
    }
    if (!strcasecmp(parent, "Fonts")) return "font_file";
    if (!strcasecmp(parent, "Preferences")) return "preferences_file";
    if (!strcasecmp(parent, "Extensions")) return "extension";
    if (!strcasecmp(parent, "Control Panels")) return "control_panel";
    return "document";
}
