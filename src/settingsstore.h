#pragma once

#include "requesttypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

struct ApplicationSettings
{
    RequestConfig request;
    QByteArray windowGeometry;
    QByteArray splitterState;
    QByteArray bodySplitterState;
    QByteArray workspaceSplitterState;
    QByteArray requestVerticalSplitterState;
    QByteArray requestEditorSplitterState;
    int transportBodyHeight = 0;
    QByteArray logDetailsSplitterState;
    QByteArray collectionVerticalSplitterState;
    QByteArray logDetailsVerticalSplitterState;
    QStringList openTabs;
    QString activeTab;
    QString selectedItem;
    QStringList expandedCollections;
    bool autoScroll = true;
    bool prettyJson = false;
    QString theme = QStringLiteral("dark");
    QString language = QStringLiteral("ru");
};

class SettingsStore final
{
public:
    explicit SettingsStore(const QString &applicationName = QStringLiteral("MultiSender"));

    QString filePath() const;
    ApplicationSettings load() const;
    bool save(const ApplicationSettings &settings) const;

private:
    QString m_filePath;
};
