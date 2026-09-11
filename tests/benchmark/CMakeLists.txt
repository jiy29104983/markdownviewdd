cmake_minimum_required(VERSION 3.16)
project(markdownview_refresh_benchmark LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MARKDOWNVIEW_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
    "Plugin source checkout to measure (baseline or candidate)")
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)
add_executable(refresh_benchmark
    refresh_benchmark.cpp
    "${MARKDOWNVIEW_SOURCE_DIR}/src/diagnostics.cpp"
    "${MARKDOWNVIEW_SOURCE_DIR}/src/host_adapter.cpp"
    "${MARKDOWNVIEW_SOURCE_DIR}/src/markdown_preview_dock.cpp"
    "${MARKDOWNVIEW_SOURCE_DIR}/src/preview_controller.cpp"
    "${MARKDOWNVIEW_SOURCE_DIR}/resources/markdownview.qrc")
target_include_directories(refresh_benchmark PRIVATE "${MARKDOWNVIEW_SOURCE_DIR}/src")
target_link_libraries(refresh_benchmark PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
target_compile_definitions(refresh_benchmark PRIVATE NDD_MARKDOWN_VIEW_VERSION="benchmark")
if(MSVC)
    target_compile_options(refresh_benchmark PRIVATE /utf-8 /W4)
else()
    target_compile_options(refresh_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
