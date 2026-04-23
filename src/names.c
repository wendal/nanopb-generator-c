#include "names.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Convert CamelCase to underscore_lower.
 * Mirrors the Python NamingStyleC.underscore() method. */
size_t names_underscore(const char *src, char *dst, size_t dst_cap)
{
    if (!src || dst_cap == 0) return 0;
    size_t si = 0, di = 0;
    size_t slen = strlen(src);

    /* Pass 1: insert underscores */
    char *tmp = malloc(slen * 2 + 2);
    if (!tmp) return 0;
    size_t ti = 0;

    for (si = 0; si < slen; si++) {
        char c = src[si];
        if (c == '-') {
            tmp[ti++] = '_';
        } else if (si > 0 && isupper((unsigned char)c)) {
            /* Insert underscore before uppercase if previous was lowercase/digit,
             * or if this starts a run (e.g. ABCFoo → ABC_Foo handled by rule 2). */
            char prev = src[si - 1];
            char next = (si + 1 < slen) ? src[si + 1] : '\0';
            if (islower((unsigned char)prev) || isdigit((unsigned char)prev)) {
                tmp[ti++] = '_';
            } else if (isupper((unsigned char)prev) && islower((unsigned char)next)) {
                tmp[ti++] = '_';
            }
            tmp[ti++] = (char)tolower((unsigned char)c);
        } else {
            tmp[ti++] = (char)tolower((unsigned char)c);
        }
    }
    tmp[ti] = '\0';

    /* Copy to dst */
    di = ti < dst_cap - 1 ? ti : dst_cap - 1;
    memcpy(dst, tmp, di);
    dst[di] = '\0';
    free(tmp);
    return di;
}

void names_upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

size_t names_join(const char **parts, int nparts, char *dst, size_t dst_cap)
{
    size_t written = 0;
    for (int i = 0; i < nparts; i++) {
        if (i > 0 && written + 1 < dst_cap) {
            dst[written++] = '_';
        }
        size_t plen = strlen(parts[i]);
        size_t avail = dst_cap - written - 1;
        if (plen > avail) plen = avail;
        memcpy(dst + written, parts[i], plen);
        written += plen;
    }
    if (written < dst_cap) dst[written] = '\0';
    return written;
}

void names_tag_define(const char *struct_name, const char *field_name,
                      char *out, size_t out_cap)
{
    snprintf(out, out_cap, "%s_%s_tag", struct_name, field_name);
}

void names_init_define(const char *msg_name, bool zero, char *out, size_t out_cap)
{
    snprintf(out, out_cap, "%s_init_%s", msg_name, zero ? "zero" : "default");
}

char *names_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *names_strndup(const char *s, size_t len)
{
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

void names_header_guard(const char *headername, char *out, size_t out_cap)
{
    size_t i = 0;
    for (; headername[i] && i + 1 < out_cap; i++) {
        char c = headername[i];
        out[i] = isalnum((unsigned char)c) ? (char)toupper((unsigned char)c) : '_';
    }
    out[i] = '\0';
}
