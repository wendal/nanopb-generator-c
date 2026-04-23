#include "generator.h"
#include "names.h"
#include "strbuf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Section 1 — Utilities
 * ===================================================================== */

/* Returns number of bytes needed to encode 'value' as a protobuf varint.
 * For negative values (as int64), uses the full 10-byte encoding. */
static int varint_max_size(int64_t value)
{
    if (value < 0) return 10;   /* negative → 64-bit two's complement */
    uint64_t v = (uint64_t)value;
    int i;
    for (i = 1; i <= 10; i++) {
        if ((v >> (i * 7)) == 0) return i;
    }
    return 10;
}

/* Convert proto fully-qualified type_name ".pkg.Msg.Nested" → "pkg_Msg_Nested" */
static void type_name_to_c(const char *type_name, char *out, size_t out_size)
{
    if (!type_name || !*type_name) { *out = '\0'; return; }
    const char *s = (*type_name == '.') ? type_name + 1 : type_name;
    char *d = out;
    size_t rem = out_size - 1;
    while (*s && rem > 0) {
        *d++ = (*s == '.') ? '_' : *s;
        s++; rem--;
    }
    *d = '\0';
}

/* Build C name prefix from package string ("my.pkg" → "my_pkg_") */
static void make_pkg_prefix(const char *pkg, char *out, size_t out_size)
{
    out[0] = '\0';
    if (!pkg || !*pkg) return;
    const char *p = pkg;
    char *d = out;
    size_t rem = out_size - 2;  /* reserve 2: underscore + NUL */
    while (*p && rem > 0) {
        *d++ = (*p == '.') ? '_' : *p;
        p++; rem--;
    }
    *d++ = '_';
    *d   = '\0';
}

/* Make a C identifier uppercase (for header guards, macros) */
static void to_upper_ident(const char *s, char *out, size_t out_size)
{
    char *d = out;
    size_t rem = out_size - 1;
    while (*s && rem > 0) {
        char c = *s++;
        *d++ = (char)(isalnum((unsigned char)c) ? toupper((unsigned char)c) : '_');
        rem--;
    }
    *d = '\0';
}

/* Append (tag << 3) varint to encoded-size calculation */
static int tag_encoded_size(int32_t tag)
{
    return varint_max_size((int64_t)tag << 3);
}

/* ========================================================================
 * Section 2 — Datatype table
 * ===================================================================== */

typedef struct {
    int         proto_type;
    int         int_size;
    const char *ctype;
    const char *pbtype;
    int         enc_size;
    int         data_size;
} datatype_t;

static const datatype_t datatypes[] = {
    /* Standard scalars */
    {FDP_TYPE_BOOL,    IS_DEFAULT, "bool",     "BOOL",    1,  4},
    {FDP_TYPE_DOUBLE,  IS_DEFAULT, "double",   "DOUBLE",  8,  8},
    {FDP_TYPE_FIXED32, IS_DEFAULT, "uint32_t", "FIXED32", 4,  4},
    {FDP_TYPE_FIXED64, IS_DEFAULT, "uint64_t", "FIXED64", 8,  8},
    {FDP_TYPE_FLOAT,   IS_DEFAULT, "float",    "FLOAT",   4,  4},
    {FDP_TYPE_INT32,   IS_DEFAULT, "int32_t",  "INT32",  10,  4},
    {FDP_TYPE_INT64,   IS_DEFAULT, "int64_t",  "INT64",  10,  8},
    {FDP_TYPE_SFIXED32,IS_DEFAULT, "int32_t",  "SFIXED32",4,  4},
    {FDP_TYPE_SFIXED64,IS_DEFAULT, "int64_t",  "SFIXED64",8,  8},
    {FDP_TYPE_SINT32,  IS_DEFAULT, "int32_t",  "SINT32",  5,  4},
    {FDP_TYPE_SINT64,  IS_DEFAULT, "int64_t",  "SINT64", 10,  8},
    {FDP_TYPE_UINT32,  IS_DEFAULT, "uint32_t", "UINT32",  5,  4},
    {FDP_TYPE_UINT64,  IS_DEFAULT, "uint64_t", "UINT64", 10,  8},
    /* Enum with int_size overrides */
    {FDP_TYPE_ENUM, IS_8,  "uint8_t",  "ENUM",  4, 1},
    {FDP_TYPE_ENUM, IS_16, "uint16_t", "ENUM",  4, 2},
    {FDP_TYPE_ENUM, IS_32, "uint32_t", "ENUM",  4, 4},
    {FDP_TYPE_ENUM, IS_64, "uint64_t", "ENUM",  4, 8},
    /* Integer types with size overrides */
    {FDP_TYPE_INT32, IS_8,  "int8_t",  "INT32", 10, 1},
    {FDP_TYPE_INT32, IS_16, "int16_t", "INT32", 10, 2},
    {FDP_TYPE_INT32, IS_32, "int32_t", "INT32", 10, 4},
    {FDP_TYPE_INT32, IS_64, "int64_t", "INT32", 10, 8},
    {FDP_TYPE_SINT32, IS_8,  "int8_t",  "SINT32", 2, 1},
    {FDP_TYPE_SINT32, IS_16, "int16_t", "SINT32", 3, 2},
    {FDP_TYPE_SINT32, IS_32, "int32_t", "SINT32", 5, 4},
    {FDP_TYPE_SINT32, IS_64, "int64_t", "SINT32",10, 8},
    {FDP_TYPE_UINT32, IS_8,  "uint8_t",  "UINT32", 2, 1},
    {FDP_TYPE_UINT32, IS_16, "uint16_t", "UINT32", 3, 2},
    {FDP_TYPE_UINT32, IS_32, "uint32_t", "UINT32", 5, 4},
    {FDP_TYPE_UINT32, IS_64, "uint64_t", "UINT32",10, 8},
    {FDP_TYPE_INT64, IS_8,  "int8_t",  "INT64", 10, 1},
    {FDP_TYPE_INT64, IS_16, "int16_t", "INT64", 10, 2},
    {FDP_TYPE_INT64, IS_32, "int32_t", "INT64", 10, 4},
    {FDP_TYPE_INT64, IS_64, "int64_t", "INT64", 10, 8},
    {FDP_TYPE_SINT64, IS_8,  "int8_t",  "SINT64",  2, 1},
    {FDP_TYPE_SINT64, IS_16, "int16_t", "SINT64",  3, 2},
    {FDP_TYPE_SINT64, IS_32, "int32_t", "SINT64",  5, 4},
    {FDP_TYPE_SINT64, IS_64, "int64_t", "SINT64", 10, 8},
    {FDP_TYPE_UINT64, IS_8,  "uint8_t",  "UINT64",  2, 1},
    {FDP_TYPE_UINT64, IS_16, "uint16_t", "UINT64",  3, 2},
    {FDP_TYPE_UINT64, IS_32, "uint32_t", "UINT64",  5, 4},
    {FDP_TYPE_UINT64, IS_64, "uint64_t", "UINT64", 10, 8},
};

static const datatype_t *lookup_datatype(int proto_type, int int_size)
{
    for (size_t i = 0; i < sizeof(datatypes)/sizeof(datatypes[0]); i++) {
        if (datatypes[i].proto_type == proto_type &&
            datatypes[i].int_size  == int_size)
            return &datatypes[i];
    }
    return NULL;
}

/* ========================================================================
 * Section 3 — Type registry (all C type names in the descriptor set)
 * ===================================================================== */

typedef struct { char proto_fqn[512]; char c_name[512]; bool is_enum; bool has_negative; } type_entry_t;
typedef struct { type_entry_t *entries; int count; } type_registry_t;

static void registry_add(type_registry_t *reg, const char *proto_fqn, const char *c_name)
{
    reg->entries = realloc(reg->entries, (size_t)(reg->count + 1) * sizeof(type_entry_t));
    snprintf(reg->entries[reg->count].proto_fqn, 512, "%s", proto_fqn);
    snprintf(reg->entries[reg->count].c_name,    512, "%s", c_name);
    reg->entries[reg->count].is_enum     = false;
    reg->entries[reg->count].has_negative = false;
    reg->count++;
}

static void registry_add_enum(type_registry_t *reg, const char *proto_fqn, const char *c_name,
                               const fdp_enum_t *e)
{
    registry_add(reg, proto_fqn, c_name);
    reg->entries[reg->count - 1].is_enum = true;
    for (int i = 0; i < e->values_count; i++) {
        if (e->values[i].number < 0) {
            reg->entries[reg->count - 1].has_negative = true;
            break;
        }
    }
}

static bool registry_enum_has_negative(const type_registry_t *reg, const char *type_name)
{
    const char *search = (type_name && *type_name == '.') ? type_name + 1 : type_name;
    for (int i = 0; i < reg->count; i++) {
        if (reg->entries[i].is_enum && strcmp(reg->entries[i].proto_fqn, search) == 0)
            return reg->entries[i].has_negative;
    }
    return true; /* default: assume negative (safe → use ENUM) */
}

static const char *registry_lookup(const type_registry_t *reg, const char *type_name)
{
    /* type_name may start with '.' */
    const char *search = (type_name && *type_name == '.') ? type_name + 1 : type_name;
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].proto_fqn, search) == 0)
            return reg->entries[i].c_name;
    }
    return NULL;
}

static void registry_free(type_registry_t *reg)
{
    free(reg->entries);
    reg->entries = NULL;
    reg->count   = 0;
}

/* Recursively register all message and enum types from a file */
static void register_file_types_r(type_registry_t *reg,
                                   const fdp_message_t *msgs, int nmsg,
                                   const fdp_enum_t    *enums, int nenum,
                                   const char *prefix)
{
    for (int i = 0; i < nenum; i++) {
        char fqn[512], cn[512];
        snprintf(fqn, 512, "%s%s", prefix, enums[i].name);
        type_name_to_c(fqn, cn, 512);
        registry_add_enum(reg, fqn, cn, &enums[i]);
    }
    for (int i = 0; i < nmsg; i++) {
        char fqn[512], cn[512];
        snprintf(fqn, 512, "%s%s", prefix, msgs[i].name);
        type_name_to_c(fqn, cn, 512);
        registry_add(reg, fqn, cn);
        /* recurse into nested types */
        char sub_prefix[512];
        snprintf(sub_prefix, 512, "%s%s.", prefix, msgs[i].name);
        register_file_types_r(reg,
            msgs[i].nested_types, msgs[i].nested_types_count,
            msgs[i].enum_types,   msgs[i].enum_types_count,
            sub_prefix);
    }
}

static type_registry_t build_type_registry(const fdp_set_t *set)
{
    type_registry_t reg = {0};
    for (int f = 0; f < set->files_count; f++) {
        const fdp_file_t *file = &set->files[f];
        char pkg_prefix[512] = "";
        if (file->package && *file->package) {
            snprintf(pkg_prefix, 512, "%s.", file->package);
        }
        register_file_types_r(&reg,
            file->message_types, file->message_types_count,
            file->enum_types,    file->enum_types_count,
            pkg_prefix);
    }
    return reg;
}

/* ========================================================================
 * Section 4 — Analyzed field/enum/message structures
 * ===================================================================== */

#define ALLOC_STATIC   1
#define ALLOC_CALLBACK 2
#define ALLOC_POINTER  3

#define RULE_REQUIRED  1
#define RULE_OPTIONAL  2
#define RULE_SINGULAR  3
#define RULE_REPEATED  4
#define RULE_FIXARRAY  5

typedef struct {
    char    name[256];
    int32_t tag;
    int     proto_type;
    /* Resolved: */
    char    ctype[256];
    char    pbtype[32];
    int     allocation;      /* ALLOC_* */
    int     rules;           /* RULE_* */
    int32_t max_size;        /* STRING / BYTES */
    int32_t max_count;       /* REPEATED / FIXARRAY */
    int     enc_size;        /* per-item wire enc size excl. tag, -1=unknown */
    bool    fixed_count;
    bool    fixed_length;    /* BYTES: fixed-length array */
    bool    default_has;
    char    callback_datatype[256];
    char    default_value[512]; /* from .proto, may be "" */
    bool    bytes_need_typedef;
    char    bytes_typename[256];  /* e.g. "MyMsg_field_t" */
    char    submsg_c_name[256];   /* For MESSAGE: the sub-message C type */
    bool    submsg_external;      /* submsg is in another file */
} afield_t;

typedef struct {
    char    full_name[512];
    const   fdp_message_t *desc;
    afield_t *fields;
    int       fields_count;
    bool      packed;
    bool      has_callbacks;
    nanopb_options_t options;
} amsg_t;

typedef struct {
    char     full_name[512];
    const    fdp_enum_t *desc;
    bool     packed;
    char     enum_ctype[32];   /* override ctype for packed enum, or "" */
    int      enc_size;         /* conservative encoded size per value */
    int      data_size;        /* sizeof() for enum values */
    /* Value entries (not sorted yet) */
    int      values_count;
    struct {
        char    name[256];  /* full C name */
        int32_t number;
    } *values;
    nanopb_options_t options;
} aenum_t;

/* ========================================================================
 * Section 5 — Field analysis
 * ===================================================================== */

/* Resolve the C name of a proto type reference.
 * type_name is like ".mypkg.Msg.Inner".
 * Returns the C name via the registry, or the dotted-to-underscore form. */
static void resolve_type_c_name(const char *type_name, const type_registry_t *reg, char *out, size_t out_size)
{
    const char *found = registry_lookup(reg, type_name);
    if (found) {
        snprintf(out, out_size, "%s", found);
    } else {
        /* Fallback: convert dots to underscores */
        type_name_to_c(type_name, out, out_size);
    }
}

static void analyze_field(afield_t *af, const fdp_field_t *fd,
                           const nanopb_options_t *msg_opts,
                           const options_file_t *file_opts,
                           const gen_options_t  *gen_opts,
                           const type_registry_t *reg,
                           const char *msg_c_name,
                           const char *dotname_prefix,
                           bool is_proto3)
{
    memset(af, 0, sizeof(*af));
    snprintf(af->name, sizeof(af->name), "%s", fd->name ? fd->name : "");
    af->tag = fd->number;
    af->proto_type = fd->type;

    /* Get merged options for this field */
    char dotname[512];
    snprintf(dotname, sizeof(dotname), "%s.%s", dotname_prefix, fd->name ? fd->name : "");
    nanopb_options_t fopts = options_get(file_opts, gen_opts, &fd->options, dotname);

    /* FT_INLINE → FT_STATIC + fixed_length (legacy) */
    if (fopts.field_type == FT_INLINE) {
        fopts.field_type  = FT_STATIC;
        fopts.fixed_length = true;
    }

    /* max_length overrides max_size for strings */
    if (fd->type == FDP_TYPE_STRING && fopts.has_max_length) {
        fopts.max_size = fopts.max_length + 1;
        fopts.has_max_size = true;
    }

    af->max_size  = fopts.has_max_size  ? fopts.max_size  : 0;
    af->max_count = fopts.has_max_count ? fopts.max_count : 0;
    af->default_has = fopts.default_has;
    af->fixed_length = fopts.fixed_length;
    af->fixed_count  = fopts.fixed_count;
    if (fopts.callback_datatype && *fopts.callback_datatype)
        snprintf(af->callback_datatype, sizeof(af->callback_datatype), "%s", fopts.callback_datatype);
    else
        snprintf(af->callback_datatype, sizeof(af->callback_datatype), "pb_callback_t");

    if (fd->default_value)
        snprintf(af->default_value, sizeof(af->default_value), "%s", fd->default_value);

    /* ---- Determine RULES ---- */
    int label = fd->label;
    if (fopts.has_label_override) label = fopts.label_override;

    bool proto3 = is_proto3 || fopts.proto3;
    bool can_be_static = true;

    if (label == FDP_LABEL_REPEATED) {
        af->rules = RULE_REPEATED;
        if (af->max_count == 0) can_be_static = false;
        else if (fopts.fixed_count) af->rules = RULE_FIXARRAY;
    } else if (label == FDP_LABEL_REQUIRED) {
        af->rules = RULE_REQUIRED;
    } else if (proto3) {
        /* proto3: messages have has_ field; optional keyword fields too */
        if (fd->type == FDP_TYPE_MESSAGE && !fopts.proto3_singular_msgs)
            af->rules = RULE_OPTIONAL;
        else if (fd->proto3_optional)
            af->rules = RULE_OPTIONAL;
        else
            af->rules = RULE_SINGULAR;
    } else {
        af->rules = RULE_OPTIONAL;
    }

    /* ---- can_be_static check ---- */
    if (fd->type == FDP_TYPE_STRING && af->max_size == 0) can_be_static = false;
    if (fd->type == FDP_TYPE_BYTES  && af->max_size == 0) can_be_static = false;
    if (af->rules == RULE_REPEATED  && af->max_count == 0) can_be_static = false;

    /* ---- Determine ALLOCATION ---- */
    int ftype = fopts.field_type;
    if (ftype == FT_DEFAULT) {
        ftype = can_be_static ? FT_STATIC : fopts.fallback_type;
        if (ftype == FT_DEFAULT) ftype = FT_CALLBACK;
    }
    if (ftype == FT_STATIC && !can_be_static) {
        fprintf(stderr, "Field '%s.%s' is static but max_size/max_count not given\n",
                msg_c_name, af->name);
        ftype = FT_CALLBACK;
    }

    switch (ftype) {
        case FT_STATIC:   af->allocation = ALLOC_STATIC;   break;
        case FT_POINTER:  af->allocation = ALLOC_POINTER;  break;
        case FT_CALLBACK:
        default:          af->allocation = ALLOC_CALLBACK; break;
    }

    /* ---- Determine C type / pb type ---- */
    if (fd->type == FDP_TYPE_ENUM) {
        bool has_neg = registry_enum_has_negative(reg, fd->type_name);
        snprintf(af->pbtype, sizeof(af->pbtype), "%s", has_neg ? "ENUM" : "UENUM");
        resolve_type_c_name(fd->type_name, reg, af->ctype, sizeof(af->ctype));
        af->enc_size = 10;  /* conservative */

        /* int_size override for enum fields */
        const datatype_t *dt = lookup_datatype(FDP_TYPE_ENUM, fopts.int_size);
        if (dt) {
            snprintf(af->ctype, sizeof(af->ctype), "%s", dt->ctype);
            af->enc_size = dt->enc_size;
        }
    } else if (fd->type == FDP_TYPE_STRING) {
        snprintf(af->pbtype, sizeof(af->pbtype), "STRING");
        snprintf(af->ctype,  sizeof(af->ctype),  "char");
        if (af->allocation == ALLOC_STATIC && af->max_size > 0) {
            af->enc_size = varint_max_size(af->max_size) + af->max_size - 1;
        } else {
            af->enc_size = -1;
        }
    } else if (fd->type == FDP_TYPE_BYTES) {
        if (fopts.fixed_length) {
            snprintf(af->pbtype, sizeof(af->pbtype), "FIXED_LENGTH_BYTES");
            snprintf(af->ctype,  sizeof(af->ctype),  "pb_byte_t");
            if (af->max_size > 0)
                af->enc_size = varint_max_size(af->max_size) + af->max_size;
        } else {
            snprintf(af->pbtype, sizeof(af->pbtype), "BYTES");
            snprintf(af->ctype,  sizeof(af->ctype),  "pb_bytes_array_t");
            if (af->allocation == ALLOC_STATIC && af->max_size > 0) {
                af->enc_size = varint_max_size(af->max_size) + af->max_size;
                /* Need a PB_BYTES_ARRAY_T typedef */
                af->bytes_need_typedef = true;
                snprintf(af->bytes_typename, sizeof(af->bytes_typename),
                         "%s_%s_t", msg_c_name, af->name);
                snprintf(af->ctype, sizeof(af->ctype), "%s", af->bytes_typename);
            }
        }
    } else if (fd->type == FDP_TYPE_MESSAGE) {
        snprintf(af->pbtype, sizeof(af->pbtype), "MESSAGE");
        resolve_type_c_name(fd->type_name, reg, af->ctype, sizeof(af->ctype));
        snprintf(af->submsg_c_name, sizeof(af->submsg_c_name), "%s", af->ctype);
        af->enc_size = -1; /* filled in during encode-size pass */
    } else {
        /* Scalar */
        const datatype_t *dt = NULL;
        if (fopts.int_size != IS_DEFAULT)
            dt = lookup_datatype(fd->type, fopts.int_size);
        if (!dt)
            dt = lookup_datatype(fd->type, IS_DEFAULT);
        if (dt) {
            snprintf(af->ctype,  sizeof(af->ctype),  "%s", dt->ctype);
            snprintf(af->pbtype, sizeof(af->pbtype), "%s", dt->pbtype);
            af->enc_size = dt->enc_size;
        } else {
            /* Unknown type — treat as callback */
            snprintf(af->ctype,  sizeof(af->ctype),  "uint8_t");
            snprintf(af->pbtype, sizeof(af->pbtype), "BYTES");
            af->allocation = ALLOC_CALLBACK;
            af->enc_size = -1;
        }
    }

    nanopb_options_free(&fopts);
}

/* ========================================================================
 * Section 6 — Collect all messages and enums from a file
 * ===================================================================== */

typedef struct {
    amsg_t  *msgs;
    int      msgs_count;
    aenum_t *enums;
    int      enums_count;
} file_items_t;

static void collect_enums_r(file_items_t *fi, const fdp_enum_t *enums, int count,
                             const char *prefix,
                             const options_file_t *file_opts,
                             const gen_options_t  *gen_opts)
{
    for (int i = 0; i < count; i++) {
        const fdp_enum_t *ed = &enums[i];
        aenum_t ae;
        memset(&ae, 0, sizeof(ae));
        snprintf(ae.full_name, sizeof(ae.full_name), "%s%s", prefix, ed->name ? ed->name : "");

        /* Merge options */
        nanopb_options_init(&ae.options);
        nanopb_options_merge(&ae.options, &ed->options);
        nanopb_options_t merged = options_get(file_opts, gen_opts, &ed->options, ae.full_name);
        nanopb_options_free(&ae.options);
        ae.options = merged;

        ae.packed  = ae.options.packed_enum;
        ae.enc_size = 4; /* default enum encoded size (conservative) */
        ae.data_size = 4;

        /* int_size override */
        if (ae.options.enum_intsize != IS_DEFAULT) {
            const datatype_t *dt = lookup_datatype(FDP_TYPE_ENUM, ae.options.enum_intsize);
            if (dt) {
                snprintf(ae.enum_ctype, sizeof(ae.enum_ctype), "%s", dt->ctype);
                ae.enc_size  = dt->enc_size;
                ae.data_size = dt->data_size;
            }
        }

        /* Collect values */
        ae.values = malloc((size_t)ed->values_count * sizeof(*ae.values));
        ae.values_count = ed->values_count;
        bool has_negative = false;
        for (int j = 0; j < ed->values_count; j++) {
            ae.values[j].number = ed->values[j].number;
            if (ae.values[j].number < 0) has_negative = true;
            /* Value name: prefix_EnumName_ValueName (long_names) or just ValueName */
            if (ae.options.long_names)
                snprintf(ae.values[j].name, sizeof(ae.values[j].name),
                         "%s_%s", ae.full_name, ed->values[j].name ? ed->values[j].name : "");
            else
                snprintf(ae.values[j].name, sizeof(ae.values[j].name),
                         "%s", ed->values[j].name ? ed->values[j].name : "");
        }
        /* Conservative enc_size when negative values present */
        if (has_negative && ae.enc_size < 10) ae.enc_size = 10;

        fi->enums = realloc(fi->enums, (size_t)(fi->enums_count + 1) * sizeof(aenum_t));
        fi->enums[fi->enums_count++] = ae;
    }
}

static void collect_messages_r(file_items_t *fi,
                                const fdp_message_t *msgs, int count,
                                const char *prefix,
                                const options_file_t *file_opts,
                                const gen_options_t  *gen_opts,
                                const type_registry_t *reg,
                                bool is_proto3);

static void collect_messages_r(file_items_t *fi,
                                const fdp_message_t *msgs, int count,
                                const char *prefix,
                                const options_file_t *file_opts,
                                const gen_options_t  *gen_opts,
                                const type_registry_t *reg,
                                bool is_proto3)
{
    for (int i = 0; i < count; i++) {
        const fdp_message_t *md = &msgs[i];

        /* Skip map entry helper messages */
        if (md->map_entry) continue;
        /* Skip messages with skip_message option */
        nanopb_options_t msg_opts_check;
        nanopb_options_init(&msg_opts_check);
        nanopb_options_merge(&msg_opts_check, &md->options);
        bool skip = msg_opts_check.skip_message;
        nanopb_options_free(&msg_opts_check);
        if (skip) {
            /* Still recurse for nested types */
            char sub_prefix[512];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s%s_", prefix, md->name ? md->name : "");
            collect_enums_r(fi, md->enum_types, md->enum_types_count, sub_prefix, file_opts, gen_opts);
            collect_messages_r(fi, md->nested_types, md->nested_types_count, sub_prefix, file_opts, gen_opts, reg, is_proto3);
            continue;
        }

        amsg_t am;
        memset(&am, 0, sizeof(am));
        snprintf(am.full_name, sizeof(am.full_name), "%s%s", prefix, md->name ? md->name : "");
        am.desc = md;

        /* Merge message options */
        nanopb_options_init(&am.options);
        nanopb_options_t merged_opts = options_get(file_opts, gen_opts, &md->options, am.full_name);
        nanopb_options_free(&am.options);
        am.options = merged_opts;
        am.packed = am.options.packed_struct;

        bool msg_proto3 = is_proto3 || am.options.proto3;

        /* Analyze fields */
        am.fields_count = md->fields_count;
        am.fields = calloc((size_t)md->fields_count, sizeof(afield_t));
        for (int j = 0; j < md->fields_count; j++) {
            analyze_field(&am.fields[j], &md->fields[j], &am.options,
                          file_opts, gen_opts, reg, am.full_name, am.full_name, msg_proto3);
            if (am.fields[j].allocation == ALLOC_CALLBACK)
                am.has_callbacks = true;
        }

        /* Sort fields by tag (FIELDLIST must be sorted by tag) */
        if (am.options.sort_by_tag) {
            for (int a = 0; a < am.fields_count - 1; a++) {
                for (int b = a + 1; b < am.fields_count; b++) {
                    if (am.fields[a].tag > am.fields[b].tag) {
                        afield_t tmp = am.fields[a];
                        am.fields[a] = am.fields[b];
                        am.fields[b] = tmp;
                    }
                }
            }
        }

        fi->msgs = realloc(fi->msgs, (size_t)(fi->msgs_count + 1) * sizeof(amsg_t));
        fi->msgs[fi->msgs_count++] = am;

        /* Recurse into nested types */
        char sub_prefix[512];
        snprintf(sub_prefix, sizeof(sub_prefix), "%s_", am.full_name);
        collect_enums_r(fi, md->enum_types, md->enum_types_count, sub_prefix, file_opts, gen_opts);
        collect_messages_r(fi, md->nested_types, md->nested_types_count, sub_prefix, file_opts, gen_opts, reg, msg_proto3);
    }
}

static file_items_t collect_file_items(const fdp_file_t *file,
                                       const options_file_t *file_opts,
                                       const gen_options_t  *gen_opts,
                                       const type_registry_t *reg)
{
    file_items_t fi = {0};
    char pkg_prefix[512] = "";
    make_pkg_prefix(file->package, pkg_prefix, sizeof(pkg_prefix));

    bool is_proto3 = file->options.proto3;

    collect_enums_r(&fi, file->enum_types, file->enum_types_count,
                    pkg_prefix, file_opts, gen_opts);
    collect_messages_r(&fi, file->message_types, file->message_types_count,
                       pkg_prefix, file_opts, gen_opts, reg, is_proto3);
    return fi;
}

static void file_items_free(file_items_t *fi)
{
    for (int i = 0; i < fi->msgs_count; i++) {
        free(fi->msgs[i].fields);
        nanopb_options_free(&fi->msgs[i].options);
    }
    free(fi->msgs);
    for (int i = 0; i < fi->enums_count; i++) {
        free(fi->enums[i].values);
        nanopb_options_free(&fi->enums[i].options);
    }
    free(fi->enums);
}

/* ========================================================================
 * Section 7 — Topological sort of messages
 * ===================================================================== */

/* Simple dependency sort: messages that reference others as STATIC fields
 * must come after those others in the output. */
static int *topo_sort_messages(const file_items_t *fi)
{
    int n = fi->msgs_count;
    int *order = malloc((size_t)n * sizeof(int));
    bool *emitted = calloc((size_t)n, sizeof(bool));

    int out = 0;
    while (out < n) {
        bool progress = false;
        for (int i = 0; i < n; i++) {
            if (emitted[i]) continue;
            /* Check all STATIC MESSAGE fields */
            bool ready = true;
            for (int j = 0; j < fi->msgs[i].fields_count; j++) {
                const afield_t *af = &fi->msgs[i].fields[j];
                if (af->proto_type != FDP_TYPE_MESSAGE) continue;
                if (af->allocation != ALLOC_STATIC) continue;
                /* Find the dependency in our list */
                for (int k = 0; k < n; k++) {
                    if (k == i) continue;
                    if (strcmp(fi->msgs[k].full_name, af->submsg_c_name) == 0) {
                        if (!emitted[k]) { ready = false; break; }
                    }
                }
                if (!ready) break;
            }
            if (ready) {
                order[out++] = i;
                emitted[i] = true;
                progress = true;
            }
        }
        if (!progress) {
            /* Cycle — just emit remaining in order */
            for (int i = 0; i < n; i++) {
                if (!emitted[i]) {
                    order[out++] = i;
                    emitted[i] = true;
                }
            }
        }
    }
    free(emitted);
    return order;
}

/* ========================================================================
 * Section 8 — Encoded size helpers
 * ===================================================================== */

/* Encoded size result: numeric value (-1 = unknown/callback) */
typedef struct {
    int      value;    /* -1 = unknown/callback, -2 = has symbolic dep, >= 0 = numeric */
    int      overhead; /* For value=-2: known numeric overhead for this field (tag+lenprefix) */
    char     symbol[256]; /* For value=-2: just the dep symbol name (e.g., "SubMsg_size") */
} enc_size_t;

static enc_size_t field_encoded_size(const afield_t *af, const file_items_t *fi)
{
    enc_size_t r = {-1, 0, ""};
    if (af->allocation != ALLOC_STATIC) return r;

    int item_enc = af->enc_size;

    if (af->proto_type == FDP_TYPE_MESSAGE) {
        /* Overhead is known: tag bytes + 5 worst-case length-prefix varint */
        r.value    = -2;
        r.overhead = tag_encoded_size(af->tag) + 5;
        snprintf(r.symbol, sizeof(r.symbol), "%s_size", af->submsg_c_name);
        return r;
    }

    if (af->proto_type == FDP_TYPE_ENUM) {
        /* Look up enum size */
        for (int i = 0; i < fi->enums_count; i++) {
            if (strcmp(fi->enums[i].full_name, af->ctype) == 0) {
                item_enc = fi->enums[i].enc_size;
                break;
            }
        }
        if (item_enc == -1) item_enc = 10; /* conservative */
    }

    if (item_enc < 0) return r;

    int ts = tag_encoded_size(af->tag);
    int per_item = item_enc + ts;

    if (af->rules == RULE_REPEATED || af->rules == RULE_FIXARRAY) {
        if (af->max_count <= 0) return r;
        int total = per_item * af->max_count;
        if (af->max_count == 1) total += 1;
        r.value = total;
    } else {
        r.value = per_item;
    }
    return r;
}

/* Compute total encoded size for a message.
 * Returns -1 if any field has unknown size (callback),
 * -2 if it depends on a sub-message symbol (fills symbol_out + numeric_out),
 * or the total numeric size.
 * Only the FIRST symbolic dependency is supported; multiple sub-message fields
 * cause a fallback to -1 (unknown). */
static int message_encoded_size(const amsg_t *am, const file_items_t *fi,
                                 char *symbol_out, size_t symbol_out_size,
                                 int *numeric_out)
{
    symbol_out[0] = '\0';
    *numeric_out  = 0;
    int  total    = 0;
    bool has_dep  = false;

    for (int i = 0; i < am->fields_count; i++) {
        enc_size_t es = field_encoded_size(&am->fields[i], fi);
        if (es.value == -1) return -1;
        if (es.value == -2) {
            total += es.overhead;
            if (!has_dep) {
                snprintf(symbol_out, symbol_out_size, "%s", es.symbol);
                has_dep = true;
            } else {
                /* Multiple symbolic deps: fall back to unknown */
                return -1;
            }
        } else {
            total += es.value;
        }
    }
    if (has_dep) {
        *numeric_out = total;
        return -2;
    }
    return total;
}

/* ========================================================================
 * Section 9 — Initializer generation
 * ===================================================================== */

static bool is_enum_pbtype(const char *pbtype)
{
    return strcmp(pbtype, "ENUM") == 0 || strcmp(pbtype, "UENUM") == 0;
}

static void field_zero_init(const afield_t *af, strbuf_t *sb)
{
    if (af->allocation == ALLOC_CALLBACK) {
        if (strcmp(af->callback_datatype, "pb_callback_t") == 0)
            strbuf_append(sb, "{{NULL}, NULL}");
        else if (af->callback_datatype[strlen(af->callback_datatype)-1] == '*')
            strbuf_append(sb, "NULL");
        else
            strbuf_append(sb, "{0}");
        return;
    }
    if (af->allocation == ALLOC_POINTER) {
        if (af->rules == RULE_REPEATED) strbuf_append(sb, "0, NULL");
        else strbuf_append(sb, "NULL");
        return;
    }

    /* STATIC */
    char inner[128];
    if (strcmp(af->pbtype, "MESSAGE") == 0)
        snprintf(inner, sizeof(inner), "%s_init_zero", af->ctype);
    else if (strcmp(af->pbtype, "STRING") == 0)
        snprintf(inner, sizeof(inner), "\"\"");
    else if (strcmp(af->pbtype, "BYTES") == 0)
        snprintf(inner, sizeof(inner), "{0, {0}}");
    else if (strcmp(af->pbtype, "FIXED_LENGTH_BYTES") == 0)
        snprintf(inner, sizeof(inner), "{0}");
    else if (is_enum_pbtype(af->pbtype)) {
        /* use _EnumType_MIN */
        snprintf(inner, sizeof(inner), "_%s_MIN", af->ctype);
    } else {
        snprintf(inner, sizeof(inner), "0");
    }

    if (af->rules == RULE_OPTIONAL) {
        strbuf_appendf(sb, "false, %s", inner);
    } else if (af->rules == RULE_REPEATED) {
        /* 0, {inner, inner, ...} */
        strbuf_appendf(sb, "0, {");
        for (int k = 0; k < af->max_count; k++) {
            if (k) strbuf_append(sb, ", ");
            strbuf_append(sb, inner);
        }
        strbuf_append(sb, "}");
    } else if (af->rules == RULE_FIXARRAY) {
        strbuf_append(sb, "{");
        for (int k = 0; k < af->max_count; k++) {
            if (k) strbuf_append(sb, ", ");
            strbuf_append(sb, inner);
        }
        strbuf_append(sb, "}");
    } else {
        strbuf_append(sb, inner);
    }
}

static void field_default_init(const afield_t *af, strbuf_t *sb)
{
    /* MESSAGE fields always use _init_default for the "default" initializer */
    if (strcmp(af->pbtype, "MESSAGE") == 0) {
        char inner[128];
        if (af->allocation == ALLOC_CALLBACK) {
            /* callback sub-message: same as zero init */
            field_zero_init(af, sb);
            return;
        }
        snprintf(inner, sizeof(inner), "%s_init_default", af->ctype);
        if (af->rules == RULE_OPTIONAL) {
            strbuf_appendf(sb, "false, %s", inner);
        } else if (af->rules == RULE_REPEATED) {
            strbuf_appendf(sb, "0, {");
            for (int k = 0; k < af->max_count; k++) {
                if (k) strbuf_append(sb, ", ");
                strbuf_append(sb, inner);
            }
            strbuf_append(sb, "}");
        } else if (af->rules == RULE_FIXARRAY) {
            strbuf_append(sb, "{");
            for (int k = 0; k < af->max_count; k++) {
                if (k) strbuf_append(sb, ", ");
                strbuf_append(sb, inner);
            }
            strbuf_append(sb, "}");
        } else {
            strbuf_append(sb, inner);
        }
        return;
    }

    /* For non-MESSAGE types: use zero initializer unless we have a known scalar default */
    if (af->default_value[0] == '\0' || af->allocation != ALLOC_STATIC) {
        field_zero_init(af, sb);
        return;
    }
    if (strcmp(af->pbtype, "BYTES")   == 0 ||
        strcmp(af->pbtype, "FIXED_LENGTH_BYTES") == 0) {
        field_zero_init(af, sb);
        return;
    }

    char inner[512];
    if (strcmp(af->pbtype, "STRING") == 0) {
        snprintf(inner, sizeof(inner), "\"%s\"", af->default_value);
    } else if (strcmp(af->pbtype, "BOOL") == 0) {
        snprintf(inner, sizeof(inner), "%s",
                 (strcmp(af->default_value, "true") == 0) ? "true" : "false");
    } else if (is_enum_pbtype(af->pbtype)) {
        snprintf(inner, sizeof(inner), "%s", af->default_value);
    } else if (strcmp(af->pbtype, "FLOAT") == 0) {
        snprintf(inner, sizeof(inner), "%sf", af->default_value);
    } else {
        snprintf(inner, sizeof(inner), "%s", af->default_value);
    }

    if (af->rules == RULE_OPTIONAL) {
        bool has_default = af->default_has;
        strbuf_appendf(sb, "%s, %s", has_default ? "true" : "false", inner);
    } else {
        strbuf_append(sb, inner);
    }
}

static void message_initializer(const amsg_t *am, strbuf_t *sb, bool zero)
{
    if (am->fields_count == 0) {
        strbuf_append(sb, "{0}");
        return;
    }
    strbuf_append(sb, "{");
    for (int i = 0; i < am->fields_count; i++) {
        if (i) strbuf_append(sb, ", ");
        if (zero)
            field_zero_init(&am->fields[i], sb);
        else
            field_default_init(&am->fields[i], sb);
    }
    strbuf_append(sb, "}");
}

/* ========================================================================
 * Section 10 — Code generation: header
 * ===================================================================== */

/* Determine X-macro params (avoid collision with field names) */
static void choose_macro_params(const amsg_t *am, char *xparam, char *aparam)
{
    const char *xp = "X", *ap = "a";
    bool x_clash, a_clash;
    do {
        x_clash = false;
        for (int i = 0; i < am->fields_count; i++)
            if (strcmp(am->fields[i].name, xp) == 0) { x_clash = true; break; }
        if (x_clash) {
            static char xbuf[8];
            snprintf(xbuf, sizeof(xbuf), "%s_", xp); xp = xbuf;
        }
    } while (x_clash);
    do {
        a_clash = false;
        for (int i = 0; i < am->fields_count; i++)
            if (strcmp(am->fields[i].name, ap) == 0) { a_clash = true; break; }
        if (a_clash) {
            static char abuf[8];
            snprintf(abuf, sizeof(abuf), "%s_", ap); ap = abuf;
        }
    } while (a_clash);
    snprintf(xparam, 8, "%s", xp);
    snprintf(aparam, 8, "%s", ap);
}

static const char *rules_str(int rules)
{
    switch (rules) {
        case RULE_REQUIRED: return "REQUIRED";
        case RULE_OPTIONAL: return "OPTIONAL";
        case RULE_SINGULAR: return "SINGULAR";
        case RULE_REPEATED: return "REPEATED";
        case RULE_FIXARRAY: return "FIXARRAY";
        default:            return "OPTIONAL";
    }
}

static const char *alloc_str(int alloc)
{
    switch (alloc) {
        case ALLOC_STATIC:   return "STATIC";
        case ALLOC_CALLBACK: return "CALLBACK";
        case ALLOC_POINTER:  return "POINTER";
        default:             return "STATIC";
    }
}

static void gen_enum(const aenum_t *ae, strbuf_t *h)
{
    strbuf_appendf(h, "typedef enum _%s", ae->full_name);
    if (ae->enum_ctype[0])
        strbuf_appendf(h, " : %s", ae->enum_ctype);
    strbuf_append(h, " {");
    strbuf_append(h, "\n");
    for (int i = 0; i < ae->values_count; i++) {
        const char *comma = (i < ae->values_count - 1) ? "," : "";
        strbuf_appendf(h, "    %s = %d%s\n",
                       ae->values[i].name, ae->values[i].number, comma);
    }
    strbuf_append(h, "}");
    if (ae->packed) strbuf_append(h, " pb_packed");
    strbuf_appendf(h, " %s;\n", ae->full_name);
}

static void gen_enum_aux(const aenum_t *ae, strbuf_t *h)
{
    if (ae->values_count == 0) return;
    /* Sort values to find min/max */
    int min_idx = 0, max_idx = 0;
    for (int i = 1; i < ae->values_count; i++) {
        if (ae->values[i].number < ae->values[min_idx].number) min_idx = i;
        if (ae->values[i].number > ae->values[max_idx].number) max_idx = i;
    }
    strbuf_appendf(h, "#define _%s_MIN %s\n",
                   ae->full_name, ae->values[min_idx].name);
    strbuf_appendf(h, "#define _%s_MAX %s\n",
                   ae->full_name, ae->values[max_idx].name);
    strbuf_appendf(h, "#define _%s_ARRAYSIZE ((%s)(%s+1))\n",
                   ae->full_name, ae->full_name, ae->values[max_idx].name);
}

/* Generate the struct definition for one message */
static void gen_message_struct(const amsg_t *am, strbuf_t *h)
{
    strbuf_appendf(h, "typedef struct _%s {", am->full_name);
    strbuf_append(h, "\n");
    if (am->fields_count == 0) {
        strbuf_append(h, "    char dummy_field;\n");
    }
    for (int i = 0; i < am->fields_count; i++) {
        const afield_t *af = &am->fields[i];
        if (af->allocation == ALLOC_CALLBACK) {
            strbuf_appendf(h, "    %s %s;\n", af->callback_datatype, af->name);
        } else if (af->allocation == ALLOC_POINTER) {
            if (af->rules == RULE_REPEATED) {
                strbuf_appendf(h, "    pb_size_t %s_count;\n", af->name);
            }
            if (strcmp(af->pbtype, "MESSAGE") == 0)
                strbuf_appendf(h, "    struct _%s *%s;\n", af->ctype, af->name);
            else
                strbuf_appendf(h, "    %s *%s;\n", af->ctype, af->name);
        } else {
            /* STATIC */
            if (af->rules == RULE_OPTIONAL)
                strbuf_appendf(h, "    bool has_%s;\n", af->name);
            else if (af->rules == RULE_REPEATED)
                strbuf_appendf(h, "    pb_size_t %s_count;\n", af->name);

            if (strcmp(af->pbtype, "STRING") == 0) {
                if (af->rules == RULE_REPEATED) {
                    strbuf_appendf(h, "    char %s[%d][%d];\n",
                                   af->name, af->max_count, af->max_size);
                } else if (af->rules == RULE_FIXARRAY) {
                    strbuf_appendf(h, "    char %s[%d][%d];\n",
                                   af->name, af->max_count, af->max_size);
                } else {
                    strbuf_appendf(h, "    char %s[%d];\n", af->name, af->max_size);
                }
            } else if (strcmp(af->pbtype, "BYTES") == 0) {
                if (af->rules == RULE_REPEATED || af->rules == RULE_FIXARRAY)
                    strbuf_appendf(h, "    %s %s[%d];\n", af->ctype, af->name, af->max_count);
                else
                    strbuf_appendf(h, "    %s %s;\n", af->ctype, af->name);
            } else if (strcmp(af->pbtype, "FIXED_LENGTH_BYTES") == 0) {
                if (af->rules == RULE_REPEATED || af->rules == RULE_FIXARRAY)
                    strbuf_appendf(h, "    pb_byte_t %s[%d][%d];\n",
                                   af->name, af->max_count, af->max_size);
                else
                    strbuf_appendf(h, "    pb_byte_t %s[%d];\n", af->name, af->max_size);
            } else if (strcmp(af->pbtype, "MESSAGE") == 0) {
                if (af->rules == RULE_REPEATED || af->rules == RULE_FIXARRAY)
                    strbuf_appendf(h, "    %s %s[%d];\n", af->ctype, af->name, af->max_count);
                else
                    strbuf_appendf(h, "    %s %s;\n", af->ctype, af->name);
            } else {
                /* Scalars and enums */
                if (af->rules == RULE_REPEATED || af->rules == RULE_FIXARRAY)
                    strbuf_appendf(h, "    %s %s[%d];\n", af->ctype, af->name, af->max_count);
                else
                    strbuf_appendf(h, "    %s %s;\n", af->ctype, af->name);
            }
        }
    }
    strbuf_append(h, "}");
    if (am->packed) strbuf_append(h, " pb_packed");
    strbuf_appendf(h, " %s;\n", am->full_name);
}

static void gen_fieldlist(const amsg_t *am, strbuf_t *h)
{
    char xp[8], ap[8];
    choose_macro_params(am, xp, ap);

    if (am->fields_count == 0) {
        strbuf_appendf(h, "#define %s_FIELDLIST(%s, %s)\n", am->full_name, xp, ap);
        return;
    }

    strbuf_appendf(h, "#define %s_FIELDLIST(%s, %s) \\\n", am->full_name, xp, ap);

    /* Sort fields by tag for the FIELDLIST (even if struct is not sorted) */
    int *idx = malloc((size_t)am->fields_count * sizeof(int));
    for (int i = 0; i < am->fields_count; i++) idx[i] = i;
    for (int a = 0; a < am->fields_count - 1; a++)
        for (int b = a + 1; b < am->fields_count; b++)
            if (am->fields[idx[a]].tag > am->fields[idx[b]].tag) {
                int t = idx[a]; idx[a] = idx[b]; idx[b] = t;
            }

    for (int k = 0; k < am->fields_count; k++) {
        const afield_t *af = &am->fields[idx[k]];
        bool last = (k == am->fields_count - 1);
        /* Each argument needs a trailing comma (required by nanopb X-macro) */
        char alloc_c[12], rules_c[12], pbtype_c[16], name_c[260];
        snprintf(alloc_c,  sizeof(alloc_c),  "%s,", alloc_str(af->allocation));
        snprintf(rules_c,  sizeof(rules_c),  "%s,", rules_str(af->rules));
        snprintf(pbtype_c, sizeof(pbtype_c), "%s,", af->pbtype);
        snprintf(name_c,   sizeof(name_c),   "%s,", af->name);
        strbuf_appendf(h, "%s(%s, %-9s %-9s %-9s %-16s %3d)%s\n",
                       xp, ap,
                       alloc_c, rules_c, pbtype_c, name_c,
                       af->tag,
                       last ? "" : " \\");
    }
    free(idx);
}

/* ========================================================================
 * Section 11 — Header file generation
 * ===================================================================== */

static void generate_header_content(const fdp_file_t *file,
                                    const file_items_t *fi,
                                    const gen_options_t *gen_opts,
                                    const char *headername,
                                    strbuf_t *h)
{
    /* Preamble */
    strbuf_append(h, "/* Automatically generated nanopb header */\n");
    if (gen_opts->notimestamp)
        strbuf_appendf(h, "/* Generated by %s */\n\n", NANOPB_VERSION_STRING);

    /* Header guard — match Python generator: prepend package when present */
    char guard[512];
    if (file->package && *file->package) {
        char guard_base[512];
        snprintf(guard_base, sizeof(guard_base), "%s_%s", file->package, headername);
        to_upper_ident(guard_base, guard, sizeof(guard));
    } else {
        to_upper_ident(headername, guard, sizeof(guard));
    }
    strbuf_appendf(h, "#ifndef PB_%s_INCLUDED\n", guard);
    strbuf_appendf(h, "#define PB_%s_INCLUDED\n", guard);

    /* pb.h include */
    strbuf_appendf(h, gen_opts->libformat ? gen_opts->libformat : "#include <%s>", "pb.h");
    strbuf_append(h, "\n");

    /* Dependencies: #include for each .proto dependency (excluding google/nanopb) */
    const char *excludes[] = {"nanopb.proto", "google/protobuf/descriptor.proto", NULL};
    for (int d = 0; d < file->dependencies_count; d++) {
        const char *dep = file->dependencies[d];
        bool excluded = false;
        for (int e = 0; excludes[e]; e++)
            if (strcmp(dep, excludes[e]) == 0) { excluded = true; break; }
        if (excluded) continue;
        /* Remove extension, add .pb.h */
        char depbase[512];
        snprintf(depbase, sizeof(depbase), "%s", dep);
        /* Strip .proto */
        size_t dlen = strlen(depbase);
        if (dlen > 6 && strcmp(depbase + dlen - 6, ".proto") == 0)
            depbase[dlen - 6] = '\0';
        char incname[512];
        snprintf(incname, sizeof(incname), "%s%s%s",
                 depbase,
                 gen_opts->extension ? gen_opts->extension : ".pb",
                 gen_opts->header_extension ? gen_opts->header_extension : ".h");
        if (gen_opts->strip_path) {
            /* Only keep basename */
            const char *slash = strrchr(incname, '/');
            if (!slash) slash = strrchr(incname, '\\');
            if (slash) memmove(incname, slash+1, strlen(slash));
        }
        strbuf_appendf(h, gen_opts->genformat ? gen_opts->genformat : "#include \"%s\"", incname);
        strbuf_append(h, "\n");
    }
    strbuf_append(h, "\n");

    /* Version check */
    strbuf_append(h, "#if PB_PROTO_HEADER_VERSION != 40\n");
    strbuf_append(h, "#error Regenerate this file with the current version of nanopb generator.\n");
    strbuf_append(h, "#endif\n\n");

    /* ---- Enum definitions ---- */
    if (fi->enums_count > 0) {
        strbuf_append(h, "/* Enum definitions */\n");
        for (int i = 0; i < fi->enums_count; i++) {
            gen_enum(&fi->enums[i], h);
            strbuf_append(h, "\n");
        }
    }

    /* ---- Struct definitions ---- */
    if (fi->msgs_count > 0) {
        strbuf_append(h, "/* Struct definitions */\n");
        /* Bytes typedefs first */
        int *topo = topo_sort_messages(fi);
        for (int k = 0; k < fi->msgs_count; k++) {
            const amsg_t *am = &fi->msgs[topo[k]];
            /* Output PB_BYTES_ARRAY_T typedefs */
            for (int j = 0; j < am->fields_count; j++) {
                if (am->fields[j].bytes_need_typedef) {
                    strbuf_appendf(h, "typedef PB_BYTES_ARRAY_T(%d) %s;\n",
                                   am->fields[j].max_size,
                                   am->fields[j].bytes_typename);
                }
            }
            if (am->packed)
                strbuf_append(h, "PB_PACKED_STRUCT_START\n");
            gen_message_struct(am, h);
            if (am->packed)
                strbuf_append(h, "PB_PACKED_STRUCT_END\n");
            strbuf_append(h, "\n");
        }
        free(topo);
        strbuf_append(h, "\n");
    }

    /* ---- extern "C" open ---- */
    strbuf_append(h, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");

    /* ---- Enum helper constants ---- */
    if (fi->enums_count > 0) {
        strbuf_append(h, "/* Helper constants for enums */\n");
        for (int i = 0; i < fi->enums_count; i++) {
            gen_enum_aux(&fi->enums[i], h);
            strbuf_append(h, "\n");
        }
        /* Enum type defines for message fields */
        for (int i = 0; i < fi->msgs_count; i++) {
            const amsg_t *am = &fi->msgs[i];
            bool any = false;
            for (int j = 0; j < am->fields_count; j++) {
                if (is_enum_pbtype(am->fields[j].pbtype)) {
                    if (!any) { any = true; }
                    strbuf_appendf(h, "#define %s_%s_ENUMTYPE %s\n",
                                   am->full_name, am->fields[j].name, am->fields[j].ctype);
                }
            }
            if (any) strbuf_append(h, "\n");
        }
        strbuf_append(h, "\n");
    }

    /* ---- Initializers ---- */
    if (fi->msgs_count > 0) {
        strbuf_append(h, "/* Initializer values for message structs */\n");
        for (int i = 0; i < fi->msgs_count; i++) {
            strbuf_t tmp = {0};
            strbuf_appendf(&tmp, "%s_init_default", fi->msgs[i].full_name);
            strbuf_appendf(h, "#define %-40s ", tmp.data);
            strbuf_free(&tmp);
            message_initializer(&fi->msgs[i], h, false);
            strbuf_append(h, "\n");
        }
        for (int i = 0; i < fi->msgs_count; i++) {
            strbuf_t tmp = {0};
            strbuf_appendf(&tmp, "%s_init_zero", fi->msgs[i].full_name);
            strbuf_appendf(h, "#define %-40s ", tmp.data);
            strbuf_free(&tmp);
            message_initializer(&fi->msgs[i], h, true);
            strbuf_append(h, "\n");
        }
        strbuf_append(h, "\n");
    }

    /* ---- Field tags ---- */
    if (fi->msgs_count > 0) {
        strbuf_append(h, "/* Field tags (for use in manual encoding/decoding) */\n");
        int *topo = topo_sort_messages(fi);
        for (int k = 0; k < fi->msgs_count; k++) {
            const amsg_t *am = &fi->msgs[topo[k]];
            for (int j = 0; j < am->fields_count; j++) {
                char tagname[512];
                snprintf(tagname, sizeof(tagname), "%s_%s_tag",
                         am->full_name, am->fields[j].name);
                strbuf_appendf(h, "#define %-40s %d\n", tagname, am->fields[j].tag);
            }
        }
        free(topo);
        strbuf_append(h, "\n");
    }

    /* ---- Struct field encoding specs ---- */
    if (fi->msgs_count > 0) {
        strbuf_append(h, "/* Struct field encoding specification for nanopb */\n");
        for (int i = 0; i < fi->msgs_count; i++) {
            const amsg_t *am = &fi->msgs[i];
            gen_fieldlist(am, h);
            if (am->has_callbacks)
                strbuf_appendf(h, "#define %s_CALLBACK pb_default_field_callback\n", am->full_name);
            else
                strbuf_appendf(h, "#define %s_CALLBACK NULL\n", am->full_name);
            strbuf_appendf(h, "#define %s_DEFAULT NULL\n", am->full_name);

            /* MSGTYPE defines for submessage fields (all allocations) */
            for (int j = 0; j < am->fields_count; j++) {
                if (strcmp(am->fields[j].pbtype, "MESSAGE") == 0) {
                    strbuf_appendf(h, "#define %s_%s_MSGTYPE %s\n",
                                   am->full_name, am->fields[j].name, am->fields[j].ctype);
                }
            }
            strbuf_append(h, "\n");
        }
        for (int i = 0; i < fi->msgs_count; i++)
            strbuf_appendf(h, "extern const pb_msgdesc_t %s_msg;\n", fi->msgs[i].full_name);
        strbuf_append(h, "\n");

        /* Backwards-compat _fields defines */
        strbuf_append(h, "/* Defines for backwards compatibility with code written before nanopb-0.4.0 */\n");
        for (int i = 0; i < fi->msgs_count; i++)
            strbuf_appendf(h, "#define %s_fields &%s_msg\n",
                           fi->msgs[i].full_name, fi->msgs[i].full_name);
        strbuf_append(h, "\n");

        /* Encoded sizes — match Python output order:
         * 1) "depends on runtime parameters" comments in message order
         * 2) All known-size #defines sorted alphabetically (including MAX_SIZE) */
        strbuf_append(h, "/* Maximum encoded size of messages (where known) */\n");

        /* Pass 1: emit runtime-params comments; collect known-size lines */
        char size_lines[64][256];
        int  nsize_lines = 0;
        char max_size_id[256] = "";   /* identifier of msg with max numeric size */
        int  max_numeric_val  = -1;

        for (int i = 0; i < fi->msgs_count; i++) {
            const amsg_t *am = &fi->msgs[i];
            char sym[512] = "";
            int  numeric  = 0;
            char size_def[256];
            snprintf(size_def, sizeof(size_def), "%s_size", am->full_name);
            int sz = message_encoded_size(am, fi, sym, sizeof(sym), &numeric);
            if (sz == -1) {
                strbuf_appendf(h, "/* %s depends on runtime parameters */\n", size_def);
            } else if (sz >= 0 && nsize_lines < 64) {
                snprintf(size_lines[nsize_lines++], 256, "#define %-40s %d", size_def, sz);
                if (sz > max_numeric_val) {
                    max_numeric_val = sz;
                    snprintf(max_size_id, sizeof(max_size_id), "%s", size_def);
                }
            }
            /* sz == -2 (guard-conditional dep): handled as runtime for now */
        }

        /* Pass 2: sort and emit defines (including MAX_SIZE if any known sizes) */
        if (nsize_lines > 0) {
            char max_define[256];
            snprintf(max_define, 256, "#define %-40s %s", guard, max_size_id);
            /* Append "_MAX_SIZE" to guard in the define */
            {
                char max_def_name[256];
                snprintf(max_def_name, sizeof(max_def_name), "%s_MAX_SIZE", guard);
                snprintf(max_define, 256, "#define %-40s %s", max_def_name, max_size_id);
            }
            /* Collect all in one array with MAX_SIZE included */
            char all[65][256];
            int  nall = 0;
            for (int i = 0; i < nsize_lines; i++)
                snprintf(all[nall++], 256, "%s", size_lines[i]);
            snprintf(all[nall++], 256, "%s", max_define);
            /* Bubble-sort alphabetically */
            for (int a = 0; a < nall - 1; a++)
                for (int b = a + 1; b < nall; b++)
                    if (strcmp(all[a], all[b]) > 0) {
                        char tmp[256];
                        memcpy(tmp,    all[a], 256);
                        memcpy(all[a], all[b], 256);
                        memcpy(all[b], tmp,    256);
                    }
            for (int i = 0; i < nall; i++)
                strbuf_appendf(h, "%s\n", all[i]);
        }
        strbuf_append(h, "\n");
    }

    /* ---- extern "C" close ---- */
    strbuf_append(h, "#ifdef __cplusplus\n} /* extern \"C\" */\n#endif\n\n");
    strbuf_append(h, "#endif\n");
}

/* ========================================================================
 * Section 12 — Source file generation
 * ===================================================================== */

static void generate_source_content(const file_items_t *fi,
                                    const gen_options_t *gen_opts,
                                    const char *headername,
                                    strbuf_t *s)
{
    strbuf_append(s, "/* Automatically generated nanopb constant definitions */\n");
    if (gen_opts->notimestamp)
        strbuf_appendf(s, "/* Generated by %s */\n\n", NANOPB_VERSION_STRING);

    /* #include the header */
    strbuf_appendf(s, gen_opts->genformat ? gen_opts->genformat : "#include \"%s\"", headername);
    strbuf_append(s, "\n");

    strbuf_append(s, "#if PB_PROTO_HEADER_VERSION != 40\n");
    strbuf_append(s, "#error Regenerate this file with the current version of nanopb generator.\n");
    strbuf_append(s, "#endif\n\n");

    for (int i = 0; i < fi->msgs_count; i++) {
        strbuf_appendf(s, "PB_BIND(%s, %s, AUTO)\n\n\n", fi->msgs[i].full_name, fi->msgs[i].full_name);
    }
    /* Python yields '\n' for each enum's enum_to_string_definition() and enum_validate()
     * even when those functions return "" (disabled) — emit matching blank lines */
    for (int i = 0; i < fi->enums_count; i++) {
        strbuf_append(s, "\n");  /* enum_to_string_definition() + '\n' */
    }
    for (int i = 0; i < fi->enums_count; i++) {
        strbuf_append(s, "\n");  /* enum_validate() + '\n' */
    }
    strbuf_append(s, "\n");
}

/* ========================================================================
 * Section 13 — Public entry point
 * ===================================================================== */

bool generate_file(const fdp_file_t   *file,
                   const fdp_set_t    *set,
                   const options_file_t *file_opts,
                   const gen_options_t  *gen_opts,
                   strbuf_t *header,
                   strbuf_t *source)
{
    /* Build type registry from all files in the set */
    type_registry_t reg = build_type_registry(set);

    /* Collect and analyze all items from this file */
    file_items_t fi = collect_file_items(file, file_opts, gen_opts, &reg);

    /* Compute output header file name */
    char headername[512] = "";
    if (file->name) {
        /* Strip .proto extension */
        snprintf(headername, sizeof(headername), "%s", file->name);
        size_t nlen = strlen(headername);
        if (nlen > 6 && strcmp(headername + nlen - 6, ".proto") == 0)
            headername[nlen - 6] = '\0';
        /* Append .pb.h */
        const char *ext   = gen_opts->extension        ? gen_opts->extension        : ".pb";
        const char *hext  = gen_opts->header_extension ? gen_opts->header_extension : ".h";
        strncat(headername, ext,  sizeof(headername) - strlen(headername) - 1);
        strncat(headername, hext, sizeof(headername) - strlen(headername) - 1);
        if (gen_opts->strip_path) {
            const char *slash = strrchr(headername, '/');
            if (!slash) slash = strrchr(headername, '\\');
            if (slash) memmove(headername, slash + 1, strlen(slash));
        }
    }

    generate_header_content(file, &fi, gen_opts, headername, header);
    generate_source_content(&fi, gen_opts, headername, source);

    file_items_free(&fi);
    registry_free(&reg);
    return true;
}
