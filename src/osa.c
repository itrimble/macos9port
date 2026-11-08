/* OSA component registry with lazy built-in registration. */
#include "osa.h"

#include <string.h>

static const OsaComponent *k_components[OSA_MAX_COMPONENTS];
static int k_component_count;
static int k_registry_ready;

/* Registers the built-in scripting components once. */
static void osa_registry_init(void) {
    if (k_registry_ready) return;
    k_registry_ready = 1;
    static const OsaComponent k_applescript = {
        OSA_TYPE_APPLESCRIPT,
        "OS999 Script",
        osa_script_compile,
        osa_script_execute,
    };
    (void)osa_component_register(&k_applescript);
}

int osa_component_register(const OsaComponent *component) {
    if (!component || component->scripting_type == 0 || !component->name) return -1;
    osa_registry_init();
    for (int i = 0; i < k_component_count; i++) {
        if (k_components[i]->scripting_type == component->scripting_type) return -1;
    }
    if (k_component_count >= OSA_MAX_COMPONENTS) return -1;
    k_components[k_component_count++] = component;
    return 0;
}

const OsaComponent *osa_component_find(uint32_t scripting_type) {
    osa_registry_init();
    for (int i = 0; i < k_component_count; i++) {
        if (k_components[i]->scripting_type == scripting_type) return k_components[i];
    }
    return NULL;
}

int osa_component_count(void) {
    osa_registry_init();
    return k_component_count;
}
