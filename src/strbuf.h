#ifndef STRBUF_H
#define STRBUF_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} strbuf_t;

void strbuf_init(strbuf_t *s);
void strbuf_free(strbuf_t *s);

/* Append raw bytes (no null check). Returns false on OOM. */
bool strbuf_append_n(strbuf_t *s, const char *str, size_t len);

/* Append a null-terminated string. */
bool strbuf_append(strbuf_t *s, const char *str);

/* Printf-style append. */
bool strbuf_appendf(strbuf_t *s, const char *fmt, ...);
bool strbuf_vappendf(strbuf_t *s, const char *fmt, va_list args);

/* Append a single character. */
bool strbuf_append_char(strbuf_t *s, char c);

/* Reset length to 0 (keeps allocation). */
void strbuf_reset(strbuf_t *s);

/* Return a heap-allocated copy of the contents (caller must free). */
char *strbuf_dup(const strbuf_t *s);

#endif /* STRBUF_H */
