#pragma once

#include <QString>
#include <functional>

class QWidget;

struct SourceReadResult
{
    QString text;
    QString error;
    bool available = false;
};

struct SourceNavigationResult
{
    bool reached = false;
    QString error;
};

// Fixed host contract: zero-based source line and Unicode scalar offset.
// QString indices are UTF-16; neither value is a Scintilla byte position.
SourceReadResult readAccessibleSource(QWidget *editor);
SourceNavigationResult navigateAccessibleSource(
    QWidget *editor, int line, int scalarOffset,
    const std::function<bool()> &stillCurrent);
