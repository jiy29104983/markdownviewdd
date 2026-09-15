#pragma once

#include "preview_status.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTextBlock>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTextDocument;
class QTextEdit;
class QToolButton;

// Window-local search over the displayed document. Positions are valid only for
// this editor/version/query generation; no QTextCursor is retained per match.
class PreviewSearch final : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewSearch(QWidget *parent = nullptr);
    ~PreviewSearch() override;
    void setSnapshot(QTextEdit *view, QWidget *editor, quint64 version);
    void setStatus(const PreviewStatus &status);
    void setActive(bool active);
    void openSearch();
    void closeSearch();
    bool handleKey(QObject *watched, QEvent *event);
    void updateHighlights();
    void setFormatting(bool formatting);
    bool isOpen() const { return m_open; }
    bool isSearching() const { return m_searching; }
    int matchCount() const { return m_matches.size(); }
    int currentIndex() const { return m_current; }
    int currentPosition() const;
    quint64 generation() const { return m_generation; }
    qint64 lastSearchNanoseconds() const { return m_lastSearchNs; }
    qint64 maxBatchNanoseconds() const { return m_maxBatchNs; }
    qint64 maxHighlightNanoseconds() const { return m_maxHighlightNs; }

signals:
    void navigateRequested(QWidget *editor, quint64 version, int position, int length);
    void searchFinished();
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void restart(bool keepAnchor);
    void stop(bool repaint = true);
    void scan(quint64 generation);
    void navigate(int direction);
    void updateUi();
    void rememberCurrent();
    void clearHighlights();
    QString contextAt(int position) const;
    bool usable() const;

    QLineEdit *m_query = nullptr;
    QCheckBox *m_case = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_message = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
    QPointer<QTextEdit> m_view;
    QPointer<QTextDocument> m_document;
    QPointer<QWidget> m_editor;
    QMetaObject::Connection m_contentConnection;
    QMetaObject::Connection m_destroyConnection;
    QMetaObject::Connection m_scrollConnection;
    PreviewStatus m_status;
    quint64 m_version = 0;
    quint64 m_generation = 0;
    int m_revision = -1;
    bool m_formatting = false;
    bool m_updatingHighlights = false;
    bool m_open = false;
    bool m_active = true;
    bool m_searching = false;
    bool m_snapshotValid = false;
    QString m_pattern;
    QVector<int> m_prefix;
    QVector<int> m_matches;
    QTextBlock m_block;
    int m_offset = 0;
    int m_matched = 0;
    int m_current = -1;
    QString m_anchorContext;
    int m_anchorPosition = -1;
    int m_bestAnchor = -1;
    int m_bestDistance = 0;
    QString m_notice;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_paintElapsed;
    qint64 m_lastSearchNs = 0;
    qint64 m_maxBatchNs = 0;
    qint64 m_maxHighlightNs = 0;
    quint64 m_highlightGeneration = 0;
    int m_highlightBegin = -1;
    int m_highlightEnd = -1;
    int m_highlightCurrent = -1;
    QPalette m_highlightPalette;
    bool m_waitingForLayout = false;
    bool m_highlightRetryPending = false;
};
