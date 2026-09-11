#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>
#include <QtGlobal>

// Internal plugin contract. Versions identify source changes, not tab visits.
enum class RefreshMode { Automatic, Manual };
enum class PreviewState { NoDocument, Unsupported, Pending, Ready, Paused, Failed };

struct PreviewStatus
{
    RefreshMode mode = RefreshMode::Automatic;
    PreviewState state = PreviewState::NoDocument;
    QPointer<QWidget> activeEditor;
    QPointer<QWidget> displayedEditor;
    QString filePath;
    QString protectionReason;
    QString error;
    quint64 contentVersion = 0;
    quint64 displayedVersion = 0;
    bool performanceProtected = false;
};
