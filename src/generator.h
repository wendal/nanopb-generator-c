#ifndef GENERATOR_H
#define GENERATOR_H

#include "descriptor.h"
#include "options.h"
#include "strbuf.h"
#include <stdbool.h>

#define GENERATOR_VERSION "nanopb-generator-c 0.1.0"
/* Emit headers/sources compatible with nanopb 0.4.x */
#define NANOPB_VERSION_STRING "nanopb-0.4.9.1"

/*
 * Generate .pb.h and .pb.c content for a single FileDescriptorProto.
 *
 * file      - the FileDescriptorProto to generate for
 * set       - the full FileDescriptorSet (for cross-file dependency resolution)
 * file_opts - parsed .options file for this proto, or NULL
 * gen_opts  - CLI/global generation options
 * header    - output string buffer for .pb.h content
 * source    - output string buffer for .pb.c content
 *
 * Returns true on success.
 */
bool generate_file(const fdp_file_t   *file,
                   const fdp_set_t    *set,
                   const options_file_t *file_opts,
                   const gen_options_t  *gen_opts,
                   strbuf_t *header,
                   strbuf_t *source);

#endif /* GENERATOR_H */
