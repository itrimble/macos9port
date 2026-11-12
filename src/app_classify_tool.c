/* os999-classify: print the classification of one application as JSON. Reads only the forks named on the command line.
 *   os999-classify [--data FILE] [--rsrc FILE] [--type APPL] [--ppc]
 * --ppc pretends the host can run PowerPC (to see which path a mixed program would take); the default is this build. */
#include "app_classify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *bytes = NULL;
    long length;
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 0 || length > 128l * 1024 * 1024 || fseek(f, 0, SEEK_SET)) goto done;
    bytes = malloc(length ? (size_t)length : 1);
    if (bytes && fread(bytes, 1, (size_t)length, f) != (size_t)length) { free(bytes); bytes = NULL; }
    *size = (size_t)length;
done:
    fclose(f);
    return bytes;
}

int main(int argc, char **argv) {
    const char *data_path = NULL, *rsrc_path = NULL;
    uint32_t type = 0;
    AppClassifyCaps caps = {0, 0};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data_path = argv[++i];
        else if (!strcmp(argv[i], "--rsrc") && i + 1 < argc) rsrc_path = argv[++i];
        else if (!strcmp(argv[i], "--type") && i + 1 < argc && strlen(argv[i + 1]) == 4) {
            const unsigned char *t = (const unsigned char *)argv[++i];
            type = (uint32_t)t[0] << 24 | (uint32_t)t[1] << 16 | (uint32_t)t[2] << 8 | t[3];
        } else if (!strcmp(argv[i], "--ppc")) caps.ppc_available = 1;
        else { fprintf(stderr, "Usage: os999-classify [--data FILE] [--rsrc FILE] [--type XXXX] [--ppc]\n"); return 2; }
    }
    size_t dn = 0, rn = 0;
    uint8_t *data = data_path ? slurp(data_path, &dn) : NULL, *rsrc = rsrc_path ? slurp(rsrc_path, &rn) : NULL;
    if ((data_path && !data) || (rsrc_path && !rsrc)) { fprintf(stderr, "Cannot read an input file.\n"); free(data); free(rsrc); return 1; }
    AppClassification c;
    app_classify(data, dn, rsrc, rn, type, &caps, &c);
    static char json[8192];
    app_classification_json(&c, json, sizeof json);
    puts(json);
    free(data);
    free(rsrc);
    return 0;
}
