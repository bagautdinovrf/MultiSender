#pragma once

#include "requesttypes.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

class WorkspaceStore final
{
public:
    explicit WorkspaceStore(const QString &applicationName = QStringLiteral("MultiSender"));

    QString workspacePath() const;
    QString historyPath() const;
    static QJsonObject workspaceToJson(const Workspace &workspace);
    static bool workspaceFromJson(const QJsonObject &object, Workspace *workspace,
                                   QString *error = nullptr);
    Workspace loadWorkspace(const RequestConfig &legacyRequest, QString *error = nullptr) const;
    bool saveWorkspace(const Workspace &workspace, QString *error = nullptr) const;
    QHash<QUuid, QVector<RequestLogEntry>> loadHistory(QString *error = nullptr) const;
    bool saveHistory(const QHash<QUuid, QVector<RequestLogEntry>> &history,
                     QString *error = nullptr) const;

private:
    QString m_workspacePath;
    QString m_historyPath;
};
