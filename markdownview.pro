TEMPLATE = lib
TARGET = markdownview
CONFIG += c++14 release warn_on
QT += core gui widgets

INCLUDEPATH += \
    $$PWD/src

SOURCES += \
    src/diagnostics.cpp \
    src/markdown_preview_dock.cpp \
    src/plugin_exports.cpp \
    src/preview_controller.cpp

HEADERS += \
    src/diagnostics.h \
    src/markdown_preview_dock.h \
    src/ndd_plugin_api.h \
    src/preview_controller.h

RESOURCES += resources/markdownview.qrc

win32 {
    DEFINES += UNICODE _UNICODE NDD_MARKDOWN_VIEW_VERSION=\\\"0.2.3\\\"
    QMAKE_CXXFLAGS += /utf-8
    DESTDIR = $$PWD/build/plugin
}
