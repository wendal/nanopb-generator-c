find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ---------------------------------------------------------------------------
# Locate the nanopb Python generator
# Priority:
#   1. User-provided -DNANOPB_PYTHON_GENERATOR=<path>
#   2. Sibling dev-checkout  (../../generator/nanopb_generator.py)
#   3. pip-installed nanopb package (nanopb.generator.nanopb_generator)
# ---------------------------------------------------------------------------
set(NANOPB_PYTHON_GENERATOR "" CACHE STRING
    "Explicit path to nanopb_generator.py; auto-detected if empty")

set(PYTHON_GENERATOR "")

if(NANOPB_PYTHON_GENERATOR AND EXISTS "${NANOPB_PYTHON_GENERATOR}")
    set(PYTHON_GENERATOR "${NANOPB_PYTHON_GENERATOR}")
endif()

if(NOT PYTHON_GENERATOR)
    set(_adj "${CMAKE_CURRENT_SOURCE_DIR}/../../generator/nanopb_generator.py")
    if(EXISTS "${_adj}")
        set(PYTHON_GENERATOR "${_adj}")
    endif()
endif()

if(NOT PYTHON_GENERATOR)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c
            "import nanopb.generator.nanopb_generator as m, os; print(os.path.abspath(m.__file__))"
        OUTPUT_VARIABLE _pip_gen OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _pip_ret ERROR_QUIET
    )
    if(_pip_ret EQUAL 0 AND EXISTS "${_pip_gen}")
        set(PYTHON_GENERATOR "${_pip_gen}")
    endif()
endif()

if(NOT PYTHON_GENERATOR)
    message(WARNING "nanopb Python generator not found — skipping comparison tests. "
                    "Install via 'pip install nanopb' or set -DNANOPB_PYTHON_GENERATOR=<path>")
    return()
endif()
message(STATUS "nanopb Python generator: ${PYTHON_GENERATOR}")

# ---------------------------------------------------------------------------
# Find a Python interpreter that has grpcio-tools
# ---------------------------------------------------------------------------
set(_GRPC_PYTHON "")
foreach(_py_candidate ${Python3_EXECUTABLE} python python3 py)
    execute_process(
        COMMAND "${_py_candidate}" -c "import grpc_tools.protoc"
        RESULT_VARIABLE _grpc_ret
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_grpc_ret EQUAL 0)
        set(_GRPC_PYTHON "${_py_candidate}")
        break()
    endif()
endforeach()

if(NOT _GRPC_PYTHON)
    message(WARNING "grpcio-tools not available in any Python interpreter — skipping comparison tests. "
                    "Install via 'pip install grpcio-tools'")
    return()
endif()

set(COMPARE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/compare_output.py")
set(PROTOS_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/protos")

# Helper to add one comparison test
function(add_compare_test NAME PROTO)
    add_test(
        NAME "compare_${NAME}"
        COMMAND ${_GRPC_PYTHON} ${COMPARE_SCRIPT}
            --proto        "${PROTOS_DIR}/${PROTO}"
            --c-generator  "$<TARGET_FILE:nanopb-generator-c>"
            --py-generator "${PYTHON_GENERATOR}"
            --work-dir     "${CMAKE_CURRENT_BINARY_DIR}/compare_${NAME}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    set_tests_properties("compare_${NAME}" PROPERTIES
        DEPENDS nanopb-generator-c
        TIMEOUT 60
    )
endfunction()

add_compare_test(simple simple.proto)
add_compare_test(enums  enums.proto)
add_compare_test(nested nested.proto)
