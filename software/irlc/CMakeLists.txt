cmake_minimum_required(VERSION 3.28)

# ========
# Includes
# ========
include(FetchContent)

project(IrlCompiler LANGUAGES CXX)

# =========
# Variables
# =========

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

set( IRLC_DIR ${CMAKE_CURRENT_SOURCE_DIR} )
set( IRLTSPICE_DIR ${IRLC_DIR}/../.. )
set( PROTO_DIR ${IRLTSPICE_DIR}/proto )
set( GEN_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen_include )
set( GEN_PROTOBUF_SRC_DIR ${GEN_INCLUDE_DIR}/protobuf )

# =================
# Developer configs
# =================
# clangd has no include paths for headers that are not part of a translation
# unit in compile_commands.json (e.g. when you open an .hpp directly). Generate
# a .clangd with fallback flags for those files. Here because it needs absolute paths.
configure_file(
    ${IRLC_DIR}/cmake/clangd.in
    ${IRLC_DIR}/.clangd
    @ONLY
)

# ============
# Dependencies
# ============

# CPM
# download CPM.cmake
file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.1/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=f3a6dcc6a04ce9e7f51a127307fa4f699fb2bade357a8eb4c5b45df76e1dc6a5
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# Boost
set(BOOST_INCLUDE_LIBRARIES graph program_options asio outcome)
CPMAddPackage(
    NAME Boost 
    VERSION 1.90.0
    URL https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.xz
    URL_HASH SHA256=aca59f889f0f32028ad88ba6764582b63c916ce5f77b31289ad19421a96c555f
)

# Protoc/protobuf
CPMAddPackage(
    NAME Protobuf
    VERSION 36.2.0
    URL https://github.com/protocolbuffers/protobuf/releases/download/v36.2/protobuf-36.2.tar.gz
    URL_HASH SHA256=3d9642a662d10e68ebae5e53f14dcce5105684212d5078f8e0d47d1ab3ae6b64
    OPTIONS
        "protobuf_BUILD_TESTS OFF"
        "protobuf_BUILD_EXAMPLES OFF"
        "protobuf_BUILD_CONFORMANCE OFF"
        "protobuf_BUILD_SHARED_LIBS OFF"
)
include("${protobuf_BINARY_DIR}/cmake/protobuf/protobuf-generate.cmake")

# fast_float
# Due to macos requiring 26.0 for std::from_chars with doubles, we depend on fast_float, which provides a near-identical api in header-only form
CPMAddPackage(
  NAME fast_float
  GITHUB_REPOSITORY "fastfloat/fast_float"
  GIT_TAG v8.3.2
)

# =======
# Targets
# =======

# Preprocess protobufs
set(PROTOS
    ${PROTO_DIR}/command.proto
)

# The project is split into three internal pieces plus one distributable. 
# Internal: 
#   - irlcore
#   - irlcompiler
#   - irldriver
# Distributable: 
#   - libirlc.a

# `target_link_libraries` on an OBJECT library adds its objects to the consumer
# (and, for a STATIC library, into the archive)

# --- irlcore : shared netlist representation, commands, and misc datastructures ---
add_library(irlcore OBJECT
    ${IRLC_DIR}/src/irl/core/netlist.cpp
    ${IRLC_DIR}/src/irl/core/debug.cpp
)
target_include_directories(irlcore 
    PUBLIC  ${IRLC_DIR}/include
    PRIVATE ${IRLC_DIR}/src
)

# --- irlcompiler : stateless parsing/verification/routing. Depends on netlist. ---
add_library(irlcompiler OBJECT
    ${IRLC_DIR}/src/irl/compiler.cpp
    ${IRLC_DIR}/src/irl/compiler/parse_spice.cpp
    ${IRLC_DIR}/src/irl/compiler/parse_eeschema.cpp
)
target_include_directories(irlcompiler
    PUBLIC  ${IRLC_DIR}/include
    PRIVATE ${IRLC_DIR}/src
)
target_link_libraries(irlcompiler PUBLIC irlcore fast_float)

# --- irldriver : board communication + oscilloscope. Depends on netlist. ---
add_library(irldriver OBJECT
    ${IRLC_DIR}/src/irl/driver.cpp
)
target_include_directories(irldriver
    PUBLIC ${IRLC_DIR}/include
    PRIVATE ${IRLC_DIR}/src
)
target_link_libraries(irldriver PUBLIC irlcore protobuf::libprotobuf Boost::asio Boost::outcome)

make_directory(${GEN_PROTOBUF_SRC_DIR})
protobuf_generate(
    TARGET irldriver
    LANGUAGE cpp
    PROTOS ${PROTOS}
    IMPORT_DIRS ${PROTO_DIR}
    PROTOC_OUT_DIR ${GEN_PROTOBUF_SRC_DIR}
)
target_include_directories(irldriver PRIVATE ${GEN_PROTOBUF_SRC_DIR})

# --- libirlc.a : the single artifact end users link. ---
add_library(irlc STATIC)
target_include_directories(irlc PUBLIC ${IRLC_DIR}/include)
target_link_libraries(irlc PRIVATE irlcore irlcompiler irldriver)
# Static archives do not carry their dependencies, so re-expose the external
# ones the packed objects need.
target_link_libraries(irlc PUBLIC protobuf::libprotobuf Boost::asio Boost::outcome fast_float)

# Legacy single-binary CLI. `src/main.cpp` still pulls in the old `src/core`
# compiler and does not build against the split yet. Re-enable once the
# compiler/driver implementations land.
# add_executable(irlc-cli)
# target_sources(irlc-cli PRIVATE ${IRLC_DIR}/src/main.cpp)
# target_link_libraries(irlc-cli PRIVATE irlc Boost::program_options)

# =======
# Testing
# =======
enable_testing()
add_subdirectory(tests)
