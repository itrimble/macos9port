#ifndef OS999_OSA_JSON_H
#define OS999_OSA_JSON_H

#include <stddef.h>

/* Bounded JSON support for the OSA bridge: flat objects with string and
 * boolean values only. All functions return OSA_OK or OSA_ERR_BAD_PARAMETER
 * and reject malformed or oversized input. Unicode escapes are limited to
 * nonzero ASCII; unsupported escapes fail instead of silently losing data. */

/* Writes in as a quoted JSON string with escapes (" \ and \u00xx for control
 * characters) plus a terminator into out. */
int osa_json_escape(const char *in, char *out, size_t n);

/* Returns 1 when present, 0 when absent, -1 for malformed objects. */
int osa_json_member_present(const char *json, const char *key);

/* Finds key in a flat JSON object and copies its decoded string value. */
int osa_json_find_string(const char *json, const char *key, char *out, size_t n);

/* Finds key in a flat JSON object and reports its boolean value in *out. */
int osa_json_find_bool(const char *json, const char *key, int *out);

#endif
