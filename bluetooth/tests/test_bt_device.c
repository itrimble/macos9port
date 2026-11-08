/* Address handling and the device database. */
#include "bt_test.h"

static void test_address_parse(void) {
    BtAddress a;
    CHECK(bt_addr_parse("00:11:22:33:44:55", &a) == BT_OK);
    CHECK(a.b[0] == 0x00 && a.b[5] == 0x55);
    CHECK(bt_addr_parse("AA-bb-CC-dd-EE-ff", &a) == BT_OK);
    CHECK(a.b[0] == 0xAA && a.b[5] == 0xFF);
    CHECK(bt_addr_parse("AABBCCDDEEFF", &a) == BT_EINVAL);
    CHECK(bt_addr_parse("00:11:22:33:44", &a) == BT_EINVAL);
    CHECK(bt_addr_parse("00:11:22:33:44:55:66", &a) == BT_EINVAL);
    CHECK(bt_addr_parse("0g:11:22:33:44:55", &a) == BT_EINVAL);
    CHECK(bt_addr_parse("00:11:22:33:44:5", &a) == BT_EINVAL);
    CHECK(bt_addr_parse(NULL, &a) == BT_EINVAL);
    CHECK(bt_addr_parse("00:11:22:33:44:55", NULL) == BT_EINVAL);
}

static void test_address_format(void) {
    BtAddress a;
    CHECK(bt_addr_parse("0A:0B:0C:0D:0E:0F", &a) == BT_OK);
    char buf[32];
    CHECK(bt_addr_format(&a, buf, sizeof buf) == BT_OK);
    CHECK(strcmp(buf, "0A:0B:0C:0D:0E:0F") == 0);
    CHECK(bt_addr_format(&a, buf, 4) == BT_EINVAL);

    BtAddress b;
    bt_addr_parse("0A:0B:0C:0D:0E:0F", &b);
    CHECK(bt_addr_cmp(&a, &b) == 0);
    b.b[5] = 0x10;
    CHECK(bt_addr_cmp(&a, &b) != 0);

    BtAddress zero;
    memset(&zero, 0, sizeof zero);
    CHECK(bt_addr_is_zero(&zero));
    CHECK(!bt_addr_is_zero(&a));
}

static void test_database(void) {
    BtDeviceDb *db = bt_db_new();
    CHECK(db != NULL);
    CHECK(bt_db_count(db) == 0);

    BtAddress a1, a2;
    bt_addr_parse("00:00:00:00:00:01", &a1);
    bt_addr_parse("00:00:00:00:00:02", &a2);

    BtDeviceInfo info;
    memset(&info, 0, sizeof info);
    info.addr = a1;
    info.class_of_device = 0x00010C;
    snprintf(info.name, sizeof info.name, "First");
    CHECK(bt_db_upsert(db, &info) == BT_OK);
    CHECK(bt_db_count(db) == 1);

    const BtDeviceInfo *found = bt_db_find(db, &a1);
    CHECK(found != NULL);
    CHECK(strcmp(found->name, "First") == 0);
    CHECK(found->class_of_device == 0x00010C);
    CHECK(bt_db_find(db, &a2) == NULL);

    /* An update without a name keeps the name we already had. */
    memset(&info, 0, sizeof info);
    info.addr = a1;
    info.rssi = -55;
    CHECK(bt_db_upsert(db, &info) == BT_OK);
    CHECK(bt_db_count(db) == 1);
    found = bt_db_find(db, &a1);
    CHECK(strcmp(found->name, "First") == 0);
    CHECK(found->rssi == -55);

    /* A second device. */
    memset(&info, 0, sizeof info);
    info.addr = a2;
    snprintf(info.name, sizeof info.name, "Second");
    CHECK(bt_db_upsert(db, &info) == BT_OK);
    CHECK(bt_db_count(db) == 2);
    CHECK(bt_db_at(db, 0) != NULL);
    CHECK(bt_db_at(db, 1) != NULL);
    CHECK(bt_db_at(db, 2) == NULL);
    CHECK(bt_db_at(db, -1) == NULL);

    CHECK(bt_db_remove(db, &a1) == BT_OK);
    CHECK(bt_db_count(db) == 1);
    CHECK(bt_db_remove(db, &a1) == BT_ENOENT);

    bt_db_free(db);
}

static void test_database_is_bounded(void) {
    BtDeviceDb *db = bt_db_new();
    CHECK(db != NULL);
    /* More devices than the table holds: the table never grows. */
    for (int i = 0; i < 200; i++) {
        BtDeviceInfo info;
        memset(&info, 0, sizeof info);
        info.addr.b[0] = 0x01;
        info.addr.b[1] = 0x02;
        info.addr.b[5] = (uint8_t)i;
        info.last_seen_ms = (uint64_t)i;
        CHECK(bt_db_upsert(db, &info) == BT_OK);
    }
    CHECK(bt_db_count(db) <= 64);
    CHECK(bt_db_count(db) > 0);
    bt_db_free(db);
}

static void test_database_rejects_bad_input(void) {
    BtDeviceDb *db = bt_db_new();
    BtDeviceInfo info;
    memset(&info, 0, sizeof info);
    CHECK(bt_db_upsert(db, NULL) == BT_EINVAL);
    CHECK(bt_db_upsert(db, &info) == BT_EINVAL); /* the zero address is not a device */
    CHECK(bt_db_find(NULL, &info.addr) == NULL);
    CHECK(bt_db_count(NULL) == 0);
    bt_db_free(db);
}

static void test_status_text(void) {
    CHECK(strcmp(bt_status_text(BT_OK), "ok") == 0);
    CHECK(strcmp(bt_status_text(BT_ECLOSED), "closed") == 0);
    CHECK(strcmp(bt_status_text(BT_EAUTH), "authentication failed") == 0);
    CHECK(strlen(bt_status_text(-999)) > 0);
}

int main(void) {
    test_address_parse();
    test_address_format();
    test_database();
    test_database_is_bounded();
    test_database_rejects_bad_input();
    test_status_text();
    return bt_done("test_bt_device");
}
