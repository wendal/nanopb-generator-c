cmake_minimum_required(VERSION 3.15)
project(nanopb-generator-c VERSION 0.4.9 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- Options ----------------------------------------------------------------
option(BUILD_TESTING "Build tests" ON)

# ---- Compiler flags ---------------------------------------------------------
if(MSVC)
    add_compile_options(/W3 /wd4996)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Generator library (zero external deps) ---------------------------------
# Uses a hand-coded minimal protobuf wire reader (no libprotobuf required).
add_library(generator_lib STATIC
    src/pb_reader.c
    src/descriptor.c
    src/strbuf.c
    src/names.c
    src/options.c
    src/generator.c
)
target_include_directories(generator_lib PUBLIC src)

# ---- Main executable --------------------------------------------------------
add_executable(nanopb-generator-c src/main.c)
target_link_libraries(nanopb-generator-c PRIVATE generator_lib)

# ---- Install ----------------------------------------------------------------
install(TARGETS nanopb-generator-c RUNTIME DESTINATION bin)

# ---- Tests ------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
