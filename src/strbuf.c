#include "strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRBUF_INIT_CAP 256

void strbuf_init(strbuf_t *s)
{
    s->data = NULL;
    s->len  = 0;
    s->cap  = 0;
}

void strbuf_free(strbuf_t *s)
{
    free(s->data);
    s->data = NULL;
    s->len  = 0;
    s->cap  = 0;
}

static bool strbuf_grow(strbuf_t *s, size_t needed)
{
    if (needed <= s->cap) return true;
    size_t new_cap = s->cap ? s->cap : STRBUF_INIT_CAP;
    while (new_cap < needed) new_cap *= 2;
    char *p = realloc(s->data, new_cap);
    if (!p) return false;
    s->data = p;
    s->cap  = new_cap;
    return true;
}

bool strbuf_append_n(strbuf_t *s, const char *str, size_t len)
{
    if (!strbuf_grow(s, s->len + len + 1)) return false;
    memcpy(s->data + s->len, str, len);
    s->len += len;
    s->data[s->len] = '\0';
    return true;
}

bool strbuf_append(strbuf_t *s, const char *str)
{
    return strbuf_append_n(s, str, strlen(str));
}

bool strbuf_append_char(strbuf_t *s, char c)
{
    return strbuf_append_n(s, &c, 1);
}

bool strbuf_vappendf(strbuf_t *s, const char *fmt, va_list args)
{
    va_list args2;
    va_copy(args2, args);
    int needed = vsnprintf(NULL, 0, fmt, args2);
    va_end(args2);
    if (needed < 0) return false;
    if (!strbuf_grow(s, s->len + (size_t)needed + 1)) return false;
    vsnprintf(s->data + s->len, (size_t)needed + 1, fmt, args);
    s->len += (size_t)needed;
    return true;
}

bool strbuf_appendf(strbuf_t *s, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bool ok = strbuf_vappendf(s, fmt, args);
    va_end(args);
    return ok;
}

void strbuf_reset(strbuf_t *s)
{
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

char *strbuf_dup(const strbuf_t *s)
{
    char *p = malloc(s->len + 1);
    if (!p) return NULL;
    memcpy(p, s->data ? s->data : "", s->len + 1);
    return p;
}
