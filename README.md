# nanopb-generator-c

A C implementation of the [nanopb](https://github.com/nanopb/nanopb) Protocol Buffer code generator. Generates `.pb.h` and `.pb.c` files compatible with nanopb 0.4.x from pre-compiled `.pb` (FileDescriptorSet binary) files.

## Features

- Generates nanopb-compatible `.pb.h` and `.pb.c` files
- Supports all scalar types, strings, bytes, enums, and nested messages
- Handles optional (`has_`), required, and repeated fields
- Static (`FT_STATIC`) and callback (`FT_CALLBACK`) field allocation
- Reads `.options` files for field configuration (`max_size`, `max_count`, etc.)
- Cross-platform: Windows, Linux, macOS
- Built with CMake

## Requirements

- CMake 3.15 or later
- A C99-compatible compiler (GCC, Clang, MSVC)
- Python 3 + `protobuf` package (for compiling `.proto` files to `.pb`)
- `protoc` (Protocol Buffers compiler)

## Building

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

First compile your `.proto` file to a `.pb` binary using `protoc`:

```sh
protoc --descriptor_set_out=myfile.pb --include_imports myfile.proto
```

Then run the generator:

```sh
nanopb-generator-c myfile.pb
```

This produces `myfile.pb.h` and `myfile.pb.c` in the current directory.

### Options

```
Usage: nanopb-generator-c [options] file.pb ...

Options:
  -h, --help                   Show this help and exit
  -V, --version                Show version and exit
  -e, --extension EXT          Extension for generated files (default: .pb)
  -H, --header-extension EXT   Extension for header files (default: .h)
  -S, --source-extension EXT   Extension for source files (default: .c)
  -f, --options-file FILE      Options file path (default: <basename>.options)
  -I, --options-path DIR       Search path for .options files
  -D, --output-dir DIR         Output directory for generated files
  -T, --no-timestamp           Do not add timestamp to generated files
  -q, --quiet                  Suppress non-error output
  -s OPTION:VALUE              Set generator option (e.g. max_size:64)
  -Q, --generated-include-format FMT
                               Format for generated #include (default: "#include \"%s\"")
  -L, --library-include-format FMT
                               Format for nanopb pb.h #include (default: "#include <%s>")
```

### Options File

Create a `<protoname>.options` file alongside your `.proto` to configure fields:

```
MyMessage.my_string   max_size:64
MyMessage.my_array    max_count:16
MyMessage             max_size:32
```

## Running Tests

```sh
cd build
ctest --output-on-failure
```

Tests compare the C generator output against the Python nanopb generator output for several sample `.proto` files.

## License

MIT — see [LICENSE](LICENSE).

Bundles [nanopb](https://github.com/nanopb/nanopb) runtime sources (BSD-3-Clause) in `vendor/nanopb/`.
