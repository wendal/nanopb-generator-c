/* Minimal FileDescriptorSet decoder.
 * Decodes only the fields needed for nanopb code generation (minimal scope).
 */
#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- FieldDescriptorProto.Label ---------------------------------------- */
#define LABEL_OPTIONAL 1
#define LABEL_REQUIRED 2
#define LABEL_REPEATED 3

/* ---- FieldDescriptorProto.Type ----------------------------------------- */
#define TYPE_DOUBLE   1
#define TYPE_FLOAT    2
#define TYPE_INT64    3
#define TYPE_UINT64   4
#define TYPE_INT32    5
#define TYPE_FIXED64  6
#define TYPE_FIXED32  7
#define TYPE_BOOL     8
#define TYPE_STRING   9
#define TYPE_GROUP    10
#define TYPE_MESSAGE  11
#define TYPE_BYTES    12
#define TYPE_UINT32   13
#define TYPE_ENUM     14
#define TYPE_SFIXED32 15
#define TYPE_SFIXED64 16
#define TYPE_SINT32   17
#define TYPE_SINT64   18

/* ---- NanoPBOptions.FieldType ------------------------------------------- */
#define FT_DEFAULT  0
#define FT_CALLBACK 1
#define FT_POINTER  4
#define FT_STATIC   2
#define FT_IGNORE   3
#define FT_INLINE   5

/* ---- NanoPBOptions.IntSize --------------------------------------------- */
#define IS_DEFAULT 0
#define IS_8        8
#define IS_16      16
#define IS_32      32
#define IS_64      64

/* ---- DescriptorSize (DS_AUTO etc.) ------------------------------------- */
#define DS_AUTO 0
#define DS_1    1
#define DS_2    2
#define DS_4    4
#define DS_8    8

/* ---- NanoPB field options (subset used in minimal scope) --------------- */
typedef struct {
    int32_t  max_size;        bool has_max_size;
    int32_t  max_length;      bool has_max_length;
    int32_t  max_count;       bool has_max_count;
    int32_t  int_size;
    int32_t  field_type;      /* FT_* */
    int32_t  fallback_type;
    bool     long_names;
    bool     packed_struct;
    bool     packed_enum;
    bool     skip_message;
    bool     no_unions;
    bool     anonymous_oneof;
    bool     proto3;
    bool     proto3_singular_msgs;
    bool     fixed_length;
    bool     fixed_count;
    bool     default_has;
    bool     sort_by_tag;
    bool     discard_unused_automatic_types;
    bool     discard_deprecated;
    char    *callback_datatype;
    int32_t  descriptorsize;  /* DS_* */
    int32_t  label_override;  bool has_label_override;
    int32_t  enum_intsize;
} nanopb_options_t;

/* ---- EnumValueDescriptorProto ------------------------------------------ */
typedef struct {
    char    *name;
    int32_t  number;
    bool     deprecated;
} fdp_enum_value_t;

/* ---- EnumDescriptorProto ----------------------------------------------- */
typedef struct {
    char              *name;
    fdp_enum_value_t  *values;
    int                values_count;
    nanopb_options_t   options;   /* nanopb extension on EnumOptions */
    bool               deprecated;
} fdp_enum_t;

/* ---- FieldDescriptorProto ---------------------------------------------- */
typedef struct {
    char    *name;
    int32_t  number;
    int32_t  label;        /* LABEL_* */
    int32_t  type;         /* TYPE_* */
    char    *type_name;    /* for message/enum types, e.g. ".pkg.MyMsg" */
    char    *extendee;
    char    *default_value;
    bool     has_oneof_index;
    int32_t  oneof_index;
    bool     proto3_optional;
    nanopb_options_t options;  /* nanopb extension on FieldOptions */
    bool     deprecated;
} fdp_field_t;

/* ---- OneofDescriptorProto ---------------------------------------------- */
typedef struct {
    char *name;
    nanopb_options_t options;
} fdp_oneof_t;

/* ---- DescriptorProto (forward declared for nesting) --------------------- */
typedef struct fdp_message fdp_message_t;

struct fdp_message {
    char           *name;
    fdp_field_t    *fields;
    int             fields_count;
    fdp_message_t  *nested_types;
    int             nested_types_count;
    fdp_enum_t     *enum_types;
    int             enum_types_count;
    fdp_oneof_t    *oneofs;
    int             oneofs_count;
    nanopb_options_t options;  /* nanopb extension on MessageOptions */
    bool            map_entry;
    bool            deprecated;
};

/* ---- FileDescriptorProto ----------------------------------------------- */
typedef struct {
    char           *name;
    char           *package;
    char          **dependencies;
    int             dependencies_count;
    fdp_message_t  *message_types;
    int             message_types_count;
    fdp_enum_t     *enum_types;
    int             enum_types_count;
    char           *syntax;
    nanopb_options_t options;  /* nanopb extension on FileOptions */
} fdp_file_t;

/* ---- FileDescriptorSet -------------------------------------------------- */
typedef struct {
    fdp_file_t *files;
    int         files_count;
} fdp_set_t;

/* ---- Decoder API -------------------------------------------------------- */

/* Decode a FileDescriptorSet from binary data.
 * Returns an allocated fdp_set_t, or NULL on error.
 * Caller must free with fdp_set_free(). */
fdp_set_t *fdp_decode(const uint8_t *data, size_t size);

/* Merge options from src into dst (src values override if set). */
void nanopb_options_init(nanopb_options_t *opts);
void nanopb_options_merge(nanopb_options_t *dst, const nanopb_options_t *src);
void nanopb_options_free(nanopb_options_t *opts);

/* Free all resources held by the set. */
void fdp_set_free(fdp_set_t *set);

/* ---- FDP_TYPE_* and FDP_LABEL_* aliases (used in generator code) ------- */
#define FDP_LABEL_OPTIONAL  LABEL_OPTIONAL
#define FDP_LABEL_REQUIRED  LABEL_REQUIRED
#define FDP_LABEL_REPEATED  LABEL_REPEATED

#define FDP_TYPE_DOUBLE   TYPE_DOUBLE
#define FDP_TYPE_FLOAT    TYPE_FLOAT
#define FDP_TYPE_INT64    TYPE_INT64
#define FDP_TYPE_UINT64   TYPE_UINT64
#define FDP_TYPE_INT32    TYPE_INT32
#define FDP_TYPE_FIXED64  TYPE_FIXED64
#define FDP_TYPE_FIXED32  TYPE_FIXED32
#define FDP_TYPE_BOOL     TYPE_BOOL
#define FDP_TYPE_STRING   TYPE_STRING
#define FDP_TYPE_GROUP    TYPE_GROUP
#define FDP_TYPE_MESSAGE  TYPE_MESSAGE
#define FDP_TYPE_BYTES    TYPE_BYTES
#define FDP_TYPE_UINT32   TYPE_UINT32
#define FDP_TYPE_ENUM     TYPE_ENUM
#define FDP_TYPE_SFIXED32 TYPE_SFIXED32
#define FDP_TYPE_SFIXED64 TYPE_SFIXED64
#define FDP_TYPE_SINT32   TYPE_SINT32
#define FDP_TYPE_SINT64   TYPE_SINT64

#endif /* DESCRIPTOR_H */
