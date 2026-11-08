#include "finder_print.h"
int desktop_print_supported(void) { return 0; }
void desktop_print_service(Session *s, const uint32_t *frame) { (void)frame; s->print_request = 0; }
int desktop_print_export_pdf(Session *s, const uint32_t *frame, const char *path) {
    (void)s; (void)frame; (void)path; return -1;
}
