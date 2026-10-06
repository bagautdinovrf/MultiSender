#include "settingsstore.h"
#include "uilanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

SettingsStore::SettingsStore(const QString &applicationName)
    : m_filePath(QDir(QCoreApplication::applicationDirPath())
                     .filePath(applicationName + QStringLiteral(".ini")))
{
}

QString SettingsStore::filePath() const
{
    return m_filePath;
}

ApplicationSettings SettingsStore::load() const
{
    const QDir directory(QCoreApplication::applicationDirPath());
    const QString widgetsPath = directory.filePath(QStringLiteral("MultiSender.ini"));
    const QString loadPath = QFileInfo::exists(m_filePath) ? m_filePath
        : QFileInfo::exists(widgetsPath) ? widgetsPath
        : directory.filePath(QStringLiteral("HttpRequester.ini"));
    QSettings settings(loadPath, QSettings::IniFormat);
    ApplicationSettings result;

    settings.beginGroup(QStringLiteral("Request"));
    result.request.protocol = requestProtocolFromString(
        settings.value(QStringLiteral("Protocol"),
                       requestProtocolName(result.request.protocol)).toString());
    result.request.host = settings.value(QStringLiteral("Host"), result.request.host).toString();
    result.request.port = static_cast<quint16>(
        settings.value(QStringLiteral("Port"), result.request.port).toUInt());
    result.request.target = settings.value(QStringLiteral("Target"), result.request.target).toString();
    result.request.method = httpMethodFromString(
        settings.value(QStringLiteral("Method"), httpMethodName(result.request.method)).toString());
    result.request.body = settings.value(QStringLiteral("Body")).toString();
    result.request.httpScheme = settings.value(QStringLiteral("HttpScheme"), result.request.httpScheme).toString().toLower();
    result.request.httpTimeoutMs = settings.value(QStringLiteral("HttpTimeoutMs"), result.request.httpTimeoutMs).toInt();
    const int headerCount = settings.beginReadArray(QStringLiteral("Headers"));
    for (int i = 0; i < headerCount; ++i) {
        settings.setArrayIndex(i);
        result.request.headers.append({settings.value(QStringLiteral("Name")).toByteArray(),
                                       settings.value(QStringLiteral("Value")).toByteArray(),
                                       settings.value(QStringLiteral("Enabled"), true).toBool()});
    }
    settings.endArray();
    result.request.waitForResponse = settings.value(QStringLiteral("WaitForResponse"),
                                                    result.request.waitForResponse).toBool();
    result.request.timeoutMs = settings.value(QStringLiteral("TimeoutMs"),
                                              result.request.timeoutMs).toInt();
    settings.beginGroup(QStringLiteral("Serial"));
    result.request.serial.portName = settings.value(QStringLiteral("PortName"),
        result.request.serial.portName).toString();
    result.request.serial.baudRate = settings.value(QStringLiteral("BaudRate"),
        result.request.serial.baudRate).toInt();
    result.request.serial.dataBits = static_cast<QSerialPort::DataBits>(
        settings.value(QStringLiteral("DataBits"), result.request.serial.dataBits).toInt());
    result.request.serial.parity = static_cast<QSerialPort::Parity>(
        settings.value(QStringLiteral("Parity"), result.request.serial.parity).toInt());
    result.request.serial.stopBits = static_cast<QSerialPort::StopBits>(
        settings.value(QStringLiteral("StopBits"), result.request.serial.stopBits).toInt());
    result.request.serial.flowControl = static_cast<QSerialPort::FlowControl>(
        settings.value(QStringLiteral("FlowControl"), result.request.serial.flowControl).toInt());
    settings.endGroup();
    result.request.intervalMs = settings.value(QStringLiteral("IntervalMs"),
                                               result.request.intervalMs).toInt();
    result.request.maxInFlight = settings.value(QStringLiteral("MaxInFlight"),
                                                result.request.maxInFlight).toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Window"));
    result.language = UiLanguage::normalized(settings.value(QStringLiteral("Language"), QStringLiteral("ru")).toString());
    result.theme = settings.value(QStringLiteral("Theme"), QStringLiteral("dark")).toString()
                           .compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("light") : QStringLiteral("dark");
    result.windowGeometry = settings.value(QStringLiteral("Geometry")).toByteArray();
    result.splitterState = settings.value(QStringLiteral("SplitterState")).toByteArray();
    result.bodySplitterState = settings.value(QStringLiteral("BodySplitterState")).toByteArray();
    result.workspaceSplitterState = settings.value(QStringLiteral("WorkspaceSplitterState"))
                                        .toByteArray();
    result.requestVerticalSplitterState = settings.value(
        QStringLiteral("RequestVerticalSplitterState")).toByteArray();
    result.requestEditorSplitterState = settings.value(
        QStringLiteral("RequestEditorSplitterState")).toByteArray();
    result.transportBodyHeight = settings.value(QStringLiteral("TransportBodyHeight"), 0).toInt();
    result.logDetailsSplitterState = settings.value(QStringLiteral("LogDetailsSplitterState"))
                                         .toByteArray();
    result.collectionVerticalSplitterState = settings.value(
        QStringLiteral("CollectionVerticalSplitterState")).toByteArray();
    result.logDetailsVerticalSplitterState = settings.value(
        QStringLiteral("LogDetailsVerticalSplitterState")).toByteArray();
    result.openTabs = settings.value(QStringLiteral("OpenTabs")).toStringList();
    result.activeTab = settings.value(QStringLiteral("ActiveTab")).toString();
    result.selectedItem = settings.value(QStringLiteral("SelectedItem")).toString();
    result.expandedCollections = settings.value(QStringLiteral("ExpandedCollections"))
                                     .toStringList();
    result.autoScroll = settings.value(QStringLiteral("AutoScroll"), true).toBool();
    result.prettyJson = settings.value(QStringLiteral("PrettyJson"), false).toBool();
    settings.endGroup();

    return result;
}

bool SettingsStore::save(const ApplicationSettings &value) const
{
    QSettings settings(m_filePath, QSettings::IniFormat);

    settings.beginGroup(QStringLiteral("Request"));
    settings.setValue(QStringLiteral("Protocol"), requestProtocolName(value.request.protocol));
    settings.setValue(QStringLiteral("Host"), value.request.host);
    settings.setValue(QStringLiteral("Port"), value.request.port);
    settings.setValue(QStringLiteral("Target"), value.request.target);
    settings.setValue(QStringLiteral("Method"), httpMethodName(value.request.method));
    settings.setValue(QStringLiteral("Body"), value.request.body);
    settings.setValue(QStringLiteral("HttpScheme"), value.request.httpScheme);
    settings.setValue(QStringLiteral("HttpTimeoutMs"), value.request.httpTimeoutMs);
    settings.beginWriteArray(QStringLiteral("Headers"), value.request.headers.size());
    for (qsizetype i = 0; i < value.request.headers.size(); ++i) {
        settings.setArrayIndex(static_cast<int>(i));
        const HttpHeader &header = value.request.headers.at(i);
        settings.setValue(QStringLiteral("Name"), header.name);
        settings.setValue(QStringLiteral("Value"), header.value);
        settings.setValue(QStringLiteral("Enabled"), header.enabled);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("WaitForResponse"), value.request.waitForResponse);
    settings.setValue(QStringLiteral("TimeoutMs"), value.request.timeoutMs);
    settings.beginGroup(QStringLiteral("Serial"));
    settings.setValue(QStringLiteral("PortName"), value.request.serial.portName);
    settings.setValue(QStringLiteral("BaudRate"), value.request.serial.baudRate);
    settings.setValue(QStringLiteral("DataBits"), value.request.serial.dataBits);
    settings.setValue(QStringLiteral("Parity"), value.request.serial.parity);
    settings.setValue(QStringLiteral("StopBits"), value.request.serial.stopBits);
    settings.setValue(QStringLiteral("FlowControl"), value.request.serial.flowControl);
    settings.endGroup();
    settings.setValue(QStringLiteral("IntervalMs"), value.request.intervalMs);
    settings.setValue(QStringLiteral("MaxInFlight"), value.request.maxInFlight);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Window"));
    settings.setValue(QStringLiteral("Language"), UiLanguage::normalized(value.language));
    settings.setValue(QStringLiteral("Theme"),
                      value.theme.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0
                          ? QStringLiteral("light") : QStringLiteral("dark"));
    settings.setValue(QStringLiteral("Geometry"), value.windowGeometry);
    settings.setValue(QStringLiteral("SplitterState"), value.splitterState);
    settings.setValue(QStringLiteral("BodySplitterState"), value.bodySplitterState);
    settings.setValue(QStringLiteral("WorkspaceSplitterState"), value.workspaceSplitterState);
    settings.setValue(QStringLiteral("RequestVerticalSplitterState"),
                      value.requestVerticalSplitterState);
    settings.setValue(QStringLiteral("RequestEditorSplitterState"),
                      value.requestEditorSplitterState);
    settings.setValue(QStringLiteral("TransportBodyHeight"), value.transportBodyHeight);
    settings.setValue(QStringLiteral("LogDetailsSplitterState"),
                      value.logDetailsSplitterState);
    settings.setValue(QStringLiteral("CollectionVerticalSplitterState"),
                      value.collectionVerticalSplitterState);
    settings.setValue(QStringLiteral("LogDetailsVerticalSplitterState"),
                      value.logDetailsVerticalSplitterState);
    settings.setValue(QStringLiteral("OpenTabs"), value.openTabs);
    settings.setValue(QStringLiteral("ActiveTab"), value.activeTab);
    settings.setValue(QStringLiteral("SelectedItem"), value.selectedItem);
    settings.setValue(QStringLiteral("ExpandedCollections"), value.expandedCollections);
    settings.setValue(QStringLiteral("AutoScroll"), value.autoScroll);
    settings.setValue(QStringLiteral("PrettyJson"), value.prettyJson);
    settings.endGroup();

    settings.sync();
    return settings.status() == QSettings::NoError;
}
