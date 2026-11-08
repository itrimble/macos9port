#include "os999_cdev.h"

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    os999_cdev_t module;
    char id_buf[OS999_CP_ID_MAX];
    char title_buf[OSPI_NAME];
    char summary_buf[128];
    char icon_buf[32];
    void *dl_handle;
    int active;
} RegisteredCdev;

static RegisteredCdev g_modules[OS999_CDEV_MAX_MODULES];
static int g_module_count;

static int valid_module_id(const char *id) {
    if (!id || !id[0]) return 0;
    for (size_t i = 0; id[i]; i++) {
        if (i + 1 >= OS999_CP_ID_MAX) return 0;
        unsigned char c = (unsigned char)id[i];
        if (!isalnum(c) && c != '_' && c != '-') return 0;
    }
    return 1;
}

static int valid_descriptor(const os999_cdev_t *m) {
    if (!m || m->api_version != OS999_CDEV_API_VERSION) return 0;
    if (!valid_module_id(m->id) || !m->title || !m->title[0] || !m->render) return 0;
    if (m->category < OS999_CDEV_CAT_APPEARANCE || m->category > OS999_CDEV_CAT_HARDWARE)
        return 0;
    return 1;
}

static int register_with_handle(const os999_cdev_t *m, void *dl_handle) {
    if (!valid_descriptor(m) || g_module_count >= OS999_CDEV_MAX_MODULES) return -1;
    if (os999_cdev_find(m->id) != NULL) return -1;
    RegisteredCdev *slot = &g_modules[g_module_count++];
    memset(slot, 0, sizeof *slot);
    slot->module = *m;
    snprintf(slot->id_buf, sizeof slot->id_buf, "%s", m->id);
    snprintf(slot->title_buf, sizeof slot->title_buf, "%s", m->title);
    snprintf(slot->summary_buf, sizeof slot->summary_buf, "%s", m->summary ? m->summary : "");
    snprintf(slot->icon_buf, sizeof slot->icon_buf, "%s",
             (m->icon_name && m->icon_name[0]) ? m->icon_name : "control_panel");
    slot->module.id = slot->id_buf;
    slot->module.title = slot->title_buf;
    slot->module.summary = slot->summary_buf;
    slot->module.icon_name = slot->icon_buf;
    slot->dl_handle = dl_handle;
    slot->active = 1;
    return g_module_count - 1;
}

int os999_cdev_register(const os999_cdev_t *module) {
    return register_with_handle(module, NULL) >= 0 ? 0 : -1;
}

static int has_plugin_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".dylib") == 0 || strcasecmp(dot, ".so") == 0 ||
           strcasecmp(dot, ".cdev") == 0;
}

int os999_cdev_load_plugin(const char *shared_lib_path) {
    if (!shared_lib_path || !shared_lib_path[0]) return -1;
    size_t len = strnlen(shared_lib_path, OSPI_PATH);
    if (len >= OSPI_PATH || strstr(shared_lib_path, "..") != NULL) return -1;
    struct stat st;
    if (lstat(shared_lib_path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    void *handle = dlopen(shared_lib_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return -1;
    os999_cdev_entry_fn entry = NULL;
    void *sym = dlsym(handle, OS999_CDEV_ENTRY_SYMBOL);
    memcpy(&entry, &sym, sizeof entry);
    if (!entry) {
        dlclose(handle);
        return -1;
    }
    const os999_cdev_t *desc = entry();
    if (register_with_handle(desc, handle) < 0) {
        dlclose(handle);
        return -1;
    }
    return 0;
}

static int cmp_str(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

int os999_cdev_scan_directory(const char *directory_path) {
    if (!directory_path || !directory_path[0]) return 0;
    size_t len = strnlen(directory_path, OSPI_PATH);
    if (len >= OSPI_PATH || strstr(directory_path, "..") != NULL) return -1;
    int fd = open(directory_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return 0;
    DIR *dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return 0;
    }
    char *names[OS999_CDEV_MAX_MODULES] = {0};
    int count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && count < OS999_CDEV_MAX_MODULES) {
        if (de->d_name[0] == '.' || !has_plugin_ext(de->d_name)) continue;
        size_t nlen = strlen(de->d_name);
        names[count] = malloc(nlen + 1);
        if (names[count]) {
            memcpy(names[count], de->d_name, nlen + 1);
            count++;
        }
    }
    closedir(dir);
    if (count > 1) qsort(names, (size_t)count, sizeof names[0], cmp_str);
    int loaded = 0;
    for (int i = 0; i < count; i++) {
        char full[OSPI_PATH];
        int k = snprintf(full, sizeof full, "%s/%s", directory_path, names[i]);
        if (k > 0 && (size_t)k < sizeof full && os999_cdev_load_plugin(full) == 0)
            loaded++;
        free(names[i]);
    }
    return loaded;
}

void os999_cdev_unregister_all(void) {
    for (int i = g_module_count - 1; i >= 0; i--) {
        if (!g_modules[i].active) continue;
        if (g_modules[i].module.shutdown)
            g_modules[i].module.shutdown(&g_modules[i].module);
        if (g_modules[i].dl_handle)
            dlclose(g_modules[i].dl_handle);
        memset(&g_modules[i], 0, sizeof g_modules[i]);
    }
    g_module_count = 0;
}

int os999_cdev_count(void) { return g_module_count; }

os999_cdev_t *os999_cdev_at(int index) {
    if (index < 0 || index >= g_module_count || !g_modules[index].active) return NULL;
    return &g_modules[index].module;
}

os999_cdev_t *os999_cdev_find(const char *id) {
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < g_module_count; i++) {
        if (g_modules[i].active && strcmp(g_modules[i].module.id, id) == 0)
            return &g_modules[i].module;
    }
    return NULL;
}

const char *os999_cdev_category_label(int category) {
    switch (category) {
    case OS999_CDEV_CAT_ALL: return "All";
    case OS999_CDEV_CAT_APPEARANCE: return "Appearance";
    case OS999_CDEV_CAT_SOUND: return "Sound";
    case OS999_CDEV_CAT_SYSTEM: return "System";
    case OS999_CDEV_CAT_HARDWARE: return "Hardware";
    default: return "Other";
    }
}
