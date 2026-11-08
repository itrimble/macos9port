#ifndef OS999_FINDER_PRINT_H
#define OS999_FINDER_PRINT_H
#include "ospi.h"
enum { PRINT_PAGE_SETUP = 1, PRINT_TEXT, PRINT_WINDOW };
int finder_print_enabled(const Session *s, int command);
int finder_print_command(Session *s, int command);
int desktop_print_supported(void);
void desktop_print_service(Session *s, const uint32_t *frame);
/* Test/export entry point never sends a physical print job. */
int desktop_print_export_pdf(Session *s, const uint32_t *frame, const char *path);
#endif
