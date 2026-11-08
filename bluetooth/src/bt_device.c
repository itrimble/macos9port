/* Device database: what discovery finds and what the stack remembers about
 * each remote device.
 *
 * The table is bounded. When it is full the least recently seen device is
 * evicted, so a busy radio cannot make the desktop grow without limit. */
#include "bt_internal.h"

#include <stdlib.h>

struct BtDeviceDb {
    BtDeviceInfo items[BT_MAX_DEVICES];
    int n;
};

BtDeviceDb *bt_db_new(void) {
    return calloc(1, sizeof(BtDeviceDb));
}

void bt_db_free(BtDeviceDb *db) {
    free(db);
}

const BtDeviceInfo *bt_db_find(const BtDeviceDb *db, const BtAddress *addr) {
    if (!db || !addr) return NULL;
    for (int i = 0; i < db->n; i++)
        if (bt_addr_cmp(&db->items[i].addr, addr) == 0) return &db->items[i];
    return NULL;
}

int bt_db_count(const BtDeviceDb *db) {
    return db ? db->n : 0;
}

const BtDeviceInfo *bt_db_at(const BtDeviceDb *db, int index) {
    if (!db || index < 0 || index >= db->n) return NULL;
    return &db->items[index];
}

/* Insert or update by address. A record without a name keeps the name we
 * already had, because an inquiry result often arrives before the name. */
int bt_db_upsert(BtDeviceDb *db, const BtDeviceInfo *info) {
    if (!db || !info || bt_addr_is_zero(&info->addr)) return BT_EINVAL;
    BtDeviceInfo *slot = (BtDeviceInfo *)bt_db_find(db, &info->addr);
    if (!slot) {
        if (db->n >= BT_MAX_DEVICES) {
            int oldest = 0;
            for (int i = 1; i < db->n; i++)
                if (db->items[i].last_seen_ms < db->items[oldest].last_seen_ms) oldest = i;
            slot = &db->items[oldest];
        } else {
            slot = &db->items[db->n++];
            memset(slot, 0, sizeof *slot);
        }
        slot->addr = info->addr;
    }
    if (info->name[0]) bt_strlcpy(slot->name, info->name, sizeof slot->name);
    if (info->class_of_device) slot->class_of_device = info->class_of_device;
    if (info->rssi) slot->rssi = info->rssi;
    if (info->paired) slot->paired = 1;
    if (info->bonded) slot->bonded = 1;
    if (info->connected) slot->connected = 1;
    if (info->legacy) slot->legacy = 1;
    slot->last_seen_ms = info->last_seen_ms ? info->last_seen_ms : bt_now_ms();
    return BT_OK;
}

int bt_db_remove(BtDeviceDb *db, const BtAddress *addr) {
    if (!db || !addr) return BT_EINVAL;
    for (int i = 0; i < db->n; i++) {
        if (bt_addr_cmp(&db->items[i].addr, addr) != 0) continue;
        memmove(&db->items[i], &db->items[i + 1], (size_t)(db->n - i - 1) * sizeof db->items[0]);
        db->n--;
        memset(&db->items[db->n], 0, sizeof db->items[0]);
        return BT_OK;
    }
    return BT_ENOENT;
}
