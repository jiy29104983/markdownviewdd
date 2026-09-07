cmake_minimum_required(VERSION 3.16)

project(ndd-markdown-view VERSION 0.2.8 LANGUAGES CXX)

include(CTest)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

add_library(markdownview SHARED
    src/diagnostics.cpp
    src/diagnostics.h
    src/host_adapter.cpp
    src/host_adapter.h
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

    add_executable(markdownview_lifecycle_tests
        tests/markdown_preview_dock_lifecycle_test.cpp
        src/diagnostics.cpp
        src/diagnostics.h
        src/host_adapter.cpp
        src/host_adapter.h
        src/markdown_preview_dock.cpp
        src/markdown_preview_dock.h
        resources/markdownview.qrc)

    target_include_directories(markdownview_lifecycle_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_link_libraries(markdownview_lifecycle_tests PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Test)

    target_compile_definitions(markdownview_lifecycle_tests PRIVATE
        NDD_MARKDOWN_VIEW_VERSION="${PROJECT_VERSION}")

    if(MSVC)
        target_compile_options(markdownview_lifecycle_tests PRIVATE /utf-8 /W4)
    else()
        target_compile_options(markdownview_lifecycle_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME markdownview_lifecycle_tests
        COMMAND markdownview_lifecycle_tests -platform offscreen -v1 -o -,txt)
    set_tests_properties(markdownview_lifecycle_tests PROPERTIES
        LABELS "behavior;lifecycle"
        TIMEOUT 60)

    add_executable(markdownview_document_identity_tests
        tests/preview_controller_document_identity_test.cpp
        src/diagnostics.cpp
        src/diagnostics.h
        src/host_adapter.cpp
        src/host_adapter.h
        src/markdown_preview_dock.cpp
        src/markdown_preview_dock.h
        src/preview_controller.cpp
        src/preview_controller.h
        resources/markdownview.qrc)

    target_include_directories(markdownview_document_identity_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_link_libraries(markdownview_document_identity_tests PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Test)

    target_compile_definitions(markdownview_document_identity_tests PRIVATE
        NDD_MARKDOWN_VIEW_VERSION="${PROJECT_VERSION}")

    if(MSVC)
        target_compile_options(markdownview_document_identity_tests PRIVATE
            /utf-8 /W4)
    else()
        target_compile_options(markdownview_document_identity_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME markdownview_document_identity_tests
        COMMAND markdownview_document_identity_tests -platform offscreen -v1 -o -,txt)
    set_tests_properties(markdownview_document_identity_tests PROPERTIES
        LABELS "behavior;document"
        TIMEOUT 120)

    add_executable(markdownview_multi_window_tests
        tests/plugin_multi_window_test.cpp
        src/diagnostics.cpp
        src/diagnostics.h
        src/host_adapter.cpp
        src/host_adapter.h
        src/markdown_preview_dock.cpp
        src/markdown_preview_dock.h
        src/plugin_exports.cpp
        src/preview_controller.cpp
        src/preview_controller.h
        src/ndd_plugin_api.h
        resources/markdownview.qrc)

    target_include_directories(markdownview_multi_window_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_link_libraries(markdownview_multi_window_tests PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Test)

    target_compile_definitions(markdownview_multi_window_tests PRIVATE
        NDD_MARKDOWN_VIEW_VERSION="${PROJECT_VERSION}")

    if(MSVC)
        target_compile_options(markdownview_multi_window_tests PRIVATE
            /utf-8 /W4)
    else()
        target_compile_options(markdownview_multi_window_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME markdownview_multi_window_tests
        COMMAND markdownview_multi_window_tests -platform offscreen -v1 -o -,txt)
    set_tests_properties(markdownview_multi_window_tests PROPERTIES
        LABELS "behavior;multi-window"
        TIMEOUT 60)

    add_executable(markdownview_diagnostics_tests
        tests/diagnostics_test.cpp
        src/diagnostics.cpp
        src/diagnostics.h)

    target_include_directories(markdownview_diagnostics_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_link_libraries(markdownview_diagnostics_tests PRIVATE
        Qt5::Core
        Qt5::Widgets
        Qt5::Test)

    target_compile_definitions(markdownview_diagnostics_tests PRIVATE
        NDD_MARKDOWN_VIEW_VERSION="${PROJECT_VERSION}")

    if(MSVC)
        target_compile_options(markdownview_diagnostics_tests PRIVATE /utf-8 /W4)
    else()
        target_compile_options(markdownview_diagnostics_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME markdownview_diagnostics_tests
        COMMAND markdownview_diagnostics_tests -platform offscreen -v1 -o -,txt)
    set_tests_properties(markdownview_diagnostics_tests PROPERTIES
        LABELS "behavior;diagnostics"
        TIMEOUT 60)
endif()
