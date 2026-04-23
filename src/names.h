#ifndef NAMES_H
#define NAMES_H

/* Naming helpers that mirror the Python generator's NamingStyle classes. */

#include <stdbool.h>
#include <stddef.h>

/* Convert CamelCase / mixed names to underscore_lower.
 * dst must have capacity >= src_len * 2 + 1 (worst case).
 * Returns the number of chars written (not including null). */
size_t names_underscore(const char *src, char *dst, size_t dst_cap);

/* Convert string to UPPER_CASE in-place (modifies dst). */
void names_upper(char *s);

/* Append underscore-separated parts.
 * parts: array of C strings, nparts: count.
 * Writes to dst (must be large enough).  Returns bytes written. */
size_t names_join(const char **parts, int nparts, char *dst, size_t dst_cap);

/* Format a field tag macro name: <struct>_<field>_tag  (e.g. MyMsg_value_tag) */
void names_tag_define(const char *struct_name, const char *field_name,
                      char *out, size_t out_cap);

/* Format a message init macro: <name>_init_default */
void names_init_define(const char *msg_name, bool zero, char *out, size_t out_cap);

/* Duplicate a string (caller must free). */
char *names_strdup(const char *s);

/* Duplicate the first len bytes of s as a null-terminated string. */
char *names_strndup(const char *s, size_t len);

/* Make a C identifier from a header file name (uppercase, non-alnum → _). */
void names_header_guard(const char *headername, char *out, size_t out_cap);

#endif /* NAMES_H */
