#ifndef OPTIONS_H
#define OPTIONS_H

#include "descriptor.h"
#include <stdbool.h>

/* ---- CLI options -------------------------------------------------------- */

typedef struct {
    const char  *extension;         /* default: ".pb" */
    const char  *header_extension;  /* default: ".h" */
    const char  *source_extension;  /* default: ".c" */
    const char  *options_file;      /* default: "%s.options" */
    const char **options_path;      /* search dirs for .options */
    int          options_path_count;
    const char  *output_dir;        /* NULL → same dir as input */
    bool         notimestamp;
    bool         quiet;
    bool         verbose;
    bool         strip_path;
    bool         error_on_unmatched;
    const char  *libformat;   /* default: "#include <%s>" */
    const char  *genformat;   /* default: "#include \"%s\"" */
    const char **exclude;     /* files to exclude from generated #include */
    int          exclude_count;
    /* Top-level nanopb option overrides from -s flag */
    nanopb_options_t toplevel;
} gen_options_t;

void gen_options_init(gen_options_t *opts);
void gen_options_free(gen_options_t *opts);

/* ---- .options file entries --------------------------------------------- */

typedef struct {
    char            *namemask;   /* e.g. "MyMessage.my_field" or "MyMessage.*" */
    nanopb_options_t options;
} options_entry_t;

typedef struct {
    options_entry_t *entries;
    int              count;
} options_file_t;

/* Parse a .options file.  Returns NULL on file-not-found (not an error),
 * or exits on parse error. */
options_file_t *options_file_load(const char *path, bool verbose);
void            options_file_free(options_file_t *of);

/* Apply matching options from 'of' (and toplevel from opts) to a copy of
 * 'base', where dotname is the fully-qualified proto name (e.g. "Pkg.Msg.field").
 * Caller must nanopb_options_free() the result. */
nanopb_options_t options_get(const options_file_t *of,
                              const gen_options_t  *opts,
                              const nanopb_options_t *base,
                              const char *dotname);

/* Return true if dotname matches mask (supports '*' wildcard at end). */
bool options_match(const char *mask, const char *dotname);

#endif /* OPTIONS_H */
