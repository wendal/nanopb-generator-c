/* Minimal protobuf binary wire reader — no external dependencies.
 * Supports varint (wire type 0) and length-delimited (wire type 2) fields,
 * which cover everything in FileDescriptorSet needed by nanopb-generator-c.
 */
#ifndef PB_READER_H
#define PB_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire types */
#define PB_WIRE_VARINT  0
#define PB_WIRE_64BIT   1
#define PB_WIRE_LEN     2
#define PB_WIRE_32BIT   5

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} pb_reader_t;

void pb_reader_init(pb_reader_t *r, const uint8_t *data, size_t size);

/* Read a base-128 varint. Returns false on error/EOF. */
bool pb_read_varint(pb_reader_t *r, uint64_t *value);

/* Read a tag byte, decode to field_number and wire_type.
 * Returns false on EOF (not an error at top level). */
bool pb_read_tag(pb_reader_t *r, uint32_t *field_number, uint8_t *wire_type);

/* Read a length-delimited payload. *data points into the original buffer
 * (zero-copy). Returns false on error. */
bool pb_read_len(pb_reader_t *r, const uint8_t **data, size_t *size);

/* Skip a field whose tag has already been read. */
bool pb_skip_field(pb_reader_t *r, uint8_t wire_type);

/* Read a sub-message into a child reader (slice of parent's buffer). */
bool pb_enter_submessage(pb_reader_t *r, pb_reader_t *child);

/* Read a zigzag-encoded sint32/sint64. */
int32_t  pb_zigzag32(uint32_t n);
int64_t  pb_zigzag64(uint64_t n);

#endif /* PB_READER_H */
