#pragma once

#include <QByteArray>
#include <QUuid>
#include <QWidget>

#include <functional>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTableView;
class QToolButton;
class CodeEditor;
class ResponseViewer;
class RequestLogModel;
class RequestFilterProxyModel;
struct RequestLogEntry;

class LogPane final : public QWidget
{
public:
    LogPane(RequestLogModel *model, const QByteArray &splitterState, bool autoScroll,
            bool prettyJson, std::function<void(const QByteArray &)> splitterChanged,
            std::function<void(bool)> autoScrollChanged,
            std::function<void(bool)> prettyJsonChanged, QWidget *parent = nullptr);

    void setClearAction(std::function<void()> action);
    void setReplayAction(std::function<void(const RequestLogEntry &)> action);
    void entryChanged(const QUuid &entryId);
    void modelReplaced();
    void setClearEnabled(bool enabled);
    void setPrettyJson(bool enabled);
    void revealLatestEntry();

protected:
    void changeEvent(QEvent *event) override;

private:
    const RequestLogEntry *currentEntry() const;
    void selectEntry(const QUuid &entryId, bool scrollToEntry = true);
    void updateDetails();
    void updateResultCount();
    void updateActions();
    void setDetailsOpen(bool open);
    void updateDetailsVisibility();
    void copyCurrentPage();
    void copyAsCurl();
    void saveCurrentBody();
    void exportLog(bool csv);
    ResponseViewer *currentBodyViewer() const;

    RequestLogModel *m_model = nullptr;
    RequestFilterProxyModel *m_proxy = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_detailsCard = nullptr;
    QTableView *m_view = nullptr;
    QStackedWidget *m_tableStack = nullptr;
    QStackedWidget *m_detailStack = nullptr;
    QStackedWidget *m_pages = nullptr;
    QTabBar *m_tabs = nullptr;
    QLineEdit *m_search = nullptr;
    QTabBar *m_filter = nullptr;
    QAction *m_autoScroll = nullptr;
    QPushButton *m_clear = nullptr;
    QToolButton *m_copy = nullptr;
    QToolButton *m_detailsToggle = nullptr;
    QToolButton *m_saveBodyButton = nullptr;
    QToolButton *m_replayButton = nullptr;
    QAction *m_copyUrl = nullptr;
    QAction *m_copyRequest = nullptr;
    QAction *m_copyResponse = nullptr;
    QAction *m_copyCurl = nullptr;
    QAction *m_replay = nullptr;
    QAction *m_saveBody = nullptr;
    QAction *m_exportJson = nullptr;
    QAction *m_exportCsv = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_statistics = nullptr;
    QLabel *m_meanDuration = nullptr;
    QWidget *m_successDot = nullptr;
    QLabel *m_live = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_timing = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_address = nullptr;
    QLabel *m_error = nullptr;
    ResponseViewer *m_requestBody = nullptr;
    CodeEditor *m_requestHeaders = nullptr;
    ResponseViewer *m_responseBody = nullptr;
    CodeEditor *m_responseHeaders = nullptr;
    QUuid m_selectedEntry;
    QUuid m_entryBeforeReset;
    int m_verticalScrollBeforeReset = 0;
    int m_horizontalScrollBeforeReset = 0;
    bool m_resetting = false;
    bool m_clearAllowed = true;
    bool m_detailsDismissed = true;
    QByteArray m_expandedSplitterState;
    std::function<void(const QByteArray &)> m_splitterChanged;
    std::function<void(bool)> m_autoScrollChanged;
    std::function<void(bool)> m_prettyJsonChanged;
    std::function<void()> m_clearAction;
    std::function<void(const RequestLogEntry &)> m_replayAction;
};
