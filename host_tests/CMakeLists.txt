cmake_minimum_required(VERSION 3.16)
project(cable_test_host_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Für VS Code IntelliSense (C/C++-Extension).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(cable_test_tests
    test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/cable_test/src/analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/cable_test/src/soak.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/report/src/report.cpp
)

target_include_directories(cable_test_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/cable_test/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/report/include
)

target_compile_options(cable_test_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME analyzer COMMAND cable_test_tests)
