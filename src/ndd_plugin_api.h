#pragma once

#include <QMenu>
#include <QString>

#include <functional>

class QAction;
class QsciScintilla;
class QWidget;

#ifndef NDD_MARKDOWN_VIEW_VERSION
#define NDD_MARKDOWN_VIEW_VERSION "0.2.8"
#endif

// This structure mirrors notepad--/src/include/pluginGl.h.  Keep field order
// unchanged: it is part of the binary plugin ABI.
struct NddProcData
{
    QString pluginName;
    QString filePath;
    QString comment;
    QString version;
    QString author;
    int menuType;
    QMenu *rootMenu;
    QAction *action;

    NddProcData()
        : menuType(0), rootMenu(nullptr), action(nullptr)
    {
    }
};

using NDD_PROC_DATA = NddProcData;
using NddGetCurrentEditor = std::function<QsciScintilla *(QWidget *)>;
using NddHostCallback = std::function<bool(QWidget *, int, void *)>;

#if defined(Q_OS_WIN)
#define NDD_PLUGIN_EXPORT __declspec(dllexport)
#else
#define NDD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
