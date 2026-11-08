/* Credential redaction for diagnostics. The broker never logs payloads;
 * this guards anything else that might carry a secret (URLs, commands). */
#include "os999_guestnet.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

static const char *const keys[] = {"password=", "passwd=", "pass=", "pwd=", "token=", "secret=",
                                   "api_key=", "apikey=", "auth=", "session="};
static const char *const lines[] = {"PASS ", "AUTHINFO PASS ", "Authorization:", "Proxy-Authorization:",
                                    "Cookie:", "Set-Cookie:"};

void gn_redact(const char *in, char *out, size_t cap) {
    if (!cap) return;
    size_t o = 0;
    const char *s = in ? in : "";
    int line_start = 1;
    while (*s && o + 1 < cap) {
        int masked = 0;
        if (line_start)
            for (size_t k = 0; k < sizeof lines / sizeof *lines && !masked; k++) {
                size_t kl = strlen(lines[k]);
                if (strncasecmp(s, lines[k], kl)) continue;
                for (size_t i = 0; i < kl && o + 1 < cap; i++) out[o++] = s[i];
                s += kl;
                const char *mask = lines[k][kl - 1] == ' ' ? "[redacted]" : " [redacted]";
                for (size_t i = 0; mask[i] && o + 1 < cap; i++) out[o++] = mask[i];
                while (*s && *s != '\r' && *s != '\n') s++; /* rest of the line */
                masked = 1;
            }
        if (!masked)
            for (size_t k = 0; k < sizeof keys / sizeof *keys && !masked; k++) {
                size_t kl = strlen(keys[k]);
                if (strncasecmp(s, keys[k], kl) || (s > in && isalnum((unsigned char)s[-1]))) continue;
                for (size_t i = 0; i < kl && o + 1 < cap; i++) out[o++] = s[i];
                s += kl;
                const char *mask = "[redacted]";
                for (size_t i = 0; mask[i] && o + 1 < cap; i++) out[o++] = mask[i];
                while (*s && *s != '&' && *s != ';' && *s != ' ' && *s != '\r' && *s != '\n') s++;
                masked = 1;
            }
        if (!masked && !strncmp(s, "://", 3)) { /* URL userinfo: scheme://user:pass@host */
            const char *at = s + 3, *end = at;
            while (*end && *end != '/' && *end != '@' && *end != ' ') end++;
            if (*end == '@') {
                const char *mask = "://[redacted]@";
                for (size_t i = 0; mask[i] && o + 1 < cap; i++) out[o++] = mask[i];
                s = end + 1;
                masked = 1;
            }
        }
        if (masked) { line_start = 0; continue; }
        line_start = *s == '\n' || *s == '\r';
        out[o++] = *s++;
    }
    out[o] = 0;
}
