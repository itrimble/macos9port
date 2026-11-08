#include "mac_ditl.h"
#include <string.h>

/* DITL: item count minus one (word), then per item: 4 reserved bytes, a rect (top, left, bottom, right), a type byte
 * (bit 7 = disabled), a length byte, and `length` data bytes padded to an even count.
 * DLOG: rect, procID, visible byte + pad, goAway byte + pad, refCon, items-resource ID, title Pascal string, and an
 * optional trailing positioning word (preceded by a pad byte when the title ends on an odd offset). Both layouts are the public Dialog Manager resource formats. */
static unsigned u16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }
static int bad(const char **error, const char *why) { if (error) *error = why; return -1; }

int mac_ditl_parse(const uint8_t *data, size_t size, MacDitl *out, const char **error) {
    memset(out, 0, sizeof *out);
    if (!data || size < 2) return bad(error, "DITL is truncated.");
    unsigned count = u16(data) + 1u;
    if (count > MAC_DITL_MAX_ITEMS) return bad(error, "DITL has more items than the bounded dialog supports.");
    size_t at = 2;
    for (unsigned i = 0; i < count; i++) {
        if (size - at < 14) return bad(error, "DITL item header is truncated.");
        const uint8_t *p = data + at;
        MacDitlItem *item = &out->items[i];
        item->top = (int16_t)u16(p + 4); item->left = (int16_t)u16(p + 6);
        item->bottom = (int16_t)u16(p + 8); item->right = (int16_t)u16(p + 10);
        item->disabled = (p[12] & 0x80) != 0;
        item->kind = p[12] & 0x7f;
        item->length = p[13];
        switch (item->kind) {
        case MAC_ITEM_USER: case MAC_ITEM_BUTTON: case MAC_ITEM_CHECK: case MAC_ITEM_RADIO: case MAC_ITEM_STATIC:
        case MAC_ITEM_EDIT: break;
        case MAC_ITEM_CONTROL: case MAC_ITEM_ICON: case MAC_ITEM_PICT:
            if (item->length != 2) return bad(error, "DITL resource item must carry a 2-byte resource ID.");
            break;
        case MAC_ITEM_HELP:
            if (item->length != 4) return bad(error, "DITL help item must carry 4 bytes.");
            break;
        default: return bad(error, "DITL has an unknown item kind.");
        }
        size_t padded = (size_t)item->length + (item->length & 1);
        if (size - at - 14 < padded) return bad(error, "DITL item data is truncated.");
        item->data = p + 14;
        at += 14 + padded;
        out->count = i + 1;
    }
    if (at != size) return bad(error, "DITL has trailing bytes.");
    return 0;
}

int mac_dlog_parse(const uint8_t *data, size_t size, MacDlog *out, const char **error) {
    memset(out, 0, sizeof *out);
    if (!data || size < 21) return bad(error, "DLOG is truncated.");
    out->top = (int16_t)u16(data); out->left = (int16_t)u16(data + 2);
    out->bottom = (int16_t)u16(data + 4); out->right = (int16_t)u16(data + 6);
    out->proc_id = (int16_t)u16(data + 8);
    out->visible = data[10]; out->go_away = data[12];
    out->ref_con = (uint32_t)u16(data + 14) << 16 | u16(data + 16);
    out->items_id = (int16_t)u16(data + 18);
    out->title_length = data[20];
    if (size - 21 < out->title_length) return bad(error, "DLOG title is truncated.");
    out->title = data + 21;
    size_t end = 21u + out->title_length, pad = end & 1, tail = size - end;
    if (tail != 0 && tail != pad + 2) return bad(error, "DLOG has trailing bytes.");
    if (out->bottom <= out->top || out->right <= out->left) return bad(error, "DLOG has an empty or inverted rectangle.");
    return 0;
}
