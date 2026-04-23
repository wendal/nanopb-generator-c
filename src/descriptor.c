#include "descriptor.h"
#include "pb_reader.h"
#include "names.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Dynamic array helpers ----------------------------------------------- */

#define DA_PUSH(arr, count, item, type) do { \
    (arr) = realloc((arr), ((count) + 1) * sizeof(type)); \
    if (!(arr)) { fprintf(stderr, "OOM\n"); exit(1); } \
    (arr)[(count)++] = (item); \
} while (0)

/* ---- NanoPBOptions defaults ---------------------------------------------- */

void nanopb_options_init(nanopb_options_t *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->field_type      = FT_DEFAULT;
    opts->fallback_type   = FT_CALLBACK;
    opts->int_size        = IS_DEFAULT;
    opts->long_names      = true;
    opts->sort_by_tag     = true;
    opts->discard_unused_automatic_types = true;
    opts->descriptorsize  = DS_AUTO;
    opts->callback_datatype = names_strdup("pb_callback_t");
    opts->enum_intsize    = IS_DEFAULT;
}

void nanopb_options_merge(nanopb_options_t *dst, const nanopb_options_t *src)
{
    if (src->has_max_size)    { dst->max_size  = src->max_size;  dst->has_max_size  = true; }
    if (src->has_max_length)  { dst->max_length = src->max_length; dst->has_max_length = true; }
    if (src->has_max_count)   { dst->max_count = src->max_count; dst->has_max_count = true; }
    if (src->int_size)        dst->int_size       = src->int_size;
    if (src->field_type)      dst->field_type     = src->field_type;
    if (src->fallback_type)   dst->fallback_type  = src->fallback_type;
    if (!src->long_names)     dst->long_names     = false;
    if (src->packed_struct)   dst->packed_struct  = true;
    if (src->packed_enum)     dst->packed_enum    = true;
    if (src->skip_message)    dst->skip_message   = true;
    if (src->no_unions)       dst->no_unions      = true;
    if (src->anonymous_oneof) dst->anonymous_oneof = true;
    if (src->proto3)          dst->proto3         = true;
    if (src->proto3_singular_msgs) dst->proto3_singular_msgs = true;
    if (src->fixed_length)    dst->fixed_length   = true;
    if (src->fixed_count)     dst->fixed_count    = true;
    if (src->default_has)     dst->default_has    = true;
    if (!src->sort_by_tag)    dst->sort_by_tag    = false;
    if (!src->discard_unused_automatic_types) dst->discard_unused_automatic_types = false;
    if (src->discard_deprecated) dst->discard_deprecated = true;
    if (src->callback_datatype && src->callback_datatype[0]) {
        free(dst->callback_datatype);
        dst->callback_datatype = names_strdup(src->callback_datatype);
    }
    if (src->descriptorsize)  dst->descriptorsize = src->descriptorsize;
    if (src->has_label_override) {
        dst->label_override = src->label_override;
        dst->has_label_override = true;
    }
    if (src->enum_intsize)    dst->enum_intsize   = src->enum_intsize;
}

void nanopb_options_free(nanopb_options_t *opts)
{
    free(opts->callback_datatype);
    opts->callback_datatype = NULL;
}

/* ---- Read a pb string (length-delimited) as a heap-allocated C string ---- */

static char *read_string(pb_reader_t *r)
{
    const uint8_t *data;
    size_t size;
    if (!pb_read_len(r, &data, &size)) return NULL;
    return names_strndup((const char *)data, size);
}

/* ---- nanopb extension field decoder (extension 1010) --------------------- */
/* Decodes the NanoPBOptions sub-message embedded as an extension. */

static void decode_nanopb_options(pb_reader_t *r, nanopb_options_t *opts)
{
    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        uint64_t v;
        switch (field_num) {
            case 1:  /* max_size */
                pb_read_varint(r, &v);
                opts->max_size = (int32_t)v; opts->has_max_size = true; break;
            case 14: /* max_length */
                pb_read_varint(r, &v);
                opts->max_length = (int32_t)v; opts->has_max_length = true; break;
            case 2:  /* max_count */
                pb_read_varint(r, &v);
                opts->max_count = (int32_t)v; opts->has_max_count = true; break;
            case 7:  /* int_size */
                pb_read_varint(r, &v); opts->int_size = (int32_t)v; break;
            case 34: /* enum_intsize */
                pb_read_varint(r, &v); opts->enum_intsize = (int32_t)v; break;
            case 3:  /* type (FieldType) */
                pb_read_varint(r, &v); opts->field_type = (int32_t)v; break;
            case 4:  /* long_names */
                pb_read_varint(r, &v); opts->long_names = v ? true : false; break;
            case 5:  /* packed_struct */
                pb_read_varint(r, &v); opts->packed_struct = v ? true : false; break;
            case 10: /* packed_enum */
                pb_read_varint(r, &v); opts->packed_enum = v ? true : false; break;
            case 6:  /* skip_message */
                pb_read_varint(r, &v); opts->skip_message = v ? true : false; break;
            case 8:  /* no_unions */
                pb_read_varint(r, &v); opts->no_unions = v ? true : false; break;
            case 9:  /* msgid (ignored in minimal) */
                pb_read_varint(r, &v); break;
            case 11: /* anonymous_oneof */
                pb_read_varint(r, &v); opts->anonymous_oneof = v ? true : false; break;
            case 12: /* proto3 */
                pb_read_varint(r, &v); opts->proto3 = v ? true : false; break;
            case 21: /* proto3_singular_msgs */
                pb_read_varint(r, &v); opts->proto3_singular_msgs = v ? true : false; break;
            case 15: /* fixed_length */
                pb_read_varint(r, &v); opts->fixed_length = v ? true : false; break;
            case 16: /* fixed_count */
                pb_read_varint(r, &v); opts->fixed_count = v ? true : false; break;
            case 23: /* default_has */
                pb_read_varint(r, &v); opts->default_has = v ? true : false; break;
            case 28: /* sort_by_tag */
                pb_read_varint(r, &v); opts->sort_by_tag = v ? true : false; break;
            case 29: /* fallback_type */
                pb_read_varint(r, &v); opts->fallback_type = (int32_t)v; break;
            case 33: /* discard_unused_automatic_types */
                pb_read_varint(r, &v);
                opts->discard_unused_automatic_types = v ? true : false; break;
            case 35: /* discard_deprecated */
                pb_read_varint(r, &v); opts->discard_deprecated = v ? true : false; break;
            case 18: { /* callback_datatype */
                char *s = read_string(r);
                if (s) { free(opts->callback_datatype); opts->callback_datatype = s; }
                break;
            }
            case 20: /* descriptorsize (DS_*) */
                pb_read_varint(r, &v); opts->descriptorsize = (int32_t)v; break;
            case 31: /* label_override */
                pb_read_varint(r, &v);
                opts->label_override = (int32_t)v;
                opts->has_label_override = true; break;
            default:
                pb_skip_field(r, wire_type); break;
        }
    }
}

/* Decode a FieldOptions or FileOptions or MessageOptions or EnumOptions.
 * We only care about the nanopb extension at field number 1010. */
static void decode_options_with_nanopb_ext(pb_reader_t *r, nanopb_options_t *opts)
{
    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        if (field_num == 1010 && wire_type == PB_WIRE_LEN) {
            pb_reader_t sub;
            pb_enter_submessage(r, &sub);
            decode_nanopb_options(&sub, opts);
        } else {
            /* Also detect map_entry (MessageOptions field 7) and deprecated (field 3) */
            if (field_num == 7 && wire_type == PB_WIRE_VARINT) {
                /* map_entry stored in caller's opts->... handled separately */
            }
            pb_skip_field(r, wire_type);
        }
    }
}

/* Decode MessageOptions, additionally capturing map_entry (field 7) */
static void decode_message_options(pb_reader_t *r, nanopb_options_t *opts, bool *map_entry, bool *deprecated)
{
    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        if (field_num == 1010 && wire_type == PB_WIRE_LEN) {
            pb_reader_t sub;
            pb_enter_submessage(r, &sub);
            decode_nanopb_options(&sub, opts);
        } else if (field_num == 7 && wire_type == PB_WIRE_VARINT) {
            uint64_t v; pb_read_varint(r, &v);
            if (map_entry) *map_entry = v ? true : false;
        } else if (field_num == 3 && wire_type == PB_WIRE_VARINT) {
            uint64_t v; pb_read_varint(r, &v);
            if (deprecated) *deprecated = v ? true : false;
        } else {
            pb_skip_field(r, wire_type);
        }
    }
}

/* Decode FieldOptions additionally capturing deprecated (field 3) */
static void decode_field_options(pb_reader_t *r, nanopb_options_t *opts, bool *deprecated)
{
    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        if (field_num == 1010 && wire_type == PB_WIRE_LEN) {
            pb_reader_t sub;
            pb_enter_submessage(r, &sub);
            decode_nanopb_options(&sub, opts);
        } else if (field_num == 3 && wire_type == PB_WIRE_VARINT) {
            uint64_t v; pb_read_varint(r, &v);
            if (deprecated) *deprecated = v ? true : false;
        } else {
            pb_skip_field(r, wire_type);
        }
    }
}

/* ---- EnumValueDescriptorProto decoder ----------------------------------- */

static fdp_enum_value_t decode_enum_value(pb_reader_t *r)
{
    fdp_enum_value_t val;
    memset(&val, 0, sizeof(val));
    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        switch (field_num) {
            case 1: val.name   = read_string(r); break;
            case 2: { uint64_t v; pb_read_varint(r, &v); val.number = (int32_t)v; break; }
            default: pb_skip_field(r, wire_type); break;
        }
    }
    return val;
}

/* ---- EnumDescriptorProto decoder ---------------------------------------- */

static fdp_enum_t decode_enum(pb_reader_t *r)
{
    fdp_enum_t e;
    memset(&e, 0, sizeof(e));
    nanopb_options_init(&e.options);

    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        switch (field_num) {
            case 1: e.name = read_string(r); break;
            case 2: {
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_enum_value_t v = decode_enum_value(&sub);
                DA_PUSH(e.values, e.values_count, v, fdp_enum_value_t);
                break;
            }
            case 3: { /* options */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                decode_options_with_nanopb_ext(&sub, &e.options);
                break;
            }
            default: pb_skip_field(r, wire_type); break;
        }
    }
    return e;
}

/* ---- FieldDescriptorProto decoder --------------------------------------- */

static fdp_field_t decode_field(pb_reader_t *r)
{
    fdp_field_t f;
    memset(&f, 0, sizeof(f));
    nanopb_options_init(&f.options);

    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        switch (field_num) {
            case 1: f.name          = read_string(r); break;
            case 2: f.extendee      = read_string(r); break;
            case 3: { uint64_t v; pb_read_varint(r, &v); f.number = (int32_t)v; break; }
            case 4: { uint64_t v; pb_read_varint(r, &v); f.label  = (int32_t)v; break; }
            case 5: { uint64_t v; pb_read_varint(r, &v); f.type   = (int32_t)v; break; }
            case 6: f.type_name     = read_string(r); break;
            case 7: f.default_value = read_string(r); break;
            case 8: { /* options */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                decode_field_options(&sub, &f.options, &f.deprecated);
                break;
            }
            case 9: {
                uint64_t v; pb_read_varint(r, &v);
                f.oneof_index = (int32_t)v;
                f.has_oneof_index = true;
                break;
            }
            case 17: { uint64_t v; pb_read_varint(r, &v); f.proto3_optional = v ? true : false; break; }
            default: pb_skip_field(r, wire_type); break;
        }
    }
    return f;
}

/* ---- OneofDescriptorProto decoder --------------------------------------- */

static fdp_oneof_t decode_oneof(pb_reader_t *r)
{
    fdp_oneof_t o;
    memset(&o, 0, sizeof(o));
    nanopb_options_init(&o.options);

    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        switch (field_num) {
            case 1: o.name = read_string(r); break;
            case 2: { /* OneofOptions */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                decode_options_with_nanopb_ext(&sub, &o.options);
                break;
            }
            default: pb_skip_field(r, wire_type); break;
        }
    }
    return o;
}

/* ---- DescriptorProto (message) decoder ---------------------------------- */

static fdp_message_t decode_message(pb_reader_t *r);  /* forward */

static fdp_message_t decode_message(pb_reader_t *r)
{
    fdp_message_t m;
    memset(&m, 0, sizeof(m));
    nanopb_options_init(&m.options);

    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        switch (field_num) {
            case 1: m.name = read_string(r); break;
            case 2: { /* field */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_field_t f = decode_field(&sub);
                DA_PUSH(m.fields, m.fields_count, f, fdp_field_t);
                break;
            }
            case 3: { /* nested_type */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_message_t nm = decode_message(&sub);
                DA_PUSH(m.nested_types, m.nested_types_count, nm, fdp_message_t);
                break;
            }
            case 4: { /* enum_type */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_enum_t e = decode_enum(&sub);
                DA_PUSH(m.enum_types, m.enum_types_count, e, fdp_enum_t);
                break;
            }
            case 7: { /* options */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                decode_message_options(&sub, &m.options, &m.map_entry, &m.deprecated);
                break;
            }
            case 8: { /* oneof_decl */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_oneof_t o = decode_oneof(&sub);
                DA_PUSH(m.oneofs, m.oneofs_count, o, fdp_oneof_t);
                break;
            }
            default: pb_skip_field(r, wire_type); break;
        }
    }
    return m;
}

/* ---- FileDescriptorProto decoder ---------------------------------------- */

static fdp_file_t decode_file(pb_reader_t *r)
{
    fdp_file_t f;
    memset(&f, 0, sizeof(f));
    nanopb_options_init(&f.options);

    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(r, &field_num, &wire_type)) {
        switch (field_num) {
            case 1:  f.name    = read_string(r); break;
            case 2:  f.package = read_string(r); break;
            case 3: {
                char *dep = read_string(r);
                DA_PUSH(f.dependencies, f.dependencies_count, dep, char *);
                break;
            }
            case 4: { /* message_type */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_message_t m = decode_message(&sub);
                DA_PUSH(f.message_types, f.message_types_count, m, fdp_message_t);
                break;
            }
            case 5: { /* enum_type */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                fdp_enum_t e = decode_enum(&sub);
                DA_PUSH(f.enum_types, f.enum_types_count, e, fdp_enum_t);
                break;
            }
            case 8: { /* options (FileOptions) */
                pb_reader_t sub;
                pb_enter_submessage(r, &sub);
                decode_options_with_nanopb_ext(&sub, &f.options);
                break;
            }
            case 12: f.syntax  = read_string(r); break;
            default: pb_skip_field(r, wire_type); break;
        }
    }
    /* Detect proto3 from syntax field */
    if (f.syntax && strcmp(f.syntax, "proto3") == 0) {
        f.options.proto3 = true;
    }
    return f;
}

/* ---- FileDescriptorSet decoder ------------------------------------------ */

fdp_set_t *fdp_decode(const uint8_t *data, size_t size)
{
    fdp_set_t *set = calloc(1, sizeof(fdp_set_t));
    if (!set) return NULL;

    pb_reader_t r;
    pb_reader_init(&r, data, size);

    uint32_t field_num;
    uint8_t wire_type;
    while (pb_read_tag(&r, &field_num, &wire_type)) {
        if (field_num == 1 && wire_type == PB_WIRE_LEN) {
            pb_reader_t sub;
            pb_enter_submessage(&r, &sub);
            fdp_file_t f = decode_file(&sub);
            DA_PUSH(set->files, set->files_count, f, fdp_file_t);
        } else {
            pb_skip_field(&r, wire_type);
        }
    }
    return set;
}

/* ---- Free helpers -------------------------------------------------------- */

static void free_enum(fdp_enum_t *e)
{
    free(e->name);
    for (int i = 0; i < e->values_count; i++) free(e->values[i].name);
    free(e->values);
    nanopb_options_free(&e->options);
}

static void free_field(fdp_field_t *f)
{
    free(f->name);
    free(f->extendee);
    free(f->type_name);
    free(f->default_value);
    nanopb_options_free(&f->options);
}

static void free_message(fdp_message_t *m)
{
    free(m->name);
    for (int i = 0; i < m->fields_count; i++) free_field(&m->fields[i]);
    free(m->fields);
    for (int i = 0; i < m->nested_types_count; i++) free_message(&m->nested_types[i]);
    free(m->nested_types);
    for (int i = 0; i < m->enum_types_count; i++) free_enum(&m->enum_types[i]);
    free(m->enum_types);
    for (int i = 0; i < m->oneofs_count; i++) {
        free(m->oneofs[i].name);
        nanopb_options_free(&m->oneofs[i].options);
    }
    free(m->oneofs);
    nanopb_options_free(&m->options);
}

static void free_file(fdp_file_t *f)
{
    free(f->name);
    free(f->package);
    for (int i = 0; i < f->dependencies_count; i++) free(f->dependencies[i]);
    free(f->dependencies);
    for (int i = 0; i < f->message_types_count; i++) free_message(&f->message_types[i]);
    free(f->message_types);
    for (int i = 0; i < f->enum_types_count; i++) free_enum(&f->enum_types[i]);
    free(f->enum_types);
    free(f->syntax);
    nanopb_options_free(&f->options);
}

void fdp_set_free(fdp_set_t *set)
{
    if (!set) return;
    for (int i = 0; i < set->files_count; i++) free_file(&set->files[i]);
    free(set->files);
    free(set);
}
