#ifndef OSPI_MAC_ICON_INCLUDED
#define OSPI_MAC_ICON_INCLUDED

#include "ospi.h"

/* Inspect an adjacent guest resource fork; retain its application icon in Item. */
int mac_icon_application(const char *root, const char *rel, Item *item);
int mac_icon_application_at(int directory, const char *name, Item *item);

/* Convert a classic Macintosh system-palette index to opaque host ARGB. */
uint32_t mac_icon_color(uint8_t index);

#endif
