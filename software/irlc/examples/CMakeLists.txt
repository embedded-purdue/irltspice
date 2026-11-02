add_executable(xbar-cli
    ${CMAKE_CURRENT_LIST_DIR}/xbar_cli.cpp
)
target_link_libraries(xbar-cli PRIVATE irldriver)
