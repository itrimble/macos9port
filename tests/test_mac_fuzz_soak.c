#include "mac_icon.h"
#include "mac_resource.h"
#include "ospi_icon_hd.h"
#include "runtime_bridge.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static uint64_t xorshift64s(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void build_seed_appledouble_fork(uint8_t *buf, size_t *out_len) {
    memset(buf, 0, 512);
    /* AppleDouble v2 header with 1 entry (ID 2 = Resource Fork at offset 64, len 314). */
    const uint8_t hdr[] = {
        0x00, 0x05, 0x16, 0x07, 0x00, 0x02, 0x00, 0x00,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x00, 0x01,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x3A
    };
    memcpy(buf, hdr, sizeof(hdr));
    uint8_t *rf = buf + 0x40;
    /* Minimal valid resource fork at offset 0x40: data @ 0x100 (len 8), map @ 0x108 (len 50). */
    rf[0] = 0x00; rf[1] = 0x00; rf[2] = 0x01; rf[3] = 0x00;
    rf[4] = 0x00; rf[5] = 0x00; rf[6] = 0x01; rf[7] = 0x08;
    rf[8] = 0x00; rf[9] = 0x00; rf[10] = 0x00; rf[11] = 0x08;
    rf[12] = 0x00; rf[13] = 0x00; rf[14] = 0x00; rf[15] = 0x32;
    rf[0x100] = 0x00; rf[0x101] = 0x00; rf[0x102] = 0x00; rf[0x103] = 0x04;
    memcpy(rf + 0x104, "OS99", 4);
    uint8_t *map = rf + 0x108;
    map[24] = 0x00; map[25] = 0x1C;
    map[26] = 0x00; map[27] = 0x32;
    map[28] = 0x00; map[29] = 0x00;
    memcpy(map + 30, "TEST", 4);
    map[34] = 0x00; map[35] = 0x00;
    map[36] = 0x00; map[37] = 0x0A;
    map[38] = 0x00; map[39] = 0x80;
    map[40] = 0xFF; map[41] = 0xFF;
    *out_len = 0x40 + 0x108 + 50;
}

static void mutate_buffer(uint8_t *buf, size_t cap, size_t *len, uint64_t *rng) {
    uint64_t r = xorshift64s(rng);
    uint32_t mode = (uint32_t)(r % 4u);
    if (mode == 0u && *len > 0) {
        size_t idx = (size_t)(xorshift64s(rng) % *len);
        buf[idx] ^= (uint8_t)(1u << (xorshift64s(rng) % 8u));
    } else if (mode == 1u && *len >= 4) {
        static const uint32_t boundaries[] = {
            0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x00051600u
        };
        uint32_t val = boundaries[xorshift64s(rng) % 6u];
        size_t idx = (size_t)(xorshift64s(rng) % (*len - 3));
        buf[idx] = (uint8_t)(val >> 24);
        buf[idx + 1] = (uint8_t)(val >> 16);
        buf[idx + 2] = (uint8_t)(val >> 8);
        buf[idx + 3] = (uint8_t)val;
    } else if (mode == 2u) {
        *len = (size_t)(xorshift64s(rng) % (cap + 1));
    } else if (*len > 0) {
        size_t idx = (size_t)(xorshift64s(rng) % *len);
        buf[idx] = (uint8_t)(xorshift64s(rng) & 0xFFu);
    }
}

static void test_fuzz_resource_and_icon_parsers(void) {
    uint8_t seed[512];
    size_t seed_len = 0;
    build_seed_appledouble_fork(seed, &seed_len);

    MacResourceFork fork;
    char err[128];
    CHECK(mac_resource_open_container(&fork, seed, seed_len, err, sizeof(err)) == 0);
    CHECK(mac_resource_count(&fork) == 1u);

    uint64_t rng = 0x05999F00DFACE001ULL;
    uint8_t mutated[512];
    int valid_parses = 0;
    for (int i = 0; i < 25000; i++) {
        memcpy(mutated, seed, sizeof(seed));
        size_t cur_len = seed_len;
        int steps = 1 + (int)(xorshift64s(&rng) % 4u);
        for (int s = 0; s < steps; s++) {
            mutate_buffer(mutated, sizeof(mutated), &cur_len, &rng);
        }
        if (mac_resource_open_container(&fork, mutated, cur_len, err, sizeof(err)) == 0) {
            valid_parses++;
            size_t count = mac_resource_count(&fork);
            for (size_t idx = 0; idx < count && idx < 8u; idx++) {
                MacResource res;
                if (mac_resource_at(&fork, idx, &res) == 0 && res.size > 0) {
                    volatile uint8_t probe = res.data[0] ^ res.data[res.size - 1];
                    (void)probe;
                }
            }
        }
    }
    CHECK(valid_parses > 0);

    char tmp_dir[] = "/tmp/os999-fuzz-XXXXXX";
    CHECK(mkdtemp(tmp_dir) != NULL);
    char app_path[256], ad_path[256], hd_path[256];
    snprintf(app_path, sizeof(app_path), "%s/App", tmp_dir);
    snprintf(ad_path, sizeof(ad_path), "%s/._App", tmp_dir);
    snprintf(hd_path, sizeof(hd_path), "%s/mutated.rgba", tmp_dir);

    FILE *af = fopen(app_path, "wb");
    if (af) fclose(af);

    for (int i = 0; i < 250; i++) {
        memcpy(mutated, seed, sizeof(seed));
        size_t cur_len = seed_len;
        mutate_buffer(mutated, sizeof(mutated), &cur_len, &rng);

        FILE *f = fopen(ad_path, "wb");
        if (f) {
            fwrite(mutated, 1, cur_len, f);
            fclose(f);
        }
        Item item;
        memset(&item, 0, sizeof(item));
        (void)mac_icon_application(tmp_dir, "App", &item);
        CHECK(item.has_guest_icon == 0 || item.has_guest_icon == 1);

        f = fopen(hd_path, "wb");
        if (f) {
            fwrite(mutated, 1, cur_len, f);
            fclose(f);
        }
        OspiHdIconSet hd;
        memset(&hd, 0, sizeof(hd));
        if (ospi_hd_icon_set_load(&hd, hd_path)) {
            ospi_hd_icon_set_free(&hd);
        }
    }
    unlink(app_path);
    unlink(ad_path);
    unlink(hd_path);
    rmdir(tmp_dir);
    CHECK(1);
}

static void test_runtime_bridge_soak(void) {
    char shm_name[64];
    RuntimeBridge *host = runtime_bridge_create(shm_name, sizeof(shm_name));
    CHECK(host != NULL);
    RuntimeBridge *guest = runtime_bridge_open(shm_name);
    CHECK(guest != NULL);

    uint32_t tx_frame[64 * 48];
    uint32_t rx_frame[64 * 48];
    for (size_t i = 0; i < 64 * 48; i++) {
        tx_frame[i] = 0xFF000000u | (uint32_t)i;
    }

    uint64_t last_seq = 0;
    for (int iter = 0; iter < 10000; iter++) {
        tx_frame[0] = 0xFF000000u | (uint32_t)iter;
        CHECK(runtime_bridge_publish(guest, tx_frame, 64, 48) == 0);

        int w = 0, h = 0;
        uint64_t seq = last_seq;
        int rc = runtime_bridge_read(host, rx_frame, 64 * 48, &w, &h, &seq);
        if (rc != 1 || w != 64 || h != 48 || seq <= last_seq || rx_frame[0] != tx_frame[0]) {
            CHECK(0);
            break;
        }
        last_seq = seq;

        RuntimeBridgeInput in_ev = {
            RUNTIME_BRIDGE_MOTION, iter & 511, iter & 383, (uint32_t)iter, 0u
        };
        (void)runtime_bridge_push(host, in_ev);
        RuntimeBridgeInput out_ev;
        if (runtime_bridge_pop(guest, &out_ev) != 1 || out_ev.message != (uint32_t)iter) {
            CHECK(0);
            break;
        }
    }
    CHECK(last_seq == 10000u);

    /* Reject oversized frame dimensions cleanly without corrupting bridge state. */
    CHECK(runtime_bridge_publish(guest, tx_frame, RUNTIME_BRIDGE_MAX_WIDTH + 1, 48) != 0);

    runtime_bridge_close(guest);
    runtime_bridge_close(host);
    runtime_bridge_unlink(shm_name);
}

int main(void) {
    test_fuzz_resource_and_icon_parsers();
    test_runtime_bridge_soak();
    if (failures > 0) {
        fprintf(stderr, "test_mac_fuzz_soak: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_fuzz_soak: all %d checks passed\n", checks);
    return 0;
}
