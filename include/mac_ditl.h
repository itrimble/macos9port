#ifndef MAC_DITL_H
#define MAC_DITL_H
#include <stddef.h>
#include <stdint.h>
/* Bounded parsers for the public DLOG (dialog template) and DITL (item list) resource layouts. Output items point into
 * the caller's resource bytes, which must outlive them. Nothing is executed or fetched; unknown item kinds fail. */
enum { MAC_DITL_MAX_ITEMS = 40 };
typedef enum {
    MAC_ITEM_USER = 0, MAC_ITEM_HELP = 1, MAC_ITEM_BUTTON = 4, MAC_ITEM_CHECK = 5, MAC_ITEM_RADIO = 6, MAC_ITEM_CONTROL = 7,
    MAC_ITEM_STATIC = 8, MAC_ITEM_EDIT = 16, MAC_ITEM_ICON = 32, MAC_ITEM_PICT = 64
} MacItemKind;
typedef struct {
    int16_t top, left, bottom, right;
    uint8_t kind;      /* MacItemKind, without the disabled bit */
    uint8_t disabled;  /* item does not respond to clicks */
    uint8_t length;    /* bytes at data (a help item carries 4 and is never drawn or hit): text for button/check/radio/static/edit, a 2-byte resource ID for icon/pict/control */
    const uint8_t *data;
} MacDitlItem;
typedef struct { unsigned count; MacDitlItem items[MAC_DITL_MAX_ITEMS]; } MacDitl;
typedef struct {
    int16_t top, left, bottom, right;
    int16_t proc_id;
    uint8_t visible, go_away;
    uint32_t ref_con;
    int16_t items_id;
    uint8_t title_length;
    const uint8_t *title;
} MacDlog;
/* Return 0 on success, -1 with *error set to a static string. The whole resource must be consumed exactly. */
int mac_ditl_parse(const uint8_t *data, size_t size, MacDitl *out, const char **error);
int mac_dlog_parse(const uint8_t *data, size_t size, MacDlog *out, const char **error);
#endif
