#include "application_probe.h"
#include "mac_resource.h"
#include <string.h>

int mac_application_probe(const StMacFile *f, MacApplicationKind *kind, StErr *err) {
    if (!f || !kind) return st_fail(err, ST_EINVAL, "missing application input");
    *kind = MAC_APP_NONE;
    uint32_t type = st_mac_type(f);
    if (type && type != MAC_RESOURCE_TYPE('A','P','P','L'))
        return st_fail(err, ST_ENOENT, "Finder type is not APPL");
    if (!f->rsrc.n && f->data.n >= 8 && !memcmp(f->data.p, "Joy!peff", 8)) {
        *kind = f->data.n >= 12 && !memcmp(f->data.p + 8, "pwpc", 4) ? MAC_APP_PPC : MAC_APP_CFM;
        return st_fail(err, ST_EUNSUPPORTED, "PEF/CFM application is not supported by the 68k runtime");
    }
    if (!f->rsrc.n) return st_fail(err, type ? ST_EFORMAT : ST_ENOENT, "application has no resource fork");
    MacResourceFork fork;
    char message[160];
    if (mac_resource_open(&fork, f->rsrc.p, f->rsrc.n, message, sizeof message))
        return st_fail(err, ST_EFORMAT, "%s", message);
    MacResource code;
    if (mac_resource_find(&fork, MAC_RESOURCE_TYPE('C','O','D','E'), 0, &code) == 0) {
        if (!code.size) return st_fail(err, ST_EFORMAT, "empty CODE 0 resource");
        *kind = MAC_APP_68K;
        return ST_OK;
    }
    for (size_t i = 0; i < mac_resource_count(&fork); i++) {
        MacResource r;
        if (!mac_resource_at(&fork, i, &r) && r.type == MAC_RESOURCE_TYPE('c','f','r','g')) {
            *kind = MAC_APP_CFM;
            return st_fail(err, ST_EUNSUPPORTED, "CFM application has no supported CODE 0 entry point");
        }
    }
    return st_fail(err, type ? ST_EFORMAT : ST_ENOENT, "application has no CODE 0 entry point");
}

int mac_application_select(const StArchive *a, const char *path, size_t *index,
                            MacApplicationKind *kind, StErr *err) {
    if (!a || !index || !kind) return st_fail(err, ST_EINVAL, "missing selection output");
    *index = SIZE_MAX;
    *kind = MAC_APP_NONE;
    size_t selected = SIZE_MAX;
    int selected_rc = ST_ENOENT;
    StErr selected_err = {0};
    MacApplicationKind selected_kind = MAC_APP_NONE;
    for (size_t i = 0; i < a->n; i++) {
        const StEntry *entry = &a->v[i];
        if (entry->is_dir || (path && strcmp(path, entry->path))) continue;
        StErr why = {0};
        MacApplicationKind found;
        int rc = mac_application_probe(&entry->file, &found, &why);
        if (!path && st_mac_type(&entry->file) != MAC_RESOURCE_TYPE('A','P','P','L') && rc != ST_OK)
            continue;
        if (selected != SIZE_MAX) return st_fail(err, ST_EEXIST, "multiple applications; select an exact path");
        selected = i;
        selected_rc = rc;
        selected_err = why;
        selected_kind = found;
    }
    if (selected == SIZE_MAX) return st_fail(err, ST_ENOENT, "selected application not found");
    *index = selected;
    *kind = selected_kind;
    if (selected_rc && err) *err = selected_err;
    return selected_rc;
}
