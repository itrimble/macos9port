#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VT_GROUND = 0, VT_ESC = 1, VT_ESC_INT = 2, VT_CSI = 3, VT_OSC = 4, VT_OSC_ESC = 5 };

static const uint32_t k_ansi_light[16] = {
    0xFF181A1Fu, 0xFFB8261Eu, 0xFF1B7A34u, 0xFF9A6700u, 0xFF1D4ED8u, 0xFF7E22CEu, 0xFF0E7490u, 0xFF6B7280u,
    0xFF4B5563u, 0xFFDC2626u, 0xFF16A34Au, 0xFFCA8A04u, 0xFF2563EBu, 0xFF9333EAu, 0xFF0891B2u, 0xFF111827u
};

static const uint32_t k_ansi_dark[16] = {
    0xFF1F242Du, 0xFFF87171u, 0xFF4ADE80u, 0xFFFACC15u, 0xFF60A5FAu, 0xFFC084FCu, 0xFF22D3EEu, 0xFFD1D5DBu,
    0xFF6B7280u, 0xFFEF4444u, 0xFF22C55Eu, 0xFFEAB308u, 0xFF3B82F6u, 0xFFA855F7u, 0xFF06B6D4u, 0xFFF9FAFBu
};

uint32_t term_default_fg(int theme) { return theme == TERM_THEME_DARK ? 0xFFE6E8EEu : 0xFF181A1Fu; }
uint32_t term_default_bg(int theme) { return theme == TERM_THEME_DARK ? 0xFF141414u : 0xFFF7F6F2u; }

uint32_t term_ansi_color(int index, int theme) {
    const uint32_t *pal = (theme == TERM_THEME_DARK) ? k_ansi_dark : k_ansi_light;
    if (index < 0) index = 0;
    if (index < 16) return pal[index];
    if (index < 232) {
        int v = index - 16, r = (v / 36) % 6, g = (v / 6) % 6, b = v % 6;
        uint32_t rv = r ? (uint32_t)(55 + r * 40) : 0;
        uint32_t gv = g ? (uint32_t)(55 + g * 40) : 0;
        uint32_t bv = b ? (uint32_t)(55 + b * 40) : 0;
        return 0xFF000000u | (rv << 16) | (gv << 8) | bv;
    }
    uint32_t gray = (uint32_t)(8 + ((index - 232) & 0x1F) * 10);
    if (gray > 255) gray = 255;
    return 0xFF000000u | (gray << 16) | (gray << 8) | gray;
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static TermLine *active_screen(TermVt *vt) { return vt->alt_screen ? vt->alt : vt->primary; }
static const TermLine *active_screen_const(const TermVt *vt) { return vt->alt_screen ? vt->alt : vt->primary; }

static void clear_cell(const TermVt *vt, TermCell *c) {
    c->cp = ' '; c->fg = vt->cur_fg; c->bg = vt->cur_bg; c->attrs = 0;
    c->fg_default = vt->cur_fg_default; c->bg_default = vt->cur_bg_default;
}

static void clear_line_range(TermVt *vt, TermLine *line, int c0, int c1) {
    c0 = clamp_int(c0, 0, TERM_MAX_COLS); c1 = clamp_int(c1, 0, TERM_MAX_COLS);
    for (int c = c0; c < c1; c++) clear_cell(vt, &line->cells[c]);
    if (c0 == 0 && c1 >= vt->cols) { line->len = 0; line->wrapped = 0; }
}

static void push_scrollback(TermVt *vt, const TermLine *line) {
    if (!vt->sb_lines || vt->sb_cap <= 0 || vt->alt_screen) return;
    int dst;
    if (vt->sb_count < vt->sb_cap) { dst = (vt->sb_head + vt->sb_count) % vt->sb_cap; vt->sb_count++; }
    else { dst = vt->sb_head; vt->sb_head = (vt->sb_head + 1) % vt->sb_cap; }
    vt->sb_lines[dst] = *line;
}

static void scroll_up(TermVt *vt, int top, int bot, int n, int save_sb) {
    TermLine *scr = active_screen(vt);
    top = clamp_int(top, 0, vt->rows - 1); bot = clamp_int(bot, top, vt->rows - 1);
    int span = bot - top + 1;
    if (n <= 0) return;
    if (n > span) n = span;
    for (int i = 0; i < n; i++) if (save_sb && top == 0) push_scrollback(vt, &scr[top + i]);
    for (int r = top; r <= bot - n; r++) scr[r] = scr[r + n];
    for (int r = bot - n + 1; r <= bot; r++) clear_line_range(vt, &scr[r], 0, TERM_MAX_COLS);
}

static void scroll_down(TermVt *vt, int top, int bot, int n) {
    TermLine *scr = active_screen(vt);
    top = clamp_int(top, 0, vt->rows - 1); bot = clamp_int(bot, top, vt->rows - 1);
    int span = bot - top + 1;
    if (n <= 0) return;
    if (n > span) n = span;
    for (int r = bot; r >= top + n; r--) scr[r] = scr[r - n];
    for (int r = top; r < top + n; r++) clear_line_range(vt, &scr[r], 0, TERM_MAX_COLS);
}

void term_vt_reset(TermVt *vt) {
    vt->cursor_row = vt->cursor_col = vt->saved_row = vt->saved_col = vt->alt_saved_row = vt->alt_saved_col = 0;
    vt->scroll_top = 0; vt->scroll_bottom = vt->rows - 1;
    vt->cursor_visible = 1; vt->app_cursor = vt->alt_screen = vt->bracketed_paste = vt->pending_wrap = 0;
    vt->auto_wrap = 1;
    vt->cur_fg = vt->saved_fg = term_default_fg(vt->theme);
    vt->cur_bg = vt->saved_bg = term_default_bg(vt->theme);
    vt->cur_attrs = vt->saved_attrs = 0;
    vt->cur_fg_default = vt->saved_fg_default = vt->cur_bg_default = vt->saved_bg_default = 1;
    vt->state = VT_GROUND; vt->utf8_cp = 0; vt->utf8_rem = 0; vt->reply_len = 0; vt->title[0] = '\0';
    for (int r = 0; r < TERM_MAX_ROWS; r++) {
        clear_line_range(vt, &vt->primary[r], 0, TERM_MAX_COLS);
        clear_line_range(vt, &vt->alt[r], 0, TERM_MAX_COLS);
    }
}

void term_vt_init(TermVt *vt, int rows, int cols) {
    if (!vt) return;
    memset(vt, 0, sizeof(*vt));
    vt->rows = clamp_int(rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
    vt->cols = clamp_int(cols, TERM_MIN_COLS, TERM_MAX_COLS);
    vt->theme = TERM_THEME_DARK;
    vt->sb_cap = TERM_SCROLLBACK_MAX;
    vt->sb_lines = (TermLine *)calloc((size_t)vt->sb_cap, sizeof(TermLine));
    term_vt_reset(vt);
}

void term_vt_free(TermVt *vt) {
    if (!vt) return;
    free(vt->sb_lines); vt->sb_lines = NULL; vt->sb_cap = vt->sb_count = vt->sb_head = 0;
}

void term_vt_set_theme(TermVt *vt, int theme) {
    if (!vt) return;
    vt->theme = (theme == TERM_THEME_DARK) ? TERM_THEME_DARK : TERM_THEME_LIGHT;
    if (vt->cur_fg_default) vt->cur_fg = term_default_fg(vt->theme);
    if (vt->cur_bg_default) vt->cur_bg = term_default_bg(vt->theme);
}

void term_vt_clear_scrollback(TermVt *vt) { if (vt) vt->sb_head = vt->sb_count = 0; }

static void linefeed(TermVt *vt) {
    vt->pending_wrap = 0;
    if (vt->cursor_row == vt->scroll_bottom) scroll_up(vt, vt->scroll_top, vt->scroll_bottom, 1, 1);
    else if (vt->cursor_row < vt->rows - 1) vt->cursor_row++;
}

static void put_codepoint(TermVt *vt, uint32_t cp) {
    TermLine *scr = active_screen(vt);
    if (vt->pending_wrap && vt->auto_wrap) {
        scr[vt->cursor_row].wrapped = 1; vt->cursor_col = 0; linefeed(vt);
    }
    int r = clamp_int(vt->cursor_row, 0, vt->rows - 1);
    int c = clamp_int(vt->cursor_col, 0, vt->cols - 1);
    TermCell *cell = &scr[r].cells[c];
    cell->cp = cp; cell->fg = vt->cur_fg; cell->bg = vt->cur_bg; cell->attrs = vt->cur_attrs;
    cell->fg_default = vt->cur_fg_default; cell->bg_default = vt->cur_bg_default;
    if (c + 1 > scr[r].len) scr[r].len = c + 1;
    if (c < vt->cols - 1) { vt->cursor_col = c + 1; vt->pending_wrap = 0; }
    else vt->pending_wrap = vt->auto_wrap ? 1 : 0;
}

static int csi_param(const TermVt *vt, int idx, int defval) {
    if (idx < 0 || idx >= vt->csi_nparams) return defval;
    return vt->csi_params[idx] > 0 ? vt->csi_params[idx] : defval;
}

static void reset_sgr(TermVt *vt) {
    vt->cur_attrs = 0;
    vt->cur_fg = term_default_fg(vt->theme); vt->cur_bg = term_default_bg(vt->theme);
    vt->cur_fg_default = vt->cur_bg_default = 1;
}

static void handle_sgr(TermVt *vt) {
    if (vt->csi_nparams == 0) { reset_sgr(vt); return; }
    for (int i = 0; i < vt->csi_nparams; i++) {
        int p = vt->csi_params[i];
        if (p == 0) reset_sgr(vt);
        else if (p == 1) {
            vt->cur_attrs |= TERM_ATTR_BOLD;
            if (!vt->cur_fg_default) {
                for (int k = 0; k < 8; k++)
                    if (vt->cur_fg == term_ansi_color(k, vt->theme)) { vt->cur_fg = term_ansi_color(k + 8, vt->theme); break; }
            }
        } else if (p == 2) vt->cur_attrs |= TERM_ATTR_DIM;
        else if (p == 3) vt->cur_attrs |= TERM_ATTR_ITALIC;
        else if (p == 4) vt->cur_attrs |= TERM_ATTR_UNDERLINE;
        else if (p == 7) vt->cur_attrs |= TERM_ATTR_REVERSE;
        else if (p == 22) vt->cur_attrs &= (uint8_t)~(TERM_ATTR_BOLD | TERM_ATTR_DIM);
        else if (p == 23) vt->cur_attrs &= (uint8_t)~TERM_ATTR_ITALIC;
        else if (p == 24) vt->cur_attrs &= (uint8_t)~TERM_ATTR_UNDERLINE;
        else if (p == 27) vt->cur_attrs &= (uint8_t)~TERM_ATTR_REVERSE;
        else if (p >= 30 && p <= 37) {
            int idx = (p - 30) + ((vt->cur_attrs & TERM_ATTR_BOLD) ? 8 : 0);
            vt->cur_fg = term_ansi_color(idx, vt->theme); vt->cur_fg_default = 0;
        } else if (p == 39) { vt->cur_fg = term_default_fg(vt->theme); vt->cur_fg_default = 1; }
        else if (p >= 40 && p <= 47) { vt->cur_bg = term_ansi_color(p - 40, vt->theme); vt->cur_bg_default = 0; }
        else if (p == 49) { vt->cur_bg = term_default_bg(vt->theme); vt->cur_bg_default = 1; }
        else if (p >= 90 && p <= 97) { vt->cur_fg = term_ansi_color(p - 90 + 8, vt->theme); vt->cur_fg_default = 0; }
        else if (p >= 100 && p <= 107) { vt->cur_bg = term_ansi_color(p - 100 + 8, vt->theme); vt->cur_bg_default = 0; }
        else if (p == 38 || p == 48) {
            int is_fg = (p == 38);
            if (i + 2 < vt->csi_nparams && vt->csi_params[i + 1] == 5) {
                uint32_t col = term_ansi_color(vt->csi_params[i + 2], vt->theme);
                if (is_fg) { vt->cur_fg = col; vt->cur_fg_default = 0; } else { vt->cur_bg = col; vt->cur_bg_default = 0; }
                i += 2;
            } else if (i + 4 < vt->csi_nparams && vt->csi_params[i + 1] == 2) {
                uint32_t r = (uint32_t)clamp_int(vt->csi_params[i + 2], 0, 255);
                uint32_t g = (uint32_t)clamp_int(vt->csi_params[i + 3], 0, 255);
                uint32_t b = (uint32_t)clamp_int(vt->csi_params[i + 4], 0, 255);
                uint32_t col = 0xFF000000u | (r << 16) | (g << 8) | b;
                if (is_fg) { vt->cur_fg = col; vt->cur_fg_default = 0; } else { vt->cur_bg = col; vt->cur_bg_default = 0; }
                i += 4;
            }
        }
    }
}

static void handle_dec_mode(TermVt *vt, int set) {
    for (int i = 0; i < vt->csi_nparams; i++) {
        int p = vt->csi_params[i];
        if (p == 1) vt->app_cursor = set;
        else if (p == 7) vt->auto_wrap = set;
        else if (p == 25) vt->cursor_visible = set;
        else if (p == 2004) vt->bracketed_paste = set;
        else if (p == 47 || p == 1047 || p == 1049) {
            if (set && !vt->alt_screen) {
                vt->alt_saved_row = vt->cursor_row; vt->alt_saved_col = vt->cursor_col; vt->alt_screen = 1;
                for (int r = 0; r < TERM_MAX_ROWS; r++) clear_line_range(vt, &vt->alt[r], 0, TERM_MAX_COLS);
                vt->cursor_row = vt->cursor_col = vt->pending_wrap = 0;
            } else if (!set && vt->alt_screen) {
                vt->alt_screen = 0;
                vt->cursor_row = clamp_int(vt->alt_saved_row, 0, vt->rows - 1);
                vt->cursor_col = clamp_int(vt->alt_saved_col, 0, vt->cols - 1);
                vt->pending_wrap = 0;
            }
        }
    }
}

static void dispatch_csi(TermVt *vt, char final_ch) {
    TermLine *scr = active_screen(vt);
    if ((vt->csi_has_digit || vt->csi_nparams > 0) && vt->csi_nparams < TERM_CSI_MAX_PARAMS) vt->csi_nparams++;
    if (vt->csi_priv == '?') {
        if (final_ch == 'h') handle_dec_mode(vt, 1);
        else if (final_ch == 'l') handle_dec_mode(vt, 0);
        return;
    }
    int p0 = csi_param(vt, 0, 1), p1 = csi_param(vt, 1, 1);
    vt->pending_wrap = 0;
    switch (final_ch) {
    case 'A': vt->cursor_row = clamp_int(vt->cursor_row - p0, 0, vt->rows - 1); break;
    case 'B': vt->cursor_row = clamp_int(vt->cursor_row + p0, 0, vt->rows - 1); break;
    case 'C': vt->cursor_col = clamp_int(vt->cursor_col + p0, 0, vt->cols - 1); break;
    case 'D': vt->cursor_col = clamp_int(vt->cursor_col - p0, 0, vt->cols - 1); break;
    case 'E': vt->cursor_row = clamp_int(vt->cursor_row + p0, 0, vt->rows - 1); vt->cursor_col = 0; break;
    case 'F': vt->cursor_row = clamp_int(vt->cursor_row - p0, 0, vt->rows - 1); vt->cursor_col = 0; break;
    case 'G': vt->cursor_col = clamp_int(p0 - 1, 0, vt->cols - 1); break;
    case 'd': vt->cursor_row = clamp_int(p0 - 1, 0, vt->rows - 1); break;
    case 'H': case 'f':
        vt->cursor_row = clamp_int(p0 - 1, 0, vt->rows - 1);
        vt->cursor_col = clamp_int(p1 - 1, 0, vt->cols - 1);
        break;
    case 'J': {
        int m = csi_param(vt, 0, 0);
        if (m == 0) {
            clear_line_range(vt, &scr[vt->cursor_row], vt->cursor_col, TERM_MAX_COLS);
            for (int r = vt->cursor_row + 1; r < vt->rows; r++) clear_line_range(vt, &scr[r], 0, TERM_MAX_COLS);
        } else if (m == 1) {
            for (int r = 0; r < vt->cursor_row; r++) clear_line_range(vt, &scr[r], 0, TERM_MAX_COLS);
            clear_line_range(vt, &scr[vt->cursor_row], 0, vt->cursor_col + 1);
        } else if (m == 2 || m == 3) {
            for (int r = 0; r < vt->rows; r++) clear_line_range(vt, &scr[r], 0, TERM_MAX_COLS);
            if (m == 3) term_vt_clear_scrollback(vt);
        }
        break;
    }
    case 'K': {
        int m = csi_param(vt, 0, 0);
        if (m == 0) clear_line_range(vt, &scr[vt->cursor_row], vt->cursor_col, TERM_MAX_COLS);
        else if (m == 1) clear_line_range(vt, &scr[vt->cursor_row], 0, vt->cursor_col + 1);
        else if (m == 2) clear_line_range(vt, &scr[vt->cursor_row], 0, TERM_MAX_COLS);
        break;
    }
    case 'L': scroll_down(vt, vt->cursor_row, vt->scroll_bottom, p0); break;
    case 'M': scroll_up(vt, vt->cursor_row, vt->scroll_bottom, p0, 0); break;
    case 'P': {
        TermLine *ln = &scr[vt->cursor_row]; int n = clamp_int(p0, 1, vt->cols - vt->cursor_col);
        for (int c = vt->cursor_col; c < vt->cols - n; c++) ln->cells[c] = ln->cells[c + n];
        clear_line_range(vt, ln, vt->cols - n, vt->cols);
        break;
    }
    case '@': {
        TermLine *ln = &scr[vt->cursor_row]; int n = clamp_int(p0, 1, vt->cols - vt->cursor_col);
        for (int c = vt->cols - 1; c >= vt->cursor_col + n; c--) ln->cells[c] = ln->cells[c - n];
        clear_line_range(vt, ln, vt->cursor_col, vt->cursor_col + n);
        break;
    }
    case 'X': clear_line_range(vt, &scr[vt->cursor_row], vt->cursor_col, vt->cursor_col + p0); break;
    case 'S': scroll_up(vt, vt->scroll_top, vt->scroll_bottom, p0, 1); break;
    case 'T': scroll_down(vt, vt->scroll_top, vt->scroll_bottom, p0); break;
    case 'r': {
        int top = clamp_int(csi_param(vt, 0, 1) - 1, 0, vt->rows - 1);
        int bot = clamp_int(csi_param(vt, 1, vt->rows) - 1, top, vt->rows - 1);
        vt->scroll_top = top; vt->scroll_bottom = bot; vt->cursor_row = vt->cursor_col = 0;
        break;
    }
    case 'm': handle_sgr(vt); break;
    case 's': vt->saved_row = vt->cursor_row; vt->saved_col = vt->cursor_col; break;
    case 'u':
        vt->cursor_row = clamp_int(vt->saved_row, 0, vt->rows - 1);
        vt->cursor_col = clamp_int(vt->saved_col, 0, vt->cols - 1);
        break;
    case 'n':
        if (p0 == 6) {
            int n = snprintf(vt->reply + vt->reply_len, sizeof(vt->reply) - vt->reply_len,
                             "\x1b[%d;%dR", vt->cursor_row + 1, vt->cursor_col + 1);
            if (n > 0 && vt->reply_len + (size_t)n < sizeof(vt->reply)) vt->reply_len += (size_t)n;
        }
        break;
    default: break;
    }
}

static void finish_osc(TermVt *vt) {
    vt->osc_buf[vt->osc_len] = '\0';
    if (strncmp(vt->osc_buf, "0;", 2) == 0 || strncmp(vt->osc_buf, "2;", 2) == 0)
        snprintf(vt->title, sizeof(vt->title), "%s", vt->osc_buf + 2);
    vt->osc_len = 0;
}

void term_vt_feed(TermVt *vt, const char *data, size_t len) {
    if (!vt || !data) return;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = (uint8_t)data[i];
        if (vt->state == VT_OSC) {
            if (b == 0x07) { finish_osc(vt); vt->state = VT_GROUND; }
            else if (b == 0x1b) vt->state = VT_OSC_ESC;
            else if (vt->osc_len < TERM_OSC_MAX - 1) vt->osc_buf[vt->osc_len++] = (char)b;
            continue;
        }
        if (vt->state == VT_OSC_ESC) { if (b == '\\') finish_osc(vt); vt->state = VT_GROUND; continue; }
        if (b == 0x1b) { vt->state = VT_ESC; vt->utf8_rem = 0; continue; }
        if (vt->state == VT_ESC) {
            if (b == '[') {
                vt->state = VT_CSI; vt->csi_priv = vt->csi_interm = vt->csi_nparams = vt->csi_has_digit = 0;
                memset(vt->csi_params, 0, sizeof(vt->csi_params));
            } else if (b == ']') { vt->state = VT_OSC; vt->osc_len = 0; }
            else if (b == '(' || b == ')' || b == '#' || b == '%') vt->state = VT_ESC_INT;
            else {
                if (b == 'c') term_vt_reset(vt);
                else if (b == 'D') linefeed(vt);
                else if (b == 'E') { vt->cursor_col = 0; linefeed(vt); }
                else if (b == 'M') {
                    vt->pending_wrap = 0;
                    if (vt->cursor_row == vt->scroll_top) scroll_down(vt, vt->scroll_top, vt->scroll_bottom, 1);
                    else if (vt->cursor_row > 0) vt->cursor_row--;
                } else if (b == '7') {
                    vt->saved_row = vt->cursor_row; vt->saved_col = vt->cursor_col;
                    vt->saved_fg = vt->cur_fg; vt->saved_bg = vt->cur_bg; vt->saved_attrs = vt->cur_attrs;
                    vt->saved_fg_default = vt->cur_fg_default; vt->saved_bg_default = vt->cur_bg_default;
                } else if (b == '8') {
                    vt->cursor_row = clamp_int(vt->saved_row, 0, vt->rows - 1);
                    vt->cursor_col = clamp_int(vt->saved_col, 0, vt->cols - 1);
                    vt->cur_fg = vt->saved_fg; vt->cur_bg = vt->saved_bg; vt->cur_attrs = vt->saved_attrs;
                    vt->cur_fg_default = vt->saved_fg_default; vt->cur_bg_default = vt->saved_bg_default;
                }
                vt->state = VT_GROUND;
            }
            continue;
        }
        if (vt->state == VT_ESC_INT) { vt->state = VT_GROUND; continue; }
        if (vt->state == VT_CSI) {
            if (b >= '0' && b <= '9') {
                int idx = vt->csi_nparams < TERM_CSI_MAX_PARAMS ? vt->csi_nparams : TERM_CSI_MAX_PARAMS - 1;
                vt->csi_params[idx] = vt->csi_params[idx] * 10 + (b - '0'); vt->csi_has_digit = 1;
            } else if (b == ';' || b == ':') {
                if (vt->csi_nparams < TERM_CSI_MAX_PARAMS - 1) vt->csi_nparams++;
                vt->csi_has_digit = 0;
            } else if (b == '?' || b == '>' || b == '=') vt->csi_priv = b;
            else if (b >= 0x20 && b <= 0x2F) vt->csi_interm = b;
            else if (b >= 0x40 && b <= 0x7E) { dispatch_csi(vt, (char)b); vt->state = VT_GROUND; }
            continue;
        }
        if (b == '\r') { vt->cursor_col = 0; vt->pending_wrap = 0; }
        else if (b == '\n' || b == '\v' || b == '\f') linefeed(vt);
        else if (b == '\b') { vt->pending_wrap = 0; if (vt->cursor_col > 0) vt->cursor_col--; }
        else if (b == '\t') { vt->pending_wrap = 0; vt->cursor_col = clamp_int((vt->cursor_col + 8) & ~7, 0, vt->cols - 1); }
        else if (b < 0x20 || b == 0x7F) continue;
        else if (vt->utf8_rem > 0) {
            if ((b & 0xC0) == 0x80) {
                vt->utf8_cp = (vt->utf8_cp << 6) | (b & 0x3F);
                if (--vt->utf8_rem == 0) put_codepoint(vt, vt->utf8_cp);
            } else { vt->utf8_rem = 0; if (b < 0x80) put_codepoint(vt, b); }
        } else if (b < 0x80) put_codepoint(vt, b);
        else if ((b & 0xE0) == 0xC0) { vt->utf8_cp = b & 0x1F; vt->utf8_rem = 1; }
        else if ((b & 0xF0) == 0xE0) { vt->utf8_cp = b & 0x0F; vt->utf8_rem = 2; }
        else if ((b & 0xF8) == 0xF0) { vt->utf8_cp = b & 0x07; vt->utf8_rem = 3; }
    }
}

void term_vt_resize(TermVt *vt, int new_rows, int new_cols) {
    if (!vt) return;
    vt->rows = clamp_int(new_rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
    vt->cols = clamp_int(new_cols, TERM_MIN_COLS, TERM_MAX_COLS);
    vt->scroll_top = 0; vt->scroll_bottom = vt->rows - 1;
    vt->cursor_row = clamp_int(vt->cursor_row, 0, vt->rows - 1);
    vt->cursor_col = clamp_int(vt->cursor_col, 0, vt->cols - 1);
    vt->pending_wrap = 0;
}

const TermCell *term_vt_cell(const TermVt *vt, int row, int col) {
    if (!vt || row < 0 || row >= vt->rows || col < 0 || col >= vt->cols) return NULL;
    return &active_screen_const(vt)[row].cells[col];
}

const TermCell *term_vt_view_cell(const TermVt *vt, int scroll_offset, int row, int col) {
    if (!vt || row < 0 || row >= vt->rows || col < 0 || col >= vt->cols) return NULL;
    if (vt->alt_screen || scroll_offset <= 0) return term_vt_cell(vt, row, col);
    int off = clamp_int(scroll_offset, 0, vt->sb_count);
    int logical = vt->sb_count - off + row;
    if (logical < vt->sb_count) {
        int idx = (vt->sb_head + logical) % vt->sb_cap;
        return &vt->sb_lines[idx].cells[col];
    }
    int scr_row = logical - vt->sb_count;
    return (scr_row >= 0 && scr_row < vt->rows) ? &vt->primary[scr_row].cells[col] : NULL;
}

static int line_to_utf8(const TermLine *ln, int max_cols, char *out, size_t out_n) {
    if (!ln || !out || out_n == 0) return -1;
    int end = max_cols;
    while (end > 0 && (ln->cells[end - 1].cp == ' ' || ln->cells[end - 1].cp == 0)) end--;
    size_t pos = 0;
    for (int c = 0; c < end; c++) {
        uint32_t cp = ln->cells[c].cp ? ln->cells[c].cp : ' ';
        char u[4]; int n = 0;
        if (cp < 0x80) { u[0] = (char)cp; n = 1; }
        else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
        else if (cp < 0x10000) {
            u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
        } else {
            u[0] = (char)(0xF0 | (cp >> 18)); u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            u[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
        }
        if (pos + (size_t)n >= out_n) break;
        memcpy(out + pos, u, (size_t)n); pos += (size_t)n;
    }
    out[pos] = '\0';
    return 0;
}

int term_vt_row_text(const TermVt *vt, int row, char *out, size_t out_n) {
    if (!vt || row < 0 || row >= vt->rows) return -1;
    return line_to_utf8(&active_screen_const(vt)[row], vt->cols, out, out_n);
}

int term_vt_scrollback_row_text(const TermVt *vt, int sb_idx, char *out, size_t out_n) {
    if (!vt || !vt->sb_lines || sb_idx < 0 || sb_idx >= vt->sb_count) return -1;
    return line_to_utf8(&vt->sb_lines[(vt->sb_head + sb_idx) % vt->sb_cap], vt->cols, out, out_n);
}

size_t term_vt_take_reply(TermVt *vt, char *out, size_t out_n) {
    if (!vt || vt->reply_len == 0 || !out || out_n == 0) return 0;
    size_t n = vt->reply_len < out_n ? vt->reply_len : out_n;
    memcpy(out, vt->reply, n); vt->reply_len = 0;
    return n;
}

int term_vt_encode_key(const TermVt *vt, int key, int mods, char *out, size_t out_n) {
    if (!vt || !out || out_n < 8) return 0;
    const char *seq = NULL; char pfx = vt->app_cursor ? 'O' : '[';
    if (key == TERM_KEY_UP) { out[0] = '\x1b'; out[1] = pfx; out[2] = 'A'; return 3; }
    if (key == TERM_KEY_DOWN) { out[0] = '\x1b'; out[1] = pfx; out[2] = 'B'; return 3; }
    if (key == TERM_KEY_RIGHT) { out[0] = '\x1b'; out[1] = pfx; out[2] = 'C'; return 3; }
    if (key == TERM_KEY_LEFT) { out[0] = '\x1b'; out[1] = pfx; out[2] = 'D'; return 3; }
    if (key == TERM_KEY_HOME) { out[0] = '\x1b'; out[1] = pfx; out[2] = 'H'; return 3; }
    if (key == TERM_KEY_END) { out[0] = '\x1b'; out[1] = pfx; out[2] = 'F'; return 3; }
    if (key == TERM_KEY_PGUP) seq = "\x1b[5~";
    else if (key == TERM_KEY_PGDN) seq = "\x1b[6~";
    else if (key == TERM_KEY_DELETE) seq = "\x1b[3~";
    if (seq) { size_t n = strlen(seq); memcpy(out, seq, n); return (int)n; }
    if ((mods & TERM_MOD_CTRL) && key >= 'a' && key <= 'z') { out[0] = (char)(key - 'a' + 1); return 1; }
    if ((mods & TERM_MOD_CTRL) && key >= 'A' && key <= 'Z') { out[0] = (char)(key - 'A' + 1); return 1; }
    if (key == '\r' || key == '\n') { out[0] = '\r'; return 1; }
    if (key == '\b' || key == 127) { out[0] = 0x7F; return 1; }
    if (key == '\t' || key == 27) { out[0] = (char)key; return 1; }
    if (key >= 32 && key < 127) {
        if (mods & TERM_MOD_ALT) { out[0] = '\x1b'; out[1] = (char)key; return 2; }
        out[0] = (char)key; return 1;
    }
    return 0;
}
