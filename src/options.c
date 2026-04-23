#include "options.h"
#include "names.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gen_options_init(gen_options_t *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->extension        = ".pb";
    opts->header_extension = ".h";
    opts->source_extension = ".c";
    opts->options_file     = "%s.options";
    opts->libformat        = "#include <%s>";
    opts->genformat        = "#include \"%s\"";
    opts->notimestamp      = true;
    nanopb_options_init(&opts->toplevel);
}

void gen_options_free(gen_options_t *opts)
{
    free(opts->options_path);
    free(opts->exclude);
    nanopb_options_free(&opts->toplevel);
}

bool options_match(const char *mask, const char *dotname)
{
    size_t mlen = strlen(mask);
    if (mlen == 0) return false;
    if (mask[mlen - 1] == '*') {
        /* prefix match */
        return strncmp(mask, dotname, mlen - 1) == 0;
    }
    return strcmp(mask, dotname) == 0;
}

/* ---- Minimal text-format NanoPBOptions parser -------------------------- */
/* Parses "key: value" pairs as found in .options files (nanopb subset). */

static bool parse_bool(const char *s)
{
    return strcmp(s, "true") == 0 || strcmp(s, "1") == 0;
}

static void apply_kv(nanopb_options_t *opts, const char *key, const char *val)
{
    if      (strcmp(key, "max_size") == 0)  { opts->max_size  = atoi(val); opts->has_max_size  = true; }
    else if (strcmp(key, "max_length") == 0){ opts->max_length = atoi(val); opts->has_max_length = true; }
    else if (strcmp(key, "max_count") == 0) { opts->max_count = atoi(val); opts->has_max_count = true; }
    else if (strcmp(key, "int_size") == 0)  {
        if      (strcmp(val, "IS_8") == 0 || strcmp(val, "8") == 0)  opts->int_size = IS_8;
        else if (strcmp(val, "IS_16") == 0 || strcmp(val, "16") == 0) opts->int_size = IS_16;
        else if (strcmp(val, "IS_32") == 0 || strcmp(val, "32") == 0) opts->int_size = IS_32;
        else if (strcmp(val, "IS_64") == 0 || strcmp(val, "64") == 0) opts->int_size = IS_64;
    }
    else if (strcmp(key, "type") == 0) {
        if      (strcmp(val, "FT_CALLBACK") == 0) opts->field_type = FT_CALLBACK;
        else if (strcmp(val, "FT_STATIC") == 0)   opts->field_type = FT_STATIC;
        else if (strcmp(val, "FT_IGNORE") == 0)   opts->field_type = FT_IGNORE;
        else if (strcmp(val, "FT_POINTER") == 0)  opts->field_type = FT_POINTER;
    }
    else if (strcmp(key, "long_names") == 0)    opts->long_names     = parse_bool(val);
    else if (strcmp(key, "packed_struct") == 0) opts->packed_struct  = parse_bool(val);
    else if (strcmp(key, "packed_enum") == 0)   opts->packed_enum    = parse_bool(val);
    else if (strcmp(key, "skip_message") == 0)  opts->skip_message   = parse_bool(val);
    else if (strcmp(key, "no_unions") == 0)     opts->no_unions      = parse_bool(val);
    else if (strcmp(key, "anonymous_oneof") == 0) opts->anonymous_oneof = parse_bool(val);
    else if (strcmp(key, "proto3") == 0)        opts->proto3         = parse_bool(val);
    else if (strcmp(key, "fixed_length") == 0)  opts->fixed_length   = parse_bool(val);
    else if (strcmp(key, "fixed_count") == 0)   opts->fixed_count    = parse_bool(val);
    else if (strcmp(key, "default_has") == 0)   opts->default_has    = parse_bool(val);
    else if (strcmp(key, "sort_by_tag") == 0)   opts->sort_by_tag    = parse_bool(val);
    else if (strcmp(key, "callback_datatype") == 0) {
        free(opts->callback_datatype);
        opts->callback_datatype = names_strdup(val);
    }
    else if (strcmp(key, "descriptorsize") == 0) {
        if (strcmp(val, "DS_1") == 0) opts->descriptorsize = DS_1;
        else if (strcmp(val, "DS_2") == 0) opts->descriptorsize = DS_2;
        else if (strcmp(val, "DS_4") == 0) opts->descriptorsize = DS_4;
        else if (strcmp(val, "DS_8") == 0) opts->descriptorsize = DS_8;
    }
    else if (strcmp(key, "fallback_type") == 0) {
        if      (strcmp(val, "FT_CALLBACK") == 0) opts->fallback_type = FT_CALLBACK;
        else if (strcmp(val, "FT_STATIC") == 0)   opts->fallback_type = FT_STATIC;
        else if (strcmp(val, "FT_POINTER") == 0)  opts->fallback_type = FT_POINTER;
    }
    /* Unknown keys are silently ignored */
}

/* Parse "key: value" or "key:value" (strips quotes from value strings) */
static void parse_option_pair(nanopb_options_t *opts, const char *pair)
{
    const char *colon = strchr(pair, ':');
    if (!colon) return;
    size_t klen = (size_t)(colon - pair);
    while (klen > 0 && isspace((unsigned char)pair[klen - 1])) klen--;
    char key[64] = {0};
    if (klen >= sizeof(key)) return;
    memcpy(key, pair, klen);

    const char *vstart = colon + 1;
    while (*vstart && isspace((unsigned char)*vstart)) vstart++;
    /* Strip trailing whitespace */
    size_t vlen = strlen(vstart);
    while (vlen > 0 && isspace((unsigned char)vstart[vlen - 1])) vlen--;
    char val[256] = {0};
    if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
    memcpy(val, vstart, vlen);

    apply_kv(opts, key, val);
}

options_file_t *options_file_load(const char *path, bool verbose)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    if (verbose) fprintf(stderr, "Reading options from %s\n", path);

    options_file_t *of = calloc(1, sizeof(options_file_t));
    if (!of) { fclose(fp); return NULL; }

    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        /* Strip comments */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *sl2 = strstr(line, "//");
        if (sl2) *sl2 = '\0';
        /* Strip block comments (simple single-line) */
        char *bs = strstr(line, "/*");
        if (bs) {
            char *be = strstr(bs, "*/");
            if (be) memmove(bs, be + 2, strlen(be + 2) + 1);
            else *bs = '\0';
        }

        /* Skip blank lines */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) continue;

        /* First token = namemask, rest = option text */
        const char *mask_start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t mask_len = (size_t)(p - mask_start);

        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) {
            fprintf(stderr, "%s:%d: missing option after field name\n", path, lineno);
            fclose(fp);
            options_file_free(of);
            exit(1);
        }

        options_entry_t entry;
        entry.namemask = names_strndup(mask_start, mask_len);
        nanopb_options_init(&entry.options);
        parse_option_pair(&entry.options, p);

        of->entries = realloc(of->entries, (size_t)(of->count + 1) * sizeof(options_entry_t));
        of->entries[of->count++] = entry;
    }
    fclose(fp);
    return of;
}

void options_file_free(options_file_t *of)
{
    if (!of) return;
    for (int i = 0; i < of->count; i++) {
        free(of->entries[i].namemask);
        nanopb_options_free(&of->entries[i].options);
    }
    free(of->entries);
    free(of);
}

nanopb_options_t options_get(const options_file_t *of,
                              const gen_options_t  *opts,
                              const nanopb_options_t *base,
                              const char *dotname)
{
    nanopb_options_t result;
    nanopb_options_init(&result);
    nanopb_options_merge(&result, base);

    /* Apply toplevel options from -s flag */
    nanopb_options_merge(&result, &opts->toplevel);

    if (!of) return result;

    /* Apply matching entries from .options file */
    for (int i = 0; i < of->count; i++) {
        if (options_match(of->entries[i].namemask, dotname)) {
            nanopb_options_merge(&result, &of->entries[i].options);
        }
    }
    return result;
}
