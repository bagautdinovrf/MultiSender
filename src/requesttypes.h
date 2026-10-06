#pragma once

#include "uilanguage.h"

#include <QByteArray>
#include <QDateTime>
#include <QSerialPort>
#include <QString>
#include <QUrl>
#include <QUuid>
#include <QVector>

enum class RequestProtocol
{
    Http,
    Tcp,
    Udp,
    Serial
};

enum class HttpMethod
{
    Get,
    Post,
    Put,
    Patch,
    Delete,
    Head,
    Options
};

enum class RequestResult
{
    Pending,
    Success,
    HttpError,
    NetworkError,
    Cancelled,
    Skipped,
    SerialError
};

struct SerialPortSettings
{
    QString portName = QStringLiteral("COM1");
    qint32 baudRate = 9600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
};

struct HttpHeader
{
    QByteArray name;
    QByteArray value;
    bool enabled = true;
};

inline constexpr qsizetype MaxPayloadBytes = 16 * 1024 * 1024;

struct RequestConfig
{
    RequestProtocol protocol = RequestProtocol::Http;
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 0; // HTTP/HTTPS use the scheme's default port when zero.
    QString target = QStringLiteral("/test");
    HttpMethod method = HttpMethod::Get;
    QString body;
    QVector<HttpHeader> headers;
    QString httpScheme = QStringLiteral("http");
    int httpTimeoutMs = 30'000;
    bool waitForResponse = false;
    int timeoutMs = 3000;
    SerialPortSettings serial;
    int intervalMs = 1000;
    int maxInFlight = 100;
    QUuid collectionId;
    QUuid requestId;
    QString collectionName;
    QString requestName;
};

template<typename T>
struct InheritedValue
{
    T value{};
    bool overridden = false;
};

struct RequestDefinition
{
    RequestProtocol protocol = RequestProtocol::Http;
    QUuid id = QUuid::createUuid();
    QString name = UiLanguage::text("Новый запрос");
    QString target = QStringLiteral("/test");
    HttpMethod method = HttpMethod::Get;
    QString body;
    QVector<HttpHeader> headers;
    QString httpScheme = QStringLiteral("http");
    int httpTimeoutMs = 30'000;
    bool waitForResponse = false;
    int timeoutMs = 3000;
    SerialPortSettings serial;
    InheritedValue<QString> host{QStringLiteral("127.0.0.1"), false};
    InheritedValue<quint16> port{0, false};
    InheritedValue<int> intervalMs{1000, false};
    InheritedValue<int> maxInFlight{100, false};
};

struct CollectionDefinition
{
    QUuid id = QUuid::createUuid();
    QString name = UiLanguage::text("Новая коллекция");
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 0;
    int intervalMs = 1000;
    int maxInFlight = 100;
    QVector<RequestDefinition> requests;
};

struct Workspace
{
    int schemaVersion = 1;
    QVector<CollectionDefinition> collections;
};

struct RequestLogEntry
{
    RequestProtocol protocol = RequestProtocol::Http;
    QUuid entryId = QUuid::createUuid();
    quint64 id = 0;
    QUuid collectionId;
    QUuid requestId;
    QString collectionName;
    QString requestName;
    QDateTime sentAt;
    QDateTime completedAt;
    HttpMethod method = HttpMethod::Get;
    RequestResult result = RequestResult::Pending;
    QUrl url;
    QString requestBody;
    QString responseBody;
    QByteArray requestBytes;
    QByteArray responseBytes;
    bool requestBytesAvailable = false;
    bool responseBytesAvailable = false;
    bool responseTruncated = false;
    RequestConfig configSnapshot;
    bool configAvailable = false;
    QVector<HttpHeader> requestHeaders;
    QVector<HttpHeader> responseHeaders;
    QString errorText;
    int httpStatus = 0;
    qint64 elapsedMs = 0;
};

struct RequestCounters
{
    quint64 sent = 0;
    quint64 completed = 0;
    quint64 errors = 0;
    quint64 skipped = 0;
    int active = 0;
};

QString httpMethodName(HttpMethod method);
HttpMethod httpMethodFromString(const QString &value);
bool httpMethodHasBody(HttpMethod method);
QString requestProtocolName(RequestProtocol protocol);
RequestProtocol requestProtocolFromString(const QString &value);
QString requestOperationName(RequestProtocol protocol, HttpMethod method);
QString requestResultName(RequestResult result, int httpStatus = 0);
QUrl buildRequestUrl(const RequestConfig &config);
QString validateRequestConfig(const RequestConfig &config);
RequestConfig resolveRequestConfig(const CollectionDefinition &collection,
                                   const RequestDefinition &request);
