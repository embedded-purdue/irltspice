include(FetchContent)

CPMAddPackage(
    NAME Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.12.0
    GIT_SHALLOW 1
)

# Core-only tests. These link the core object library and nothing else,
add_executable(irlcore-tests
    ${CMAKE_CURRENT_LIST_DIR}/test_smallvec.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_chunkedlist.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_netlist.cpp
)

target_include_directories(irlcore-tests
    PRIVATE
        ${IRLC_DIR}/src/
)

target_link_options(irlcore-tests PRIVATE -g -fsanitize=address)

target_link_libraries(irlcore-tests
    PRIVATE
        irlcore
        Catch2::Catch2WithMain
)

add_test(NAME irlcore-tests COMMAND irlcore-tests)



# Compiler tests. These link the compiler object library, thus also core,
add_executable(irlcompiler-tests
    ${CMAKE_CURRENT_LIST_DIR}/test_parsers.cpp
)

target_include_directories(irlcompiler-tests
    PRIVATE
        ${IRLC_DIR}/src/
)

target_link_options(irlcompiler-tests PRIVATE -g -fsanitize=address)

target_link_libraries(irlcompiler-tests
    PRIVATE
        irlcore
        irlcompiler
        Catch2::Catch2WithMain
)

add_test(NAME irlcompiler-tests COMMAND irlcompiler-tests)

# Legacy tests, disabled during the interface refactor: they still depend on the
# old `src/core` compiler/routing/parsers and do not build against the split.
# Re-enable each alongside the module it exercises.
#
# add_executable(irlc-tests
#     ${CMAKE_CURRENT_LIST_DIR}/test_routing.cpp
#     ${CMAKE_CURRENT_LIST_DIR}/test_parsers.cpp
#     ${CMAKE_CURRENT_LIST_DIR}/test_numbers.cpp
# )
# target_link_options(irlc-tests PRIVATE -g -fsanitize=address)
# target_include_directories(irlc-tests PRIVATE ${IRLC_DIR}/src/)
# target_link_libraries(irlc-tests PRIVATE irlc Boost::graph Catch2::Catch2WithMain)
# add_test(NAME irlc-tests COMMAND irlc-tests)
