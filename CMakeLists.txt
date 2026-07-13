cmake_minimum_required(VERSION 3.16)

project(ndd-markdown-view VERSION 0.2.3 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

add_library(markdownview SHARED
    src/diagnostics.cpp
    src/diagnostics.h
    src/markdown_preview_dock.cpp
    src/markdown_preview_dock.h
    src/plugin_exports.cpp
    src/preview_controller.cpp
    src/preview_controller.h
    src/ndd_plugin_api.h
    resources/markdownview.qrc)

target_include_directories(markdownview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(markdownview PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets)

target_compile_definitions(markdownview PRIVATE
    NDD_MARKDOWN_VIEW_VERSION="${PROJECT_VERSION}"
    UNICODE
    _UNICODE)

if(MSVC)
    target_compile_options(markdownview PRIVATE /utf-8 /W4)
else()
    target_compile_options(markdownview PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(markdownview PROPERTIES
    OUTPUT_NAME "markdownview"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")

install(TARGETS markdownview
    RUNTIME DESTINATION plugin
    LIBRARY DESTINATION plugin)
