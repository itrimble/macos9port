#include "desktop_help.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "session_int.h"

int desktop_help_command(Session *s, int command) {
    const char *title, *key, *body;
    switch (command) {
    case CMD_HELP:
        title = "How to Use OS999 Stylized"; key = ":help";
        body = "Open folders and files with a double-click or File, Open. Cmd-Q quits the desktop.\n\n"
               "Right-click or Control-click an item for its contextual menu. "
               "Click outside a menu or press Escape to dismiss it.\n\n"
               "View switches folders between icons and a list, and sorts by name or kind. Click a list heading to sort by that column.\n\n"
               "Edit, Select All selects folder items. Copy Names copies their names; "
               "Copy Text copies the contents of a text window. It does not copy files.\n\n"
               "OS999, Control Panels, Appearance changes desktop pictures and color themes.\n\n"
               "File, Install from Garden uses a Macintosh Garden link from your clipboard. "
               "OS999's own runtime remains experimental.\n\n"
               "Drag a file to Trash to move it there. Empty Trash asks before deleting files permanently.";
        break;
    case CMD_HELP_KEYS:
        title = "Keyboard Shortcuts"; key = ":help-keys";
        body = "Cmd-N  New Folder\nCmd-O or Return  Open\nCmd-W  Close\nCmd-I  Get Info\n"
               "Cmd-A  Select All folder items\nCmd-C  Copy names or text\nCmd-T  Next color theme\nCmd-Q  Quit\n\n"
               "In folders: arrow keys select items, Home/End select first/last, and a letter "
               "cycles through names with that initial. Selection scrolls into view.\n\n"
               "With a menu open: Up/Down choose an available action, Left/Right switch menus, "
               "Return runs the action, and Escape closes the menu.\n\n"
               "F9  Toggle startup introduction\nF10  Preview startup\n\n"
               "Shortcuts inside a classic application go to that application. "
               "Click the desktop or its menu bar to use desktop commands.";
        break;
    case CMD_HELP_STATUS:
        title = "What Works Today"; key = ":help-status";
        body = "Desktop\nFolders, text viewing, icons and list view, contextual menus, "
               "desktop pictures and color themes are available.\n\n"
               "Editing\nCopy Names and Copy Text use the Mac clipboard. File Cut/Paste, "
               "Undo and text editing are not implemented.\n\n"
               "Themes\nBuilt-in themes change colors. Custom theme files and window-shape skins "
               "are not supported yet.\n\n"
               "Classic applications\nThe independent runtime remains experimental. Some programs "
               "start and draw, but gameplay, sound and saving vary.\n\n"
               "Modern browsing, network services and classic QuickTime playback "
               "have not passed end-to-end acceptance.";
        break;
    default: return 0;
    }
    context_close(); appearance_panel_close();
    if (session_open_text(s, title, key, body)) dialog_info(s, "Help", "Close a window first.");
    return 1;
}
