#include "desktop_help.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "session_int.h"

int desktop_help_command(Session *s, int command) {
    const char *title, *key, *body;
    if (command == CMD_TELEMETRY_TOGGLE) {
        int on = !telemetry_consent();
        if (telemetry_set_consent(on)) dialog_info(s, "Usage Log", "The setting could not be saved.");
        else dialog_info(s, "Usage Log", on
            ? "Recording is on. Clicks, shortcuts, commands and window names are kept inside the OS999 disk, in its hidden .os999/Telemetry folder. Typed text is never recorded and nothing is sent anywhere."
            : "Recording is off. Existing log files were left in place.");
        return 1;
    }
    switch (command) {
    case CMD_HELP:
        title = "How to Use OS999"; key = ":help";
        body = "Open folders and files with a double-click or File, Open. Cmd-Q quits the active application; in Finder it quits the desktop.\n\n"
               "Right-click or Control-click an item for its contextual menu. "
               "Click outside a menu or press Escape to dismiss it.\n\n"
               "View switches folders between icons, buttons and a list, and sorts by name, kind, date or size. Click a list heading to sort by that column.\n\n"
               "Edit, Select All selects folder items. Copy Names copies their names; "
               "Copy Text copies the contents of a text window. It does not copy files.\n\n"
               "OS999, Control Panels, Appearance changes desktop pictures and color themes.\n\n"
               "File, Install from Garden uses a Macintosh Garden link from your clipboard. "
               "OS999's own runtime remains experimental.\n\n"
               "Shift-click selects multiple items. Drag selected items between folders to move them; Option-drag copies them. Drag to Trash to save their original locations for Put Away. Empty Trash asks before permanent deletion.";
        break;
    case CMD_HELP_KEYS:
        title = "Keyboard Shortcuts"; key = ":help-keys";
        body = "Cmd-N  New Folder\nCmd-O or Return  Open\nCmd-W  Close\nCmd-I  Get Info\n"
               "Cmd-A  Select All folder items\nCmd-C  Copy names or text\nCmd-T  Next color theme\nCmd-Q  Quit\nCmd-Tab  Next application\nCmd-Shift-Tab  Previous application\n\n"
               "In folders: arrow keys select items, Home/End select first/last, and a letter "
               "cycles through names with that initial. Selection scrolls into view.\n\n"
               "With a menu open: Up/Down choose an available action, Left/Right switch menus, "
               "Return runs the action, and Escape closes the menu.\n\n"
               "F9  Toggle startup introduction\nF10  Preview startup\n\n"
               "Shortcuts inside a classic application go to that application. "
               "Click the desktop or its menu bar to use desktop commands.\n\n"
               "Application Switcher: drag the application menu out onto the desktop, or choose Application Switcher from that menu. Click an application to switch; Option-click hides the previous application. The rightmost title box toggles names; Option-click it changes icon size; Option-Shift-click changes orientation. The adjacent box collapses a vertical switcher. Drag its right edge to resize. Drop one supported media file onto OS999Amp to open it without moving it. Other applications do not yet accept document drops.";
        break;
    case CMD_HELP_STATUS:
        title = "What Works Today"; key = ":help-status";
        body = "Desktop\nFolders with saved layouts, pop-up tabs, name search, labels, aliases, text viewing, icons, buttons and list view, contextual menus, "
               "desktop pictures and color themes are available.\n\n"
               "Editing\nCopy Names and Copy Text use the Mac clipboard. File Cut/Paste, "
               "and text editing are not implemented. Undo restores moves, Trash operations and renames. Undoing copies or aliases moves the created items to Trash.\n\n"
               "Themes\nBuilt-in themes change colors. Custom theme files and window-shape skins "
               "are not supported yet.\n\n"
               "Classic applications\nThe independent runtime remains experimental. Some programs "
               "start and draw, but gameplay, sound and saving vary.\n\n"
               "Printing and applications\nOn macOS, Page Setup and Print open the host print panels. Finder prints plain text below 64 KiB; Print Window prints the visible folder or text window. The application menu switches between Finder, Browser, OS999Amp and the current classic application when running, with Hide and Show All. Cmd-Tab cycles forward and Cmd-Shift-Tab cycles backward when available; use the application menu if your host reserves that shortcut. The runtime currently permits one classic application at a time.\n\n"
               "Modern browsing, network services and classic QuickTime playback "
               "have not passed end-to-end acceptance.";
        break;
    default: return 0;
    }
    context_close(); appearance_panel_close();
    if (session_open_text(s, title, key, body)) dialog_info(s, "Help", "Close a window first.");
    return 1;
}
