#include "ndd_plugin_api.h"
#include "diagnostics.h"
#include "preview_controller.h"

#include <QPointer>

namespace {
QPointer<PreviewController> g_controller;
}

extern "C" {

NDD_PLUGIN_EXPORT bool NDD_PROC_IDENTIFY(NDD_PROC_DATA *data)
{
    if (!data) {
        return false;
    }

    data->pluginName = QStringLiteral("Markdown 预览");
    data->comment = QStringLiteral("实时预览当前 Markdown 文件");
    data->version = QStringLiteral("v") +
                    QStringLiteral(NDD_MARKDOWN_VIEW_VERSION);
    data->author = QStringLiteral("markdownview-- contributors");
    data->menuType = 1;
    return true;
}

NDD_PLUGIN_EXPORT int NDD_PROC_MAIN(QWidget *notepad,
                                    const QString &pluginFilePath,
                                    NddGetCurrentEditor getCurrentEditor,
                                    NddHostCallback hostCallback,
                                    NDD_PROC_DATA *data)
{
    Q_UNUSED(pluginFilePath);

    Diagnostics::resetLog();
    Diagnostics::write(QStringLiteral("NDD_PROC_MAIN entered"));

    if (!notepad || !data || !data->rootMenu || !getCurrentEditor) {
        Diagnostics::write(QStringLiteral("NDD_PROC_MAIN rejected invalid arguments"));
        return -1;
    }

    if (!g_controller) {
        Diagnostics::write(QStringLiteral("creating PreviewController"));
        Q_UNUSED(getCurrentEditor);
        Q_UNUSED(hostCallback);
        g_controller = new PreviewController(notepad);
        if (!g_controller->installMenu(data->rootMenu)) {
            Diagnostics::write(QStringLiteral("installMenu failed"));
            delete g_controller;
            g_controller = nullptr;
            return -2;
        }
        Diagnostics::write(QStringLiteral("PreviewController and menu ready"));
    }

    return 0;
}

} // extern "C"
