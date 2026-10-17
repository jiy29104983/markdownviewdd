cmake_minimum_required(VERSION 3.16)

project(ndd-markdown-view VERSION 0.2.10 LANGUAGES CXX)

include(CTest)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_compile_definitions(NDD_MARKDOWN_VIEW_VERSION="${PROJECT_VERSION}")

add_library(markdownview_objects OBJECT
    src/diagnostics.cpp
    src/diagnostics.h
    src/saved_markdown_font.cpp
    src/saved_markdown_font.h
    src/source_navigation.cpp
    src/code_block_index.cpp
    src/code_block_tools.cpp
    src/heading_index.cpp
    src/preview_search.cpp
    src/heading_outline.cpp
    src/host_adapter.cpp
    src/host_adapter.h
    src/markdown_preview_dock.cpp
    src/markdown_preview_dock.h
    src/preview_controller.cpp
    src/preview_controller.h
    src/preview_status.h
    src/ndd_plugin_api.h
    resources/markdownview.qrc)

set_target_properties(markdownview_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(markdownview_objects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(markdownview_objects PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets)
target_compile_definitions(markdownview_objects PUBLIC UNICODE _UNICODE)

add_library(markdownview SHARED src/plugin_exports.cpp)
target_link_libraries(markdownview PRIVATE markdownview_objects)

set_target_properties(markdownview PROPERTIES
    OUTPUT_NAME "markdownviewdd"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")

install(TARGETS markdownview
    RUNTIME DESTINATION plugin
    LIBRARY DESTINATION plugin)

if(BUILD_TESTING)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)

    function(add_markdownview_test target source label timeout)
        add_executable(${target} ${source} ${ARGN})
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(${target} PRIVATE Qt5::Widgets Qt5::Test)
        if(NOT target STREQUAL "markdownview_diagnostics_tests")
            target_link_libraries(${target} PRIVATE markdownview_objects)
        endif()
        add_test(NAME ${target}
            COMMAND ${target} -platform offscreen -v1 -o ${target}.txt,txt)
        set_tests_properties(${target} PROPERTIES
            LABELS "behavior;${label}" TIMEOUT ${timeout})
    endfunction()

    add_markdownview_test(markdownview_lifecycle_tests
        tests/markdown_preview_dock_lifecycle_test.cpp lifecycle 60)
    add_markdownview_test(markdownview_document_identity_tests
        tests/preview_controller_document_identity_test.cpp document 120)
    add_markdownview_test(markdownview_multi_window_tests
        tests/plugin_multi_window_test.cpp multi-window 60 src/plugin_exports.cpp)
    add_markdownview_test(markdownview_diagnostics_tests
        tests/diagnostics_test.cpp diagnostics 60 src/diagnostics.cpp)
    add_markdownview_test(markdownview_font_tests
        tests/saved_markdown_font_test.cpp font 90)
    add_markdownview_test(markdownview_outline_tests
        tests/heading_outline_test.cpp outline 90)
    add_markdownview_test(markdownview_search_tests
        tests/preview_search_test.cpp search 120)
    add_markdownview_test(markdownview_code_tests
        tests/code_block_test.cpp code 120)

    if(WIN32)
        # Qt 5.15's offscreen backend uses the basic font database, which
        # otherwise searches Qt's empty lib/fonts directory on Windows.
        file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" windows_font_directory)
        if(NOT IS_DIRECTORY "${windows_font_directory}")
            message(FATAL_ERROR "Windows test fonts not found: ${windows_font_directory}")
        endif()
        get_property(markdownview_tests DIRECTORY PROPERTY TESTS)
        set_tests_properties(${markdownview_tests}
            PROPERTIES ENVIRONMENT "QT_QPA_FONTDIR=${windows_font_directory}")
    endif()
endif()
