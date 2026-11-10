#include "term.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"
#include "session_int.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static void con_write(TermVt *vt, const char *s) {
    if (vt && s) term_vt_feed(vt, s, strlen(s));
}

static void con_printf(TermVt *vt, const char *fmt, ...) {
    if (!vt || !fmt) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        term_vt_feed(vt, buf, len);
    }
}

static void con_prompt(TermVt *vt) {
    con_write(vt, "\x1b[1;34mos999>\x1b[0m ");
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static void trim_trailing(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static int str_icontains(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nl) == 0) return 1;
    }
    return 0;
}

void term_console_init(TermConsole *con, Session *s, TermVt *vt) {
    if (!con) return;
    memset(con, 0, sizeof(*con));
    con->session = s;
    if (vt) {
        con_write(vt, "\x1b[1mOS999 Console\x1b[0m \xe2\x80\x94 Desktop Command Interface\r\n");
        con_write(vt, "Type \x1b[1mhelp\x1b[0m for available desktop commands.\r\n\r\n");
        con_prompt(vt);
    }
}

static int cmd_help(TermVt *vt) {
    con_write(vt,
        "Available OS999 Console commands:\r\n"
        "  help                      Show this command summary\r\n"
        "  about                     Show OS999 desktop & host info\r\n"
        "  theme list                List available color themes\r\n"
        "  theme set <name|id>       Activate a color theme (e.g. platinum)\r\n"
        "  wallpaper list            List available desktop pictures\r\n"
        "  wallpaper set <id|plain>  Activate a desktop picture or plain desktop\r\n"
        "  sound preview <id>        Preview a system sound (startup, alert, menu)\r\n"
        "  open <target>             Open a folder, document, or application\r\n"
        "  clear                     Clear the console screen\r\n"
        "  shutdown                  Shut down OS999 (same as Special > Shut Down)\r\n"
        "  restart                   Restart OS999 (same as Special > Restart)\r\n"
        "  quit / exit               Close this console window\r\n");
    return 0;
}

/* Shut Down and Restart act on the whole desktop, so they go through the same commands as the menu. */
static int cmd_power(TermConsole *con, TermVt *vt, int restart) {
    if (!con->session) { con_write(vt, "No desktop session is available.\r\n"); return -1; }
    con_write(vt, restart ? "Restarting OS999...\r\n" : "Shutting down OS999...\r\n");
    session_command(con->session, restart ? CMD_RESTART_DO : CMD_SHUTDOWN);
    return 0;
}

static int cmd_about(TermConsole *con, TermVt *vt) {
    Session *s = con->session;
    uint64_t ram_mb = (s ? s->ram : host_ram()) / (1024ull * 1024ull);
    uint64_t free_mb = (s ? s->free_bytes : 0) / (1024ull * 1024ull);
    const char *arch = (s && s->arch[0]) ? s->arch : host_arch();
    con_printf(vt,
        "OS999 \xe2\x80\x94 Classic Desktop Environment\r\n"
        "  Architecture : %s\r\n"
        "  Host Memory  : %llu MB\r\n"
        "  Volume Free  : %llu MB\r\n"
        "  Active Theme : %s\r\n",
        arch ? arch : "unknown",
        (unsigned long long)ram_mb,
        (unsigned long long)free_mb,
        desktop_theme_name(desktop_theme_current()));
    return 0;
}

static int cmd_theme(TermConsole *con, TermVt *vt, const char *arg) {
    arg = skip_ws(arg);
    int count = desktop_theme_count();
    if (strcmp(arg, "list") == 0 || *arg == '\0') {
        int cur = desktop_theme_current();
        for (int i = 0; i < count; i++) {
            con_printf(vt, "  %s [%d] %s\r\n", (i == cur) ? "*" : " ", i, desktop_theme_name(i));
        }
        return 0;
    }
    if (strncmp(arg, "set", 3) == 0 && (arg[3] == '\0' || isspace((unsigned char)arg[3]))) {
        const char *q = skip_ws(arg + 3);
        if (!*q) {
            con_write(vt, "Usage: theme set <name|id>\r\n");
            return -1;
        }
        int match = -1;
        char *endp = NULL;
        long idx = strtol(q, &endp, 10);
        if (endp && *endp == '\0' && idx >= 0 && idx < count) {
            match = (int)idx;
        }
        for (int i = 0; match < 0 && i < count; i++) {
            if (strcasecmp(desktop_theme_name(i), q) == 0) match = i;
        }
        for (int i = 0; match < 0 && i < count; i++) {
            if (str_icontains(desktop_theme_name(i), q)) match = i;
        }
        if (match < 0) {
            con_printf(vt, "Unknown theme: %s\r\n", q);
            return -1;
        }
        if (con->session) session_command(con->session, CMD_THEME_BASE + match);
        else (void)desktop_theme_select(match);
        con_printf(vt, "Theme set to %s.\r\n", desktop_theme_name(desktop_theme_current()));
        return 0;
    }
    con_write(vt, "Usage: theme list | theme set <name|id>\r\n");
    return -1;
}

static int cmd_wallpaper(TermConsole *con, TermVt *vt, const char *arg) {
    arg = skip_ws(arg);
    int count = desktop_wallpaper_count();
    if (strcmp(arg, "list") == 0 || *arg == '\0') {
        int cur = desktop_wallpaper_current();
        con_printf(vt, "  %s [0] Plain Desktop\r\n", (cur < 0) ? "*" : " ");
        for (int i = 0; i < count; i++) {
            con_printf(vt, "  %s [%d] %s\r\n", (cur == i) ? "*" : " ", i + 1, desktop_wallpaper_name(i));
        }
        return 0;
    }
    if (strncmp(arg, "set", 3) == 0 && (arg[3] == '\0' || isspace((unsigned char)arg[3]))) {
        const char *q = skip_ws(arg + 3);
        if (!*q) {
            con_write(vt, "Usage: wallpaper set <id|plain>\r\n");
            return -1;
        }
        int target = -2;
        if (strcasecmp(q, "plain") == 0 || strcasecmp(q, "none") == 0 ||
            strcasecmp(q, "plain desktop") == 0 || strcmp(q, "0") == 0 || strcmp(q, "-1") == 0) {
            target = -1;
        } else {
            char *endp = NULL;
            long v = strtol(q, &endp, 10);
            if (endp && *endp == '\0' && v >= 1 && v <= count) target = (int)v - 1;
            for (int i = 0; target == -2 && i < count; i++) {
                if (strcasecmp(desktop_wallpaper_name(i), q) == 0 ||
                    str_icontains(desktop_wallpaper_name(i), q)) {
                    target = i;
                }
            }
        }
        if (target < -1 || target >= count) {
            con_printf(vt, "Unknown wallpaper: %s\r\n", q);
            return -1;
        }
        if (con->session) session_command(con->session, CMD_WALLPAPER_BASE + target + 1);
        else (void)desktop_wallpaper_select(target);
        if (target < 0) con_write(vt, "Wallpaper set to Plain Desktop.\r\n");
        else con_printf(vt, "Wallpaper set to %s.\r\n", desktop_wallpaper_name(target));
        return 0;
    }
    con_write(vt, "Usage: wallpaper list | wallpaper set <id|plain>\r\n");
    return -1;
}

static int cmd_sound(TermConsole *con, TermVt *vt, const char *arg) {
    arg = skip_ws(arg);
    if (strncmp(arg, "preview", 7) == 0 && (arg[7] == '\0' || isspace((unsigned char)arg[7]))) {
        const char *q = skip_ws(arg + 7);
        int id = -1;
        const char *label = NULL;
        if (strcasecmp(q, "startup") == 0 || strcasecmp(q, "chord") == 0 || strcmp(q, "0") == 0) {
            id = 0; label = "startup";
        } else if (strcasecmp(q, "alert") == 0 || strcasecmp(q, "beep") == 0 || strcmp(q, "1") == 0) {
            id = 1; label = "alert";
        } else if (strcasecmp(q, "menu_open") == 0 || strcasecmp(q, "menu") == 0 || strcmp(q, "2") == 0) {
            id = 2; label = "menu_open";
        } else if (strcasecmp(q, "menu_select") == 0 || strcasecmp(q, "select") == 0 || strcmp(q, "3") == 0) {
            id = 3; label = "menu_select";
        }
        if (id < 0) {
            con_printf(vt, "Unknown sound id '%s' (use startup, alert, menu_open, menu_select).\r\n", q);
            return -1;
        }
        if (con->session) con->session->sound_preview_request = id;
        con_printf(vt, "Sound preview queued: %s (%d)\r\n", label, id);
        return 0;
    }
    con_write(vt, "Usage: sound preview <startup|alert|menu_open|menu_select>\r\n");
    return -1;
}

static int open_in_folder_win(Session *s, const char *folder_rel, const char *item_name) {
    int win_idx = -1;
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_FOLDER && strcmp(s->win[i].rel, folder_rel) == 0) {
            win_idx = i;
            break;
        }
    }
    int opened_temp = 0;
    if (win_idx < 0) {
        if (open_folder(s, folder_rel) != 0) return -1;
        win_idx = s->nwin - 1;
        opened_temp = 1;
    }
    Window *w = &s->win[win_idx];
    for (int i = 0; i < w->nitems; i++) {
        if (strcasecmp(window_items(w)[i].name, item_name) == 0) {
            open_item(s, w, i);
            return 0;
        }
    }
    if (opened_temp) close_index(s, win_idx);
    return -1;
}

static int cmd_open(TermConsole *con, TermVt *vt, const char *arg) {
    Session *s = con->session;
    const char *q = skip_ws(arg);
    if (!*q) {
        con_write(vt, "Usage: open <application|folder|document>\r\n");
        return -1;
    }
    if (!s) return -1;
    if (strcasecmp(q, "terminal") == 0 || strcasecmp(q, "host shell") == 0 || strcasecmp(q, "shell") == 0) {
        if (terminal_open(s, TERM_MODE_SHELL) == 0) {
            con_write(vt, "Opened Terminal \xe2\x80\x94 Host Shell.\r\n");
            return 0;
        }
        return -1;
    }
    if (strcasecmp(q, "console") == 0 || strcasecmp(q, "os999 console") == 0) {
        if (terminal_open(s, TERM_MODE_CONSOLE) == 0) {
            con_write(vt, "Opened OS999 Console.\r\n");
            return 0;
        }
        return -1;
    }
    if (strcasecmp(q, "appearance") == 0 || strcasecmp(q, "themes") == 0) {
        session_command(s, CMD_APPEARANCE_THEMES);
        con_write(vt, "Opened Appearance control panel.\r\n");
        return 0;
    }
    if (strcasecmp(q, "control panels") == 0) {
        session_command(s, CMD_CONTROL_PANELS);
        con_write(vt, "Opened Control Panels.\r\n");
        return 0;
    }
    char full[OSPI_PATH];
    struct stat st;
    if (volume_join(full, sizeof(full), s->root, q) == 0 && stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (open_folder(s, q) == 0) {
            con_printf(vt, "Opened folder '%s'.\r\n", q);
            return 0;
        }
    }
    const char *slash = strrchr(q, '/');
    if (slash) {
        char dir[OSPI_PATH];
        size_t dlen = (size_t)(slash - q);
        if (dlen < sizeof(dir)) {
            memcpy(dir, q, dlen);
            dir[dlen] = '\0';
            if (open_in_folder_win(s, dir, slash + 1) == 0) {
                con_printf(vt, "Opened '%s'.\r\n", q);
                return 0;
            }
        }
    } else {
        const char *dirs[] = { "", "Applications", "Documents" };
        for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
            if (open_in_folder_win(s, dirs[d], q) == 0) {
                con_printf(vt, "Opened '%s'.\r\n", q);
                return 0;
            }
        }
    }
    con_printf(vt, "Could not find '%s' on volume.\r\n", q);
    return -1;
}

int term_console_exec(TermConsole *con, TermVt *vt, const char *cmd_line) {
    if (!con || !vt || !cmd_line) return -1;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", cmd_line);
    trim_trailing(buf);
    const char *p = skip_ws(buf);
    if (!*p) return 0;

    int rc = -1;
    if (!strcmp(p, "quit") || !strcmp(p, "exit")) { con->close_requested = 1; return 0; }
    if (strcmp(p, "help") == 0 || strcmp(p, "?") == 0) rc = cmd_help(vt);
    else if (strcmp(p, "about") == 0) rc = cmd_about(con, vt);
    else if (!strcmp(p, "shutdown") || !strcmp(p, "shut down")) rc = cmd_power(con, vt, 0);
    else if (!strcmp(p, "restart") || !strcmp(p, "reboot")) rc = cmd_power(con, vt, 1);
    else if (strcmp(p, "clear") == 0) {
        con_write(vt, "\x1b[2J\x1b[H");
        rc = 0;
    } else if (strncmp(p, "theme", 5) == 0 && (p[5] == '\0' || isspace((unsigned char)p[5]))) {
        rc = cmd_theme(con, vt, p + 5);
    } else if (strncmp(p, "wallpaper", 9) == 0 && (p[9] == '\0' || isspace((unsigned char)p[9]))) {
        rc = cmd_wallpaper(con, vt, p + 9);
    } else if (strncmp(p, "sound", 5) == 0 && (p[5] == '\0' || isspace((unsigned char)p[5]))) {
        rc = cmd_sound(con, vt, p + 5);
    } else if (strncmp(p, "open", 4) == 0 && (p[4] == '\0' || isspace((unsigned char)p[4]))) {
        rc = cmd_open(con, vt, p + 4);
    } else {
        con_printf(vt, "Unknown command: %s (type 'help' for commands)\r\n", p);
        rc = -1;
    }
    return rc;
}

static void replace_input_line(TermConsole *con, TermVt *vt, const char *new_line) {
    while (con->len > 0) {
        con_write(vt, "\b \b");
        con->len--;
    }
    snprintf(con->line, sizeof(con->line), "%s", new_line ? new_line : "");
    con->len = (int)strlen(con->line);
    con->cursor = con->len;
    con_write(vt, con->line);
}

void term_console_input(TermConsole *con, TermVt *vt, const char *data, size_t len) {
    if (!con || !vt || !data) return;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0x1b && i + 2 < len && data[i + 1] == '[') {
            char code = data[i + 2];
            if (code == 'A' && con->history_count > 0) {
                if (con->history_index > 0) con->history_index--;
                replace_input_line(con, vt, con->history[con->history_index]);
            } else if (code == 'B') {
                if (con->history_index < con->history_count - 1) {
                    con->history_index++;
                    replace_input_line(con, vt, con->history[con->history_index]);
                } else {
                    con->history_index = con->history_count;
                    replace_input_line(con, vt, "");
                }
            }
            i += 2;
            continue;
        }
        if (c == '\r' || c == '\n') {
            con_write(vt, "\r\n");
            con->line[con->len] = '\0';
            if (con->len > 0) {
                if (con->history_count < 16) {
                    snprintf(con->history[con->history_count++], sizeof(con->history[0]), "%s", con->line);
                } else {
                    memmove(con->history[0], con->history[1], 15 * sizeof(con->history[0]));
                    snprintf(con->history[15], sizeof(con->history[0]), "%s", con->line);
                }
                con->history_index = con->history_count;
                (void)term_console_exec(con, vt, con->line);
                if (con->close_requested) return;
            }
            con->len = 0;
            con->cursor = 0;
            con->line[0] = '\0';
            con_prompt(vt);
        } else if (c == 0x03) {
            con_write(vt, "^C\r\n");
            con->len = 0;
            con->cursor = 0;
            con->line[0] = '\0';
            con_prompt(vt);
        } else if (c == 0x7f || c == '\b') {
            if (con->len > 0) {
                con->len--;
                con->cursor = con->len;
                con->line[con->len] = '\0';
                con_write(vt, "\b \b");
            }
        } else if (c >= 32 && c < 127 && con->len < (int)sizeof(con->line) - 1) {
            con->line[con->len++] = (char)c;
            con->line[con->len] = '\0';
            con->cursor = con->len;
            char ch = (char)c;
            term_vt_feed(vt, &ch, 1);
        }
    }
}
