TEMPLATE = lib
TARGET = markdownviewdd
CONFIG += c++14 release warn_on
QT += core gui widgets

INCLUDEPATH += \
    $$PWD/src

SOURCES += \
    src/diagnostics.cpp \
    src/source_navigation.cpp \
    src/code_block_index.cpp \
    src/code_block_tools.cpp \
    src/heading_index.cpp \
    src/heading_outline.cpp \
    src/preview_search.cpp \
    src/host_adapter.cpp \
    src/saved_markdown_font.cpp \
    src/markdown_preview_dock.cpp \
    src/plugin_exports.cpp \
    src/preview_controller.cpp

HEADERS += \
    src/diagnostics.h \
    src/source_navigation.h \
    src/code_block_index.h \
    src/code_block_tools.h \
    src/heading_index.h \
    src/heading_outline.h \
    src/preview_search.h \
    src/host_adapter.h \
    src/saved_markdown_font.h \
    src/markdown_preview_dock.h \
    src/ndd_plugin_api.h \
    src/preview_controller.h \
    src/preview_status.h

RESOURCES += resources/markdownview.qrc

win32 {
    DEFINES += UNICODE _UNICODE NDD_MARKDOWN_VIEW_VERSION=\\\"0.2.9\\\"
    QMAKE_CXXFLAGS += /utf-8
    DESTDIR = $$PWD/build/plugin
}
