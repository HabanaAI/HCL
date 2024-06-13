cmake_minimum_required(VERSION 3.5.1)

project(hcl LANGUAGES CXX C)

set(TARGET_NAME_SO hcl)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${NOAVX512}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fopenmp -fno-omit-frame-pointer -g1")
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

include_directories(
    $ENV{HCL_SRC_PKG_DIR}
    $ENV{HCL_SRC_PKG_DIR}/dependencies
    $ENV{HCL_SRC_PKG_DIR}/dependencies/scal
    $ENV{HCL_SRC_PKG_DIR}/dependencies/3rd-parties/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/3rd-parties/hwloc-2.10.0/include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/3rd-parties/json/single_include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/habanalabs/include/uapi/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/hl-thunk/include/uapi/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/qman_fw/engines-arc/include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/rdma-core/build/include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/specs/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/specs_external/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/specs_h9/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/specs_h9/gaudi3/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/swtools_sdk/hl_gcfg/include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/swtools_sdk/hl_logger/include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/synapse/include/
    $ENV{HCL_SRC_PKG_DIR}/dependencies/synapse/include/internal/
    $ENV{HCL_SRC_PKG_DIR}/hcl/hccl_ofi_wrapper/
    $ENV{HCL_SRC_PKG_DIR}/hcl/include/
    $ENV{HCL_SRC_PKG_DIR}/hcl/include/internal/
    $ENV{HCL_SRC_PKG_DIR}/hcl/src/
    $ENV{HCL_SRC_PKG_DIR}/hcl/src/hccl/
    $ENV{HCL_SRC_PKG_DIR}/hcl/src/infra/
)

# Specify .so .cpp files
set(SRC_DIRS
    ../common
    ..
    .
    hccl
    infra
    infra/scal/gaudi2
    infra/scal/gaudi3
    infra/scal/gen2_arch_common
    interfaces
    events
    libfabric
    ibverbs

    platform/gaudi2
    platform/gaudi2/commands
    platform/gaudi2/signals
    platform/gaudi2/events
    platform/gaudi2/scal

    platform/gaudi3
    platform/gaudi3/commands
    platform/gaudi3/signals
    platform/gaudi3/events
    platform/gaudi3/scal

    platform/gen2_arch_common
    platform/gen2_arch_common/commands
    platform/gen2_arch_common/events
    platform/gen2_arch_common/scal
    platform/gen2_arch_common/signals
)

file(GLOB SRCS "")
foreach(dir ${SRC_DIRS})
    file(GLOB_RECURSE FILES ${dir}/*.cpp)
    list(APPEND SRCS ${FILES})
endforeach()

add_library(${TARGET_NAME_SO} SHARED ${SRCS})
set_target_properties(${TARGET_NAME_SO} PROPERTIES CXX_VISIBILITY_PRESET hidden)

target_link_libraries(
    ${TARGET_NAME_SO}
    $ENV{HCL_LIB_DIR}/libhl_logger.so
    $ENV{HCL_LIB_DIR}/libhl_gcfg.so
    $ENV{HCL_LIB_DIR}/libemon.a
    $ENV{HCL_LIB_DIR}/libhwloc_embedded.a
    $ENV{HCL_LIB_DIR}/libglpk.a
)
