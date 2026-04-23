#include "pb_reader.h"
#include <string.h>

void pb_reader_init(pb_reader_t *r, const uint8_t *data, size_t size)
{
    r->data = data;
    r->size = size;
    r->pos  = 0;
}

bool pb_read_varint(pb_reader_t *r, uint64_t *value)
{
    uint64_t result = 0;
    int shift = 0;
    while (r->pos < r->size) {
        uint8_t b = r->data[r->pos++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
        {
            *value = result;
            return true;
        }
        shift += 7;
        if (shift >= 64) return false; /* overflow */
    }
    return false; /* truncated */
}

bool pb_read_tag(pb_reader_t *r, uint32_t *field_number, uint8_t *wire_type)
{
    if (r->pos >= r->size) return false;
    uint64_t tag;
    if (!pb_read_varint(r, &tag)) return false;
    *wire_type    = (uint8_t)(tag & 0x07);
    *field_number = (uint32_t)(tag >> 3);
    return true;
}

bool pb_read_len(pb_reader_t *r, const uint8_t **data, size_t *size)
{
    uint64_t len;
    if (!pb_read_varint(r, &len)) return false;
    if (r->pos + (size_t)len > r->size) return false;
    *data = r->data + r->pos;
    *size = (size_t)len;
    r->pos += (size_t)len;
    return true;
}

bool pb_skip_field(pb_reader_t *r, uint8_t wire_type)
{
    uint64_t v;
    const uint8_t *dummy_data;
    size_t dummy_size;
    switch (wire_type) {
        case PB_WIRE_VARINT: return pb_read_varint(r, &v);
        case PB_WIRE_64BIT:
            if (r->pos + 8 > r->size) return false;
            r->pos += 8;
            return true;
        case PB_WIRE_LEN: return pb_read_len(r, &dummy_data, &dummy_size);
        case PB_WIRE_32BIT:
            if (r->pos + 4 > r->size) return false;
            r->pos += 4;
            return true;
        default: return false;
    }
}

bool pb_enter_submessage(pb_reader_t *r, pb_reader_t *child)
{
    const uint8_t *data;
    size_t size;
    if (!pb_read_len(r, &data, &size)) return false;
    pb_reader_init(child, data, size);
    return true;
}

int32_t pb_zigzag32(uint32_t n)
{
    return (int32_t)((n >> 1) ^ -(int32_t)(n & 1));
}

int64_t pb_zigzag64(uint64_t n)
{
    return (int64_t)((n >> 1) ^ -(int64_t)(n & 1));
}
