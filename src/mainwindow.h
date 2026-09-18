#pragma once

#include "requesttypes.h"
#include "settingsstore.h"
#include "workspacestore.h"

#include <QHash>
#include <QMainWindow>

class CollectionTab;
class QCloseEvent;
class QPushButton;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QSplitter;
class QTabWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class RequestTab;
class LogPane;
class RequestLogModel;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    static QString applicationStyleSheet();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct RequestSession;
    enum class ItemType { Collection = 1, Request = 2 };

    void setupUi();
    void setupConnections();
    void setupShortcuts();
    void loadState();
    void rebuildTree();
    void rebuildSessions();
    void restoreTabs();
    void refreshOpenTabs();
    void refreshCollectionTab(const QUuid &collectionId);
    void refreshRequestTab(const QUuid &requestId);
    void refreshTreeStatuses();
    void filterTree();
    void toggleTheme();
    void updateThemeButton();
    bool changeLanguage(const QString &language);
    void showOverview();
    void refreshOverview(bool replaceLog = false);
    void stopAll();
    void clearAllLogs();
    void importWorkspace();
    void exportWorkspace(const QUuid &collectionId = {});
    void replayEntry(const RequestLogEntry &entry);

    CollectionDefinition *collection(const QUuid &id);
    const CollectionDefinition *collection(const QUuid &id) const;
    RequestDefinition *request(const QUuid &id, CollectionDefinition **owner = nullptr);
    const RequestDefinition *request(const QUuid &id,
                                     const CollectionDefinition **owner = nullptr) const;
    RequestSession *session(const QUuid &requestId) const;
    void createSession(const RequestDefinition &request);

    void openCollection(const QUuid &collectionId);
    void openRequest(const QUuid &requestId);
    void closeTab(int index);
    void addTabCloseButton(QWidget *tab, int index);
    QString tabKey(int index) const;
    int tabIndex(const QString &kind, const QUuid &id) const;

    void addCollection();
    void addRequest(const QUuid &collectionId = {});
    void duplicateItem(QTreeWidgetItem *item);
    void deleteItem(QTreeWidgetItem *item);
    void renameItem(QTreeWidgetItem *item);
    void showTreeContextMenu(const QPoint &position);
    void applyTreeOrder();
    bool treeStructureValid() const;

    void updateCollectionDefinition(const CollectionDefinition &definition);
    void updateRequestDefinition(const RequestDefinition &definition);
    void sendOnce(const QUuid &requestId);
    void startRequest(const QUuid &requestId);
    void stopRequest(const QUuid &requestId);
    void startCollection(const QUuid &collectionId);
    void stopCollection(const QUuid &collectionId);
    void clearRequestLog(const QUuid &requestId);
    void clearCollectionLog(const QUuid &collectionId);
    void handleLogEntry(const QUuid &requestId, const RequestLogEntry &entry);
    QVector<RequestLogEntry> aggregateEntries(const QUuid &collectionId) const;
    RequestCounters aggregateCounters(const QUuid &collectionId) const;

    void scheduleWorkspaceSave();
    void scheduleHistorySave();
    bool saveWorkspace();
    bool saveHistory();
    bool saveUiSettings();
    bool saveAll();
    void showStorageError(const QString &message);

    static QString objectKey(ItemType type, const QUuid &id);
    static QUuid idFromItem(const QTreeWidgetItem *item);
    static ItemType typeFromItem(const QTreeWidgetItem *item);

    SettingsStore m_settingsStore;
    WorkspaceStore m_workspaceStore;
    ApplicationSettings m_uiSettings;
    Workspace m_workspace;
    QHash<QUuid, QVector<RequestLogEntry>> m_loadedHistory;
    QHash<QUuid, RequestSession *> m_sessions;
    QHash<QUuid, RequestTab *> m_requestTabs;
    QHash<QUuid, CollectionTab *> m_collectionTabs;

    QSplitter *m_workspaceSplitter = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTabWidget *m_tabs = nullptr;
    QStackedWidget *m_workspacePages = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_collectionCount = nullptr;
    QLabel *m_searchEmpty = nullptr;
    QLabel *m_saveStatus = nullptr;
    QPushButton *m_themeButton = nullptr;
    QPushButton *m_stopAllButton = nullptr;
    QLabel *m_overviewSummary = nullptr;
    QVector<QLabel *> m_overviewMetrics;
    QPushButton *m_workspaceHome = nullptr;
    LogPane *m_overviewLog = nullptr;
    RequestLogModel *m_overviewModel = nullptr;
    QStringList m_searchExpanded;
    bool m_filtering = false;
    bool m_searchActive = false;
    QPushButton *m_addCollectionButton = nullptr;
    QPushButton *m_addRequestButton = nullptr;
    QTimer *m_workspaceSaveTimer = nullptr;
    QTimer *m_historySaveTimer = nullptr;
    bool m_rebuildingTree = false;
    bool m_closing = false;
    bool m_restartForLanguage = false;
};
