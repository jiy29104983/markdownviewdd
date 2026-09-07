#include "ndd_plugin_api.h"
#include "diagnostics.h"
#include "preview_controller.h"

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

    Diagnostics::initialize();
    Diagnostics::write(notepad, QStringLiteral("NDD_PROC_MAIN entered"));

    if (!notepad || !data || !data->rootMenu || !getCurrentEditor) {
        Diagnostics::write(notepad, QStringLiteral("NDD_PROC_MAIN rejected invalid arguments"),
                           Diagnostics::Level::Error);
        return -1;
    }

    PreviewController *controller = notepad->findChild<PreviewController *>(
        QString(), Qt::FindDirectChildrenOnly);
    const bool createdController = !controller;
    if (!controller) {
        Diagnostics::write(notepad, QStringLiteral("creating PreviewController for host window"));
        Q_UNUSED(getCurrentEditor);
        Q_UNUSED(hostCallback);
        controller = new PreviewController(notepad);
    } else {
        Diagnostics::write(notepad, QStringLiteral(
            "reusing PreviewController for repeated host-window initialization"));
    }

    if (!controller->installMenu(data->rootMenu)) {
        Diagnostics::write(notepad, QStringLiteral("installMenu failed"),
                           Diagnostics::Level::Error);
        if (createdController) {
            delete controller;
        }
        return -2;
    }
    Diagnostics::write(notepad, QStringLiteral("PreviewController and menu ready"));

    return 0;
}

} // extern "C"
