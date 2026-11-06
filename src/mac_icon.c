#include "mac_icon.h"
#include "mac_resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int bundled_application_icon(const MacResourceFork *fork, int16_t *id) {
    for (size_t r = 0; r < mac_resource_count(fork); r++) {
        MacResource bundle;
        if (mac_resource_at(fork, r, &bundle) ||
            bundle.type != MAC_RESOURCE_TYPE('B','N','D','L') || bundle.size < 8) continue;
        size_t types = (size_t)be16(bundle.data + 6) + 1, pos = 8, nf = 0, ni = 0;
        int16_t file_local[64], file_resource[64], icon_local[64], icon_resource[64];
        for (size_t t = 0; t < types; t++) {
            if (bundle.size - pos < 6) break;
            uint32_t type = be32(bundle.data + pos);
            size_t count = (size_t)be16(bundle.data + pos + 4) + 1;
            pos += 6;
            if (count > 64 || count > (bundle.size - pos) / 4) break;
            int16_t *locals = type == MAC_RESOURCE_TYPE('F','R','E','F') ? file_local :
                              type == MAC_RESOURCE_TYPE('I','C','N','#') ? icon_local : NULL;
            int16_t *resources = type == MAC_RESOURCE_TYPE('F','R','E','F') ? file_resource :
                                 type == MAC_RESOURCE_TYPE('I','C','N','#') ? icon_resource : NULL;
            if (locals) {
                for (size_t i = 0; i < count; i++) {
                    locals[i] = (int16_t)be16(bundle.data + pos + i * 4);
                    resources[i] = (int16_t)be16(bundle.data + pos + i * 4 + 2);
                }
                if (locals == file_local) nf = count;
                else ni = count;
            }
            pos += count * 4;
        }
        for (size_t i = 0; i < nf; i++) {
            MacResource fref;
            if (mac_resource_find(fork, MAC_RESOURCE_TYPE('F','R','E','F'), file_resource[i], &fref) ||
                fref.size < 4 || be32(fref.data) != MAC_RESOURCE_TYPE('A','P','P','L')) continue;
            for (size_t j = 0; j < ni; j++) if (icon_local[j] == file_local[i]) {
                *id = icon_resource[j];
                return 1;
            }
        }
    }
    return 0;
}

static void load_icon(const MacResourceFork *fork, Item *item) {
    int16_t id;
    if (!bundled_application_icon(fork, &id)) return;
    MacResource color, mono;
    if (mac_resource_find(fork, MAC_RESOURCE_TYPE('i','c','l','8'), id, &color) ||
        mac_resource_find(fork, MAC_RESOURCE_TYPE('I','C','N','#'), id, &mono) ||
        color.size != sizeof item->guest_icon || mono.size < 256) return;
    memcpy(item->guest_icon, color.data, sizeof item->guest_icon);
    memcpy(item->guest_icon_mask, mono.data + 128, sizeof item->guest_icon_mask);
    item->has_guest_icon = 1;
}

int mac_icon_application(const char *root, const char *rel, Item *item) {
    char sidecar_rel[OSPI_PATH], path[OSPI_PATH];
    int n = snprintf(sidecar_rel, sizeof sidecar_rel, "%s.rsrc", rel);
    if (!item || n < 0 || (size_t)n >= sizeof sidecar_rel ||
        volume_join(path, sizeof path, root, sidecar_rel) != 0) return 0;
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > 64 * 1024 * 1024) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t size = (size_t)st.st_size;
    uint8_t *bytes = malloc(size);
    int valid = 0;
    if (bytes && fread(bytes, 1, size, f) == size) {
        MacResourceFork fork;
        MacResource code;
        char err[80];
        valid = mac_resource_open_container(&fork, bytes, size, err, sizeof err) == 0 &&
                mac_resource_find(&fork, MAC_RESOURCE_TYPE('C','O','D','E'), 0, &code) == 0;
        if (valid) load_icon(&fork, item);
    }
    free(bytes);
    fclose(f);
    return valid;
}
