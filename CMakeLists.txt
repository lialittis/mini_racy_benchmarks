cmake_minimum_required(VERSION 3.16)

project(mini_racy_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CANN_ROOT "" CACHE PATH "Root directory of the installed CANN toolkit")
if(NOT CANN_ROOT)
    if(DEFINED ENV{CANN_ROOT})
        set(CANN_ROOT "$ENV{CANN_ROOT}")
    else()
        set(CANN_ROOT "/usr/local/Ascend/ascend-toolkit/latest")
    endif()
endif()

find_path(ACL_INCLUDE_DIR acl/acl.h
    HINTS
        "${CANN_ROOT}/include"
        "${CANN_ROOT}/aarch64-linux/include"
        "${CANN_ROOT}/runtime/include")

find_library(ASCENDCL_LIBRARY ascendcl
    HINTS
        "${CANN_ROOT}/lib64"
        "${CANN_ROOT}/aarch64-linux/lib64"
        "${CANN_ROOT}/aarch64-linux/devlib")

if(NOT ACL_INCLUDE_DIR OR NOT ASCENDCL_LIBRARY)
    message(FATAL_ERROR "Cannot find AscendCL under CANN_ROOT=${CANN_ROOT}")
endif()

add_executable(mrb_matmul_runner
    runner/src/common.cpp
    runner/src/main.cpp
    runner/src/operator_desc.cpp
    runner/src/op_runner.cpp)

target_include_directories(mrb_matmul_runner PRIVATE
    "${ACL_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner/include")

target_link_libraries(mrb_matmul_runner PRIVATE "${ASCENDCL_LIBRARY}")

set_target_properties(mrb_matmul_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
