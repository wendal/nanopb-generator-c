#include "generator.h"
#include "descriptor.h"
#include "options.h"
#include "strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#   include <direct.h>   /* _mkdir */
#   define PATH_SEP '\\'
#else
#   include <sys/stat.h>
#   include <sys/types.h>
#   define PATH_SEP '/'
#endif

/* ---- file I/O ---------------------------------------------------------- */

static uint8_t *read_file(const char *path, size_t *size_out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return NULL; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); *size_out = 0; return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return NULL;
    }
    fclose(fp);
    *size_out = (size_t)sz;
    return buf;
}

static bool write_file(const char *path, const char *data, size_t size)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror(path); return false; }
    bool ok = fwrite(data, 1, size, fp) == size;
    fclose(fp);
    return ok;
}

/* Create directory (and parents) for a file path */
static void ensure_dir(const char *filepath)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", filepath);
    /* Find last separator */
    char *last = strrchr(dir, '/');
#ifdef _WIN32
    char *last2 = strrchr(dir, '\\');
    if (last2 > last) last = last2;
#endif
    if (!last) return;
    *last = '\0';
    if (dir[0] == '\0') return;
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

/* ---- path helpers ------------------------------------------------------ */

/* Given a base filepath (no extension) and optional output_dir, extension,
 * header/source extension — build the output path. */
static void build_output_path(const char *proto_name,
                               const char *output_dir,
                               const char *extension,
                               const char *type_extension, /* ".h" or ".c" */
                               bool strip_path,
                               char *out, size_t out_size)
{
    /* Remove .proto from proto_name if present */
    char base[512];
    snprintf(base, sizeof(base), "%s", proto_name);
    size_t blen = strlen(base);
    if (blen > 6 && strcmp(base + blen - 6, ".proto") == 0)
        base[blen - 6] = '\0';

    if (strip_path) {
        const char *slash = strrchr(base, '/');
#ifdef _WIN32
        const char *sl2 = strrchr(base, '\\');
        if (sl2 > slash) slash = sl2;
#endif
        if (slash) memmove(base, slash + 1, strlen(slash));
    }

    if (output_dir && *output_dir) {
        snprintf(out, out_size, "%s%c%s%s%s",
                 output_dir, PATH_SEP, base, extension, type_extension);
    } else {
        snprintf(out, out_size, "%s%s%s", base, extension, type_extension);
    }
}

/* ---- Options file search ----------------------------------------------- */

static options_file_t *find_and_load_options(const char *proto_name,
                                              const gen_options_t *gen_opts,
                                              bool verbose)
{
    char base[512];
    snprintf(base, sizeof(base), "%s", proto_name);
    size_t blen = strlen(base);
    if (blen > 6 && strcmp(base + blen - 6, ".proto") == 0)
        base[blen - 6] = '\0';

    /* Build the options filename using the format string */
    char optname[512];
    if (gen_opts->options_file && strchr(gen_opts->options_file, '%'))
        snprintf(optname, sizeof(optname), gen_opts->options_file, base);
    else if (gen_opts->options_file)
        snprintf(optname, sizeof(optname), "%s", gen_opts->options_file);
    else
        snprintf(optname, sizeof(optname), "%s.options", base);

    /* Search "." and options_path entries */
    options_file_t *of = options_file_load(optname, verbose);
    if (of) return of;

    for (int i = 0; i < gen_opts->options_path_count; i++) {
        char full[512];
        snprintf(full, sizeof(full), "%s%c%s", gen_opts->options_path[i], PATH_SEP, optname);
        of = options_file_load(full, verbose);
        if (of) return of;
    }
    return NULL;
}

/* ---- Argument parser --------------------------------------------------- */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] file.pb ...\n"
        "\n"
        "Options:\n"
        "  -e EXT         Set extension for generated files [default: .pb]\n"
        "  -H EXT         Set extension for header files [default: .h]\n"
        "  -S EXT         Set extension for source files [default: .c]\n"
        "  -f FILE        Options file name format [default: %%s.options]\n"
        "  -I DIR         Search path for .options files\n"
        "  -D DIR         Output directory\n"
        "  -Q FORMAT      Format for generated #includes [default: #include \"%%s\"]\n"
        "  -L FORMAT      Format for library #includes [default: #include <%%s>]\n"
        "  --strip-path   Strip directory path from #include file names\n"
        "  -T, --no-timestamp  Don't add timestamp (default)\n"
        "  -t, --timestamp     Add timestamp\n"
        "  -q, --quiet    Suppress normal output\n"
        "  -v, --verbose  Print extra information\n"
        "  -s OPT:VAL     Set generator option (e.g. -s max_size:32)\n"
        "  -x FILE        Exclude file from generated #includes\n"
        "  -V, --version  Show version and exit\n"
        "  -h, --help     Show this help\n",
        prog);
}

typedef struct {
    const char **pb_files;
    int          pb_files_count;
    gen_options_t opts;
    /* Dynamic arrays for options_path and exclude */
    const char **options_path_buf;
    const char **exclude_buf;
    int options_path_alloc;
    int exclude_alloc;
} cli_args_t;

static bool parse_args(int argc, char **argv, cli_args_t *args)
{
    memset(args, 0, sizeof(*args));
    gen_options_init(&args->opts);

    static const char *pb_file_buf[256];
    args->pb_files = pb_file_buf;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-') {
            /* positional: pb file */
            if (args->pb_files_count >= 256) {
                fprintf(stderr, "Too many input files\n");
                return false;
            }
            pb_file_buf[args->pb_files_count++] = arg;
            continue;
        }

#define NEXTARG() (++i < argc ? argv[i] : (fprintf(stderr, "%s requires argument\n", arg), (char*)NULL))

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]); exit(0);
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            printf("%s\n", GENERATOR_VERSION);
            exit(0);
        } else if (strcmp(arg, "-T") == 0 || strcmp(arg, "--no-timestamp") == 0) {
            args->opts.notimestamp = true;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--timestamp") == 0) {
            args->opts.notimestamp = false;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            args->opts.quiet = true; args->opts.verbose = false;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            args->opts.verbose = true;
        } else if (strcmp(arg, "--strip-path") == 0) {
            args->opts.strip_path = true;
        } else if (strcmp(arg, "--no-strip-path") == 0) {
            args->opts.strip_path = false;
        } else if (strcmp(arg, "-e") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            args->opts.extension = v;
        } else if (strcmp(arg, "-H") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            args->opts.header_extension = v;
        } else if (strcmp(arg, "-S") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            args->opts.source_extension = v;
        } else if (strcmp(arg, "-f") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            args->opts.options_file = v;
        } else if (strcmp(arg, "-D") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            args->opts.output_dir = v;
        } else if (strcmp(arg, "-Q") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            if (strcmp(v, "quote")   == 0) v = "#include \"%s\"";
            if (strcmp(v, "bracket") == 0) v = "#include <%s>";
            args->opts.genformat = v;
        } else if (strcmp(arg, "-L") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            if (strcmp(v, "quote")   == 0) v = "#include \"%s\"";
            if (strcmp(v, "bracket") == 0) v = "#include <%s>";
            args->opts.libformat = v;
        } else if (strcmp(arg, "-I") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            if (args->opts.options_path_count >= args->options_path_alloc) {
                args->options_path_alloc = args->options_path_alloc ? args->options_path_alloc * 2 : 8;
                args->options_path_buf = realloc(args->options_path_buf,
                    (size_t)args->options_path_alloc * sizeof(char*));
                args->opts.options_path = args->options_path_buf;
            }
            args->options_path_buf[args->opts.options_path_count++] = v;
        } else if (strcmp(arg, "-x") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            if (args->opts.exclude_count >= args->exclude_alloc) {
                args->exclude_alloc = args->exclude_alloc ? args->exclude_alloc * 2 : 4;
                args->exclude_buf = realloc(args->exclude_buf,
                    (size_t)args->exclude_alloc * sizeof(char*));
                args->opts.exclude = args->exclude_buf;
            }
            args->exclude_buf[args->opts.exclude_count++] = v;
        } else if (strcmp(arg, "-s") == 0) {
            char *v = NEXTARG(); if (!v) return false;
            /* Parse "key:value" or "key=value" */
            char key[64], val[256];
            char *sep = strchr(v, ':');
            if (!sep) sep = strchr(v, '=');
            if (!sep) {
                fprintf(stderr, "Invalid option format '%s', expected key:value\n", v);
                return false;
            }
            size_t klen = (size_t)(sep - v);
            if (klen >= sizeof(key)) klen = sizeof(key) - 1;
            memcpy(key, v, klen); key[klen] = '\0';
            snprintf(val, sizeof(val), "%s", sep + 1);
            /* Apply to toplevel options */
            nanopb_options_t *t = &args->opts.toplevel;
            if      (strcmp(key, "max_size") == 0)  { t->max_size  = atoi(val); t->has_max_size  = true; }
            else if (strcmp(key, "max_length") == 0){ t->max_length = atoi(val); t->has_max_length = true; }
            else if (strcmp(key, "max_count") == 0) { t->max_count = atoi(val); t->has_max_count = true; }
            else if (strcmp(key, "type") == 0) {
                if      (strcmp(val, "FT_STATIC")   == 0) t->field_type = FT_STATIC;
                else if (strcmp(val, "FT_CALLBACK") == 0) t->field_type = FT_CALLBACK;
                else if (strcmp(val, "FT_POINTER")  == 0) t->field_type = FT_POINTER;
            }
            else if (strcmp(key, "long_names") == 0)
                t->long_names = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
            /* Other -s options can be extended as needed */
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return false;
        }
#undef NEXTARG
    }

    if (args->pb_files_count == 0) {
        print_usage(argv[0]);
        return false;
    }
    return true;
}

/* ---- Main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
    cli_args_t args;
    if (!parse_args(argc, argv, &args)) return 1;

    int exit_code = 0;

    for (int fi = 0; fi < args.pb_files_count; fi++) {
        const char *pb_path = args.pb_files[fi];

        /* Load the binary FileDescriptorSet */
        size_t data_size = 0;
        uint8_t *data = read_file(pb_path, &data_size);
        if (!data) { exit_code = 1; continue; }

        fdp_set_t *set = fdp_decode(data, data_size);
        free(data);
        if (!set || set->files_count == 0) {
            fprintf(stderr, "%s: Failed to decode FileDescriptorSet\n", pb_path);
            fdp_set_free(set);
            exit_code = 1;
            continue;
        }

        /* Process each file in the set, or just the last (user's file) */
        /* Convention: generate for all files (user can filter with -x) */
        /* For parity with Python generator: generate for LAST file in set */
        /* (dependencies are in earlier slots) */
        const fdp_file_t *target = &set->files[set->files_count - 1];

        /* Load .options file for the target */
        options_file_t *file_opts = find_and_load_options(
            target->name, &args.opts, args.opts.verbose);

        /* Generate */
        strbuf_t header = {0}, source = {0};
        bool ok = generate_file(target, set, file_opts, &args.opts, &header, &source);

        if (ok) {
            /* Build output paths */
            char hpath[512], cpath[512];
            build_output_path(target->name,
                              args.opts.output_dir,
                              args.opts.extension,
                              args.opts.header_extension,
                              args.opts.strip_path,
                              hpath, sizeof(hpath));
            build_output_path(target->name,
                              args.opts.output_dir,
                              args.opts.extension,
                              args.opts.source_extension,
                              args.opts.strip_path,
                              cpath, sizeof(cpath));

            ensure_dir(hpath);
            ensure_dir(cpath);

            if (!write_file(hpath, header.data, header.len)) { ok = false; }
            if (!write_file(cpath, source.data, source.len)) { ok = false; }

            if (!args.opts.quiet && ok)
                fprintf(stderr, "Writing to %s and %s\n", hpath, cpath);
        }

        if (!ok) exit_code = 1;

        strbuf_free(&header);
        strbuf_free(&source);
        options_file_free(file_opts);
        fdp_set_free(set);
    }

    free(args.options_path_buf);
    free(args.exclude_buf);
    gen_options_free(&args.opts);
    return exit_code;
}
