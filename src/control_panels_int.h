#ifndef OS999_CONTROL_PANELS_INT_H
#define OS999_CONTROL_PANELS_INT_H

#include "control_panels.h"

enum {
    CP_WIN_W = 620,
    CP_WIN_H = 440,
    CP_CAT_COUNT = 5,
    CP_CARD_W = 180,
    CP_CARD_H = 148,
    CP_CARD_DX = 188,
    CP_CARD_DY = 158
};

typedef struct {
    Session *owner;
    int open;
    int focused;
    int win_x, win_y, pos_valid;
    int dragging_win, drag_dx, drag_dy;
    int category;
    int selected_slot;
    int scroll;
    int open_module;
    int context_open;
    Rect context_box;
    int last_slot, last_x, last_y;
    uint32_t last_ms;
} ControlPanelsUI;

extern ControlPanelsUI g_cp_ui;

static inline int cp_clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static inline Rect cp_close_rect(Rect r) { return (Rect){r.x + 8, r.y + 4, 12, 12}; }
static inline Rect cp_cat_rect(Rect r, int cat) { return (Rect){r.x + 16 + cat * 94, r.y + 28, 90, 24}; }
static inline Rect cp_open_btn_rect(Rect r) { return (Rect){r.x + r.w - 108, r.y + 28, 92, 24}; }
static inline Rect cp_grid_rect(Rect r) { return (Rect){r.x + 16, r.y + 60, r.w - 32, r.h - 92}; }
static inline Rect cp_back_btn_rect(Rect r) { return (Rect){r.x + 16, r.y + 27, 112, 26}; }
static inline Rect cp_save_btn_rect(Rect r) { return (Rect){r.x + r.w - 88, r.y + 27, 72, 26}; }
static inline Rect cp_content_rect(Rect r) { return (Rect){r.x + 16, r.y + 60, r.w - 32, r.h - 76}; }

static inline Rect cp_card_rect(Rect grid, int slot, int scroll) {
    return (Rect){
        grid.x + 10 + (slot % 3) * CP_CARD_DX,
        grid.y + 12 + (slot / 3) * CP_CARD_DY - scroll,
        CP_CARD_W,
        CP_CARD_H
    };
}

Rect cp_window_rect(const Session *s);
int cp_filtered_modules(int out[OS999_CDEV_MAX_MODULES]);
int cp_max_grid_scroll(Rect grid, int nslots);
void cp_clamp_grid_state(const Session *s);
void cp_ensure_slot_visible(const Session *s, int slot);
void cp_save_active_module(Session *s);
void cp_open_selected_slot(Session *s);

#endif
