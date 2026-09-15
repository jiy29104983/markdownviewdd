#pragma once

#include "code_block_index.h"
#include "preview_status.h"
#include <QWidget>
#include <QMetaObject>
#include <functional>

class QComboBox;
class QLabel;
class QMenu;
class QTextDocument;
class QTextEdit;
class QToolButton;
class QTimer;

// Controls live outside QTextDocument: neither decorations nor feedback export.
class CodeBlockTools final : public QWidget
{
    Q_OBJECT
public:
    using ClipboardWriter = std::function<bool(const QString &)>;
    explicit CodeBlockTools(QWidget *parent = nullptr);
    ~CodeBlockTools() override;
    void setSnapshot(QTextEdit *edit, const QVector<CodeBlockRecord> &records,
                     QWidget *editor, quint64 version);
    void clearSnapshot();
    void setStatus(const PreviewStatus &status);
    void setFormatting(bool formatting) { m_formatting = formatting; }
    void setClipboardWriter(ClipboardWriter writer);
    void setSnapshotValidator(std::function<bool(QWidget *, quint64)> validator) { m_validator = std::move(validator); }
    bool copyBlock(int ordinal);
    QVector<CodeBlockRecord> records() const { return m_records; }

signals:
    void navigateRequested(QWidget *editor, quint64 version, int position, int length);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateSelection();
    QString snapshotHint() const;
    QComboBox *m_blocks;
    QToolButton *m_copy;
    QLabel *m_feedback;
    QTimer *m_feedbackTimer;
    QPointer<QMenu> m_menu;
    QPointer<QTextEdit> m_edit;
    QPointer<QTextDocument> m_document;
    QPointer<QWidget> m_editor;
    QVector<CodeBlockRecord> m_records;
    QVector<QMetaObject::Connection> m_connections;
    quint64 m_version = 0;
    quint64 m_generation = 0;
    bool m_formatting = false;
    bool m_stale = false;
    bool m_manual = false;
    ClipboardWriter m_writer;
    std::function<bool(QWidget *, quint64)> m_validator;
};
