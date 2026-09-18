cmake_minimum_required(VERSION 3.30)

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
set( GEN_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen_proto )
set( GEN_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen_include )
set( GEN_PROTOBUF_SRC_DIR ${GEN_INCLUDE_DIR}/protobuf )

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

# =======
# Targets
# =======

# Preprocess protobufs
set(PROTOS
    ${PROTO_DIR}/dummy.proto
)
set(PROCESSED_PROTOS "")

make_directory(${GEN_PROTO_DIR})
foreach(PROTO ${PROTOS})
    get_filename_component(PROTO_NAME ${PROTO} NAME)
    set(OUTPUT_PROTO "${GEN_PROTO_DIR}/${PROTO_NAME}")
    
    add_custom_command(
        OUTPUT "${OUTPUT_PROTO}"
        COMMAND sed [[s/\/\*ifnpb\*\/.*\/\*endif\*\///]] "${PROTO}" > "${OUTPUT_PROTO}"
        DEPENDS "${PROTO}" "${GEN_PROTO_DIR}"
        COMMENT "Preprocessing ${PROTO_NAME} with sed"
        VERBATIM
    )
    
    list(APPEND PROCESSED_PROTOS "${OUTPUT_PROTO}")
endforeach()

# Lib (separated for testing and such)
add_library(libirlc) 
target_sources(libirlc 
    PRIVATE 
        ${IRLC_DIR}/src/core/compiler.cpp
        ${IRLC_DIR}/src/core/route.cpp
        ${IRLC_DIR}/src/core/parse_spice.cpp
        ${IRLC_DIR}/src/core/parse_eechema.cpp
        ${IRLC_DIR}/src/core/verify.cpp
        ${IRLC_DIR}/src/core/debug.cpp
        ${IRLC_DIR}/src/core/board_info.cpp
        ${IRLC_DIR}/src/core/numbers.cpp
        ${IRLC_DIR}/src/core/emit.cpp
)
target_include_directories(libirlc 
    PUBLIC 
        ${IRLC_DIR}/include/
    PRIVATE
        ${GEN_PROTOBUF_SRC_DIR}
)
target_link_libraries(libirlc 
    PRIVATE 
        Boost::graph
        Boost::asio
        Boost::outcome
        protobuf::libprotobuf
)

make_directory(${GEN_PROTOBUF_SRC_DIR})
protobuf_generate(
    TARGET libirlc
    LANGUAGE cpp
    PROTOS ${PROCESSED_PROTOS}
    IMPORT_DIRS ${GEN_PROTO_DIR}
    PROTOC_OUT_DIR ${GEN_PROTOBUF_SRC_DIR}
)


# Actual compiler
add_executable(irlc)
target_sources(irlc 
    PRIVATE 
        ${IRLC_DIR}/src/main.cpp
)
target_link_libraries(irlc 
    PRIVATE 
        libirlc 
        Boost::program_options
)

# =======
# Testing
# =======
enable_testing()
add_subdirectory(tests)
