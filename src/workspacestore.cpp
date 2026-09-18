#include "uilanguage.h"
#include "workspacestore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QUuid uuidFrom(const QJsonValue &value)
{
    return QUuid(value.toString());
}

QJsonArray headersToJson(const QVector<HttpHeader> &headers, bool utf8 = false)
{
    QJsonArray array;
    for (const HttpHeader &header : headers) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), utf8 ? QString::fromUtf8(header.name) : QString::fromLatin1(header.name));
        object.insert(QStringLiteral("value"), utf8 ? QString::fromUtf8(header.value) : QString::fromLatin1(header.value));
        object.insert(QStringLiteral("enabled"), header.enabled);
        array.append(object);
    }
    return array;
}

QVector<HttpHeader> headersFromJson(const QJsonValue &value, bool utf8 = false)
{
    const QJsonArray array = value.toArray();
    QVector<HttpHeader> headers;
    headers.reserve(array.size());
    for (const QJsonValue &item : array) {
        const QJsonObject object = item.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString value = object.value(QStringLiteral("value")).toString();
        headers.append({utf8 ? name.toUtf8() : name.toLatin1(), utf8 ? value.toUtf8() : value.toLatin1(),
                        object.value(QStringLiteral("enabled")).toBool(true)});
    }
    return headers;
}

QJsonObject requestToJson(const RequestDefinition &request)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), uuidText(request.id));
    object.insert(QStringLiteral("name"), request.name);
    object.insert(QStringLiteral("protocol"), requestProtocolName(request.protocol));
    object.insert(QStringLiteral("target"), request.target);
    object.insert(QStringLiteral("method"), httpMethodName(request.method));
    object.insert(QStringLiteral("body"), request.body);
    object.insert(QStringLiteral("headers"), headersToJson(request.headers, true));
    object.insert(QStringLiteral("httpScheme"), request.httpScheme);
    object.insert(QStringLiteral("httpTimeoutMs"), request.httpTimeoutMs);
    object.insert(QStringLiteral("waitForResponse"), request.waitForResponse);
    object.insert(QStringLiteral("timeoutMs"), request.timeoutMs);
    object.insert(QStringLiteral("serial"), QJsonObject{
        {QStringLiteral("portName"), request.serial.portName},
        {QStringLiteral("baudRate"), request.serial.baudRate},
        {QStringLiteral("dataBits"), request.serial.dataBits},
        {QStringLiteral("parity"), request.serial.parity},
        {QStringLiteral("stopBits"), request.serial.stopBits},
        {QStringLiteral("flowControl"), request.serial.flowControl},
    });
    object.insert(QStringLiteral("host"), request.host.value);
    object.insert(QStringLiteral("overrideHost"), request.host.overridden);
    object.insert(QStringLiteral("port"), static_cast<int>(request.port.value));
    object.insert(QStringLiteral("overridePort"), request.port.overridden);
    object.insert(QStringLiteral("intervalMs"), request.intervalMs.value);
    object.insert(QStringLiteral("overrideIntervalMs"), request.intervalMs.overridden);
    object.insert(QStringLiteral("maxInFlight"), request.maxInFlight.value);
    object.insert(QStringLiteral("overrideMaxInFlight"), request.maxInFlight.overridden);
    return object;
}

RequestDefinition requestFromJson(const QJsonObject &object)
{
    RequestDefinition request;
    const QUuid id = uuidFrom(object.value(QStringLiteral("id")));
    if (!id.isNull()) {
        request.id = id;
    }
    request.name = object.value(QStringLiteral("name")).toString(request.name);
    request.protocol = requestProtocolFromString(
        object.value(QStringLiteral("protocol")).toString());
    request.target = object.value(QStringLiteral("target")).toString(request.target);
    request.method = httpMethodFromString(object.value(QStringLiteral("method")).toString());
    request.body = object.value(QStringLiteral("body")).toString();
    request.headers = headersFromJson(object.value(QStringLiteral("headers")), true);
    request.httpScheme = object.value(QStringLiteral("httpScheme")).toString(request.httpScheme).toLower();
    request.httpTimeoutMs = object.value(QStringLiteral("httpTimeoutMs")).toInt(request.httpTimeoutMs);
    request.waitForResponse = object.value(QStringLiteral("waitForResponse"))
                                  .toBool(request.waitForResponse);
    request.timeoutMs = object.value(QStringLiteral("timeoutMs")).toInt(request.timeoutMs);
    const QJsonObject serial = object.value(QStringLiteral("serial")).toObject();
    request.serial.portName = serial.value(QStringLiteral("portName"))
                                  .toString(request.serial.portName);
    request.serial.baudRate = serial.value(QStringLiteral("baudRate"))
                                  .toInt(request.serial.baudRate);
    request.serial.dataBits = static_cast<QSerialPort::DataBits>(
        serial.value(QStringLiteral("dataBits")).toInt(request.serial.dataBits));
    request.serial.parity = static_cast<QSerialPort::Parity>(
        serial.value(QStringLiteral("parity")).toInt(request.serial.parity));
    request.serial.stopBits = static_cast<QSerialPort::StopBits>(
        serial.value(QStringLiteral("stopBits")).toInt(request.serial.stopBits));
    request.serial.flowControl = static_cast<QSerialPort::FlowControl>(
        serial.value(QStringLiteral("flowControl")).toInt(request.serial.flowControl));
    request.host.value = object.value(QStringLiteral("host")).toString(request.host.value);
    request.host.overridden = object.value(QStringLiteral("overrideHost")).toBool();
    request.port.value = static_cast<quint16>(
        object.value(QStringLiteral("port")).toInt(request.port.value));
    request.port.overridden = object.value(QStringLiteral("overridePort")).toBool();
    request.intervalMs.value = object.value(QStringLiteral("intervalMs"))
                                   .toInt(request.intervalMs.value);
    request.intervalMs.overridden = object.value(QStringLiteral("overrideIntervalMs")).toBool();
    request.maxInFlight.value = object.value(QStringLiteral("maxInFlight"))
                                    .toInt(request.maxInFlight.value);
    request.maxInFlight.overridden = object.value(QStringLiteral("overrideMaxInFlight")).toBool();
    return request;
}

QJsonObject configToJson(const RequestConfig &config)
{
    RequestDefinition request;
    request.id = config.requestId;
    request.name = config.requestName;
    request.protocol = config.protocol;
    request.target = config.target;
    request.method = config.method;
    request.body = config.body;
    request.headers = config.headers;
    request.httpScheme = config.httpScheme;
    request.httpTimeoutMs = config.httpTimeoutMs;
    request.waitForResponse = config.waitForResponse;
    request.timeoutMs = config.timeoutMs;
    request.serial = config.serial;
    request.host = {config.host, true};
    request.port = {config.port, true};
    request.intervalMs = {config.intervalMs, true};
    request.maxInFlight = {config.maxInFlight, true};
    QJsonObject object = requestToJson(request);
    object.insert(QStringLiteral("collectionId"), uuidText(config.collectionId));
    object.insert(QStringLiteral("collectionName"), config.collectionName);
    return object;
}

RequestConfig configFromJson(const QJsonObject &object)
{
    CollectionDefinition collection;
    collection.id = uuidFrom(object.value(QStringLiteral("collectionId")));
    collection.name = object.value(QStringLiteral("collectionName")).toString();
    RequestDefinition request = requestFromJson(object);
    request.id = uuidFrom(object.value(QStringLiteral("id")));
    request.host.overridden = request.port.overridden = true;
    request.intervalMs.overridden = request.maxInFlight.overridden = true;
    return resolveRequestConfig(collection, request);
}

QJsonObject logEntryToJson(const RequestLogEntry &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("entryId"), uuidText(entry.entryId));
    object.insert(QStringLiteral("sequence"), QString::number(entry.id));
    object.insert(QStringLiteral("collectionId"), uuidText(entry.collectionId));
    object.insert(QStringLiteral("requestId"), uuidText(entry.requestId));
    object.insert(QStringLiteral("collectionName"), entry.collectionName);
    object.insert(QStringLiteral("requestName"), entry.requestName);
    object.insert(QStringLiteral("protocol"), requestProtocolName(entry.protocol));
    object.insert(QStringLiteral("sentAt"), entry.sentAt.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("completedAt"), entry.completedAt.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("method"), httpMethodName(entry.method));
    object.insert(QStringLiteral("result"), static_cast<int>(entry.result));
    object.insert(QStringLiteral("url"), entry.url.toString(QUrl::FullyEncoded));
    object.insert(QStringLiteral("requestBody"), entry.requestBody);
    object.insert(QStringLiteral("responseBody"), entry.responseBody);
    if (entry.requestBytesAvailable)
        object.insert(QStringLiteral("requestBytesBase64"), QString::fromLatin1(entry.requestBytes.toBase64()));
    if (entry.responseBytesAvailable)
        object.insert(QStringLiteral("responseBytesBase64"), QString::fromLatin1(entry.responseBytes.toBase64()));
    if (entry.responseTruncated)
        object.insert(QStringLiteral("responseTruncated"), true);
    if (entry.configAvailable)
        object.insert(QStringLiteral("configSnapshot"), configToJson(entry.configSnapshot));
    object.insert(QStringLiteral("requestHeaders"), headersToJson(entry.requestHeaders));
    object.insert(QStringLiteral("responseHeaders"), headersToJson(entry.responseHeaders));
    object.insert(QStringLiteral("errorText"), entry.errorText);
    object.insert(QStringLiteral("httpStatus"), entry.httpStatus);
    object.insert(QStringLiteral("elapsedMs"), QString::number(entry.elapsedMs));
    return object;
}

RequestLogEntry logEntryFromJson(const QJsonObject &object)
{
    RequestLogEntry entry;
    const QUuid entryId = uuidFrom(object.value(QStringLiteral("entryId")));
    if (!entryId.isNull()) {
        entry.entryId = entryId;
    }
    entry.id = object.value(QStringLiteral("sequence")).toString().toULongLong();
    entry.collectionId = uuidFrom(object.value(QStringLiteral("collectionId")));
    entry.requestId = uuidFrom(object.value(QStringLiteral("requestId")));
    entry.collectionName = object.value(QStringLiteral("collectionName")).toString();
    entry.requestName = object.value(QStringLiteral("requestName")).toString();
    entry.protocol = requestProtocolFromString(
        object.value(QStringLiteral("protocol")).toString());
    entry.sentAt = QDateTime::fromString(object.value(QStringLiteral("sentAt")).toString(),
                                        Qt::ISODateWithMs);
    entry.completedAt = QDateTime::fromString(
        object.value(QStringLiteral("completedAt")).toString(), Qt::ISODateWithMs);
    entry.method = httpMethodFromString(object.value(QStringLiteral("method")).toString());
    entry.result = static_cast<RequestResult>(object.value(QStringLiteral("result")).toInt());
    entry.url = QUrl(object.value(QStringLiteral("url")).toString());
    entry.requestBody = object.value(QStringLiteral("requestBody")).toString();
    entry.responseBody = object.value(QStringLiteral("responseBody")).toString();
    const auto restoreBytes = [&object](const QString &key, QByteArray &bytes, bool &available) {
        const QJsonValue value = object.value(key);
        if (!value.isString())
            return;
        const auto decoded = QByteArray::fromBase64Encoding(value.toString().toLatin1(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (decoded && decoded.decoded.size() <= MaxPayloadBytes) {
            bytes = decoded.decoded;
            available = true;
        }
    };
    restoreBytes(QStringLiteral("requestBytesBase64"), entry.requestBytes, entry.requestBytesAvailable);
    restoreBytes(QStringLiteral("responseBytesBase64"), entry.responseBytes, entry.responseBytesAvailable);
    entry.responseTruncated = object.value(QStringLiteral("responseTruncated")).toBool();
    if (object.value(QStringLiteral("configSnapshot")).isObject()) {
        entry.configSnapshot = configFromJson(object.value(QStringLiteral("configSnapshot")).toObject());
        entry.configAvailable = true;
    }
    entry.requestHeaders = headersFromJson(object.value(QStringLiteral("requestHeaders")));
    entry.responseHeaders = headersFromJson(object.value(QStringLiteral("responseHeaders")));
    entry.errorText = object.value(QStringLiteral("errorText")).toString();
    entry.httpStatus = object.value(QStringLiteral("httpStatus")).toInt();
    entry.elapsedMs = object.value(QStringLiteral("elapsedMs")).toString().toLongLong();
    return entry;
}

bool writeJson(const QString &path, const QJsonDocument &document, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

QJsonDocument readJson(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError && error) {
        *error = parseError.errorString();
    }
    return document;
}

void preserveCorruptedFile(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return;
    }
    const QString suffix = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QFile::copy(path, path + QStringLiteral(".corrupt-") + suffix);
}

Workspace defaultWorkspace(const RequestConfig &legacy)
{
    Workspace workspace;
    CollectionDefinition collection;
    collection.name = UiLanguage::text("Моя коллекция");
    collection.host = legacy.host;
    collection.port = legacy.port;
    collection.intervalMs = legacy.intervalMs;
    collection.maxInFlight = legacy.maxInFlight;
    RequestDefinition request;
    request.name = UiLanguage::text("Запрос 1");
    request.protocol = legacy.protocol;
    request.target = legacy.target;
    request.method = legacy.method;
    request.body = legacy.body;
    request.headers = legacy.headers;
    request.httpScheme = legacy.httpScheme;
    request.httpTimeoutMs = legacy.httpTimeoutMs;
    request.waitForResponse = legacy.waitForResponse;
    request.timeoutMs = legacy.timeoutMs;
    request.serial = legacy.serial;
    request.host.value = legacy.host;
    request.port.value = legacy.port;
    request.intervalMs.value = legacy.intervalMs;
    request.maxInFlight.value = legacy.maxInFlight;
    collection.requests.append(request);
    workspace.collections.append(collection);
    return workspace;
}

} // namespace

WorkspaceStore::WorkspaceStore(const QString &applicationName)
{
    const QDir directory(QCoreApplication::applicationDirPath());
    m_workspacePath = directory.filePath(applicationName + QStringLiteral(".workspace.json"));
    m_historyPath = directory.filePath(applicationName + QStringLiteral(".history.json"));
}

QString WorkspaceStore::workspacePath() const { return m_workspacePath; }
QString WorkspaceStore::historyPath() const { return m_historyPath; }

Workspace WorkspaceStore::loadWorkspace(const RequestConfig &legacyRequest, QString *error) const
{
    const QDir directory(QCoreApplication::applicationDirPath());
    const QString widgetsPath = directory.filePath(QStringLiteral("MultiSender.workspace.json"));
    const QString loadPath = QFileInfo::exists(m_workspacePath) ? m_workspacePath
        : QFileInfo::exists(widgetsPath) ? widgetsPath
        : directory.filePath(QStringLiteral("HttpRequester.workspace.json"));
    if (!QFileInfo::exists(loadPath)) {
        return defaultWorkspace(legacyRequest);
    }
    QString readError;
    const QJsonDocument document = readJson(loadPath, &readError);
    if (!document.isObject()) {
        preserveCorruptedFile(loadPath);
        if (error) {
            *error = UiLanguage::text("Не удалось прочитать рабочее пространство: %1").arg(readError);
        }
        return defaultWorkspace(legacyRequest);
    }

    Workspace workspace;
    if (!workspaceFromJson(document.object(), &workspace, &readError)) {
        preserveCorruptedFile(loadPath);
        if (error)
            *error = UiLanguage::text("Не удалось прочитать рабочее пространство: %1").arg(readError);
        return defaultWorkspace(legacyRequest);
    }
    return workspace;
}

bool WorkspaceStore::workspaceFromJson(const QJsonObject &root, Workspace *result, QString *error)
{
    if (!result || !root.value(QStringLiteral("collections")).isArray()) {
        if (error)
            *error = UiLanguage::text("Ожидается объект с массивом collections");
        return false;
    }
    Workspace workspace;
    workspace.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
    const QJsonArray collections = root.value(QStringLiteral("collections")).toArray();
    for (const QJsonValue &value : collections) {
        const QJsonObject object = value.toObject();
        CollectionDefinition collection;
        const QUuid id = uuidFrom(object.value(QStringLiteral("id")));
        if (!id.isNull()) {
            collection.id = id;
        }
        collection.name = object.value(QStringLiteral("name")).toString(collection.name);
        collection.host = object.value(QStringLiteral("host")).toString(collection.host);
        collection.port = static_cast<quint16>(object.value(QStringLiteral("port"))
                                                   .toInt(collection.port));
        collection.intervalMs = object.value(QStringLiteral("intervalMs"))
                                    .toInt(collection.intervalMs);
        collection.maxInFlight = object.value(QStringLiteral("maxInFlight"))
                                     .toInt(collection.maxInFlight);
        for (const QJsonValue &request : object.value(QStringLiteral("requests")).toArray()) {
            collection.requests.append(requestFromJson(request.toObject()));
        }
        workspace.collections.append(collection);
    }
    *result = std::move(workspace);
    return true;
}

QJsonObject WorkspaceStore::workspaceToJson(const Workspace &workspace)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), workspace.schemaVersion);
    QJsonArray collections;
    for (const CollectionDefinition &collection : workspace.collections) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), uuidText(collection.id));
        object.insert(QStringLiteral("name"), collection.name);
        object.insert(QStringLiteral("host"), collection.host);
        object.insert(QStringLiteral("port"), static_cast<int>(collection.port));
        object.insert(QStringLiteral("intervalMs"), collection.intervalMs);
        object.insert(QStringLiteral("maxInFlight"), collection.maxInFlight);
        QJsonArray requests;
        for (const RequestDefinition &request : collection.requests) {
            requests.append(requestToJson(request));
        }
        object.insert(QStringLiteral("requests"), requests);
        collections.append(object);
    }
    root.insert(QStringLiteral("collections"), collections);
    return root;
}

bool WorkspaceStore::saveWorkspace(const Workspace &workspace, QString *error) const
{
    return writeJson(m_workspacePath, QJsonDocument(workspaceToJson(workspace)), error);
}

QHash<QUuid, QVector<RequestLogEntry>> WorkspaceStore::loadHistory(QString *error) const
{
    QHash<QUuid, QVector<RequestLogEntry>> result;
    const QDir directory(QCoreApplication::applicationDirPath());
    const QString widgetsPath = directory.filePath(QStringLiteral("MultiSender.history.json"));
    const QString loadPath = QFileInfo::exists(m_historyPath) ? m_historyPath
        : QFileInfo::exists(widgetsPath) ? widgetsPath
        : directory.filePath(QStringLiteral("HttpRequester.history.json"));
    if (!QFileInfo::exists(loadPath)) {
        return result;
    }
    QString readError;
    const QJsonDocument document = readJson(loadPath, &readError);
    if (!document.isObject()) {
        preserveCorruptedFile(loadPath);
        if (error) {
            *error = UiLanguage::text("Не удалось прочитать историю: %1").arg(readError);
        }
        return result;
    }
    const QJsonObject requests = document.object().value(QStringLiteral("requests")).toObject();
    for (auto it = requests.begin(); it != requests.end(); ++it) {
        const QUuid requestId(it.key());
        QVector<RequestLogEntry> entries;
        for (const QJsonValue &value : it.value().toArray()) {
            entries.append(logEntryFromJson(value.toObject()));
        }
        if (!requestId.isNull()) {
            result.insert(requestId, entries.mid(qMax(0, entries.size() - 1000)));
        }
    }
    return result;
}

bool WorkspaceStore::saveHistory(const QHash<QUuid, QVector<RequestLogEntry>> &history,
                                 QString *error) const
{
    QJsonObject requests;
    for (auto it = history.cbegin(); it != history.cend(); ++it) {
        QJsonArray entries;
        const QVector<RequestLogEntry> limited = it.value().mid(qMax(0, it.value().size() - 1000));
        for (const RequestLogEntry &entry : limited) {
            entries.append(logEntryToJson(entry));
        }
        requests.insert(uuidText(it.key()), entries);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("requests"), requests);
    return writeJson(m_historyPath, QJsonDocument(root), error);
}
