cmake_minimum_required(VERSION 3.21)
project(Aplus VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_executable(aplus
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/codeeditor.cpp
    src/codeeditor.h
    src/highlighter.cpp
    src/highlighter.h
    src/compilerunner.cpp
    src/compilerunner.h
    src/consolerunner.cpp
    src/consolerunner.h
    resources.qrc
)

target_include_directories(aplus PRIVATE src)
target_link_libraries(aplus PRIVATE Qt6::Widgets)
target_compile_options(aplus PRIVATE -Wall -Wextra)
set_target_properties(aplus PROPERTIES WIN32_EXECUTABLE TRUE)

option(APLUS_BUILD_TESTS "Build the compiler runner tests" ON)
if(APLUS_BUILD_TESTS)
    enable_testing()
    add_executable(compilerunner_tests
        tests/test_compilerunner.cpp
        src/compilerunner.cpp
        src/compilerunner.h
    )
    target_include_directories(compilerunner_tests PRIVATE src)
    target_link_libraries(compilerunner_tests PRIVATE Qt6::Core)
    target_compile_options(compilerunner_tests PRIVATE -Wall -Wextra)
    add_test(NAME compilerunner COMMAND compilerunner_tests)
endif()
