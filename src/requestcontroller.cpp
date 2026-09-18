#include "uilanguage.h"
#include "requestcontroller.h"
#include "payloadencoding.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QHttpHeaders>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSerialPort>
#include <QTcpSocket>
#include <QUdpSocket>

namespace {

QVector<HttpHeader> requestHeaders(const QNetworkRequest &request)
{
    QVector<HttpHeader> result;
    const auto pairs = request.headers().toListOfPairs();
    result.reserve(pairs.size());
    for (const auto &[name, value] : pairs) {
        result.append({name, value});
    }
    return result;
}

QVector<HttpHeader> responseHeaders(const QNetworkReply &reply)
{
    const QList<QNetworkReply::RawHeaderPair> pairs = reply.rawHeaderPairs();
    QVector<HttpHeader> result;
    result.reserve(pairs.size());
    for (const auto &[name, value] : pairs) {
        result.append({name, value});
    }
    return result;
}

QString serialPortKey(QString name)
{
    name = name.trimmed();
    if (name.startsWith(QStringLiteral("\\\\.\\"))) {
        name.remove(0, 4);
    }
    return name.toUpper();
}

QString serialErrorText(const QSerialPort &port)
{
    switch (port.error()) {
    case QSerialPort::PermissionError:
        return UiLanguage::text("Порт %1 занят другой программой или нет доступа: %2")
            .arg(port.portName(), port.errorString());
    case QSerialPort::DeviceNotFoundError:
        return UiLanguage::text("Порт %1 не найден: %2")
            .arg(port.portName(), port.errorString());
    case QSerialPort::ResourceError:
        return UiLanguage::text("Порт %1 недоступен или отключён: %2")
            .arg(port.portName(), port.errorString());
    case QSerialPort::UnsupportedOperationError:
        return UiLanguage::text("Параметры порта %1 не поддерживаются: %2")
            .arg(port.portName(), port.errorString());
    default:
        return UiLanguage::text("Ошибка COM-порта %1: %2")
            .arg(port.portName(), port.errorString());
    }
}

QString responseLimitError()
{
    return UiLanguage::text("Ответ превысил 16 МиБ. Получение остановлено; сохранены первые 16 МиБ.");
}

// Read one byte beyond the cap to detect overflow without allocating an unbounded chunk.
bool readBounded(QIODevice *device, QByteArray &response)
{
    const qsizetype remaining = qMax<qsizetype>(0, MaxPayloadBytes - response.size());
    const QByteArray chunk = device->read(remaining + 1);
    response.append(chunk.first(qMin(remaining, chunk.size())));
    return chunk.size() <= remaining;
}

} // namespace

struct RequestController::ActiveRequest
{
    enum class SocketPhase { Connecting, Writing, Reading };

    RequestLogEntry entry;
    QElapsedTimer elapsed;
    bool cancelled = false;
    bool responseTruncated = false;
    QString forcedError;
    QByteArray payload;
    QByteArray response;
    QHostAddress peerAddress;
    QString serialKey;
    QTimer *timeout = nullptr;
    SocketPhase phase = SocketPhase::Connecting;
    qint64 bytesAccepted = 0;
    qint64 bytesWritten = 0;
    int lookupId = -1;
    int timeoutMs = 3000;
    bool waitForResponse = false;
    bool writing = false;
};

RequestController::RequestController(QObject *parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { dispatch(m_periodicConfig); });
}

RequestController::~RequestController()
{
    m_timer.stop();
    const QList<QNetworkReply *> replies = m_activeRequests.keys();
    for (QNetworkReply *reply : replies) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        delete m_activeRequests.take(reply);
    }
    const QList<QAbstractSocket *> sockets = m_activeSockets.keys();
    for (QAbstractSocket *socket : sockets) {
        ActiveRequest *active = m_activeSockets.take(socket);
        active->timeout->stop();
        if (active->lookupId != -1) {
            QHostInfo::abortHostLookup(active->lookupId);
        }
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        delete active;
    }
    const QList<QSerialPort *> serialPorts = m_activeSerialPorts.keys();
    for (QSerialPort *port : serialPorts) {
        ActiveRequest *active = m_activeSerialPorts.take(port);
        active->timeout->stop();
        disconnect(port, nullptr, this, nullptr);
        if (port->isOpen()) {
            port->close();
        }
        delete active;
    }
}

bool RequestController::isRunning() const
{
    return m_running;
}

RequestCounters RequestController::counters() const
{
    return m_counters;
}

void RequestController::setNextRequestId(quint64 nextId)
{
    m_nextRequestId = qMax<quint64>(1, nextId);
}

void RequestController::sendOnce(const RequestConfig &config)
{
    dispatch(config);
}

void RequestController::start(const RequestConfig &config)
{
    if (m_running) {
        return;
    }
    m_periodicConfig = config;
    m_running = true;
    emit runningChanged(true);
    dispatch(m_periodicConfig);
    if (m_running) {
        m_timer.start(m_periodicConfig.intervalMs);
    }
}

void RequestController::stop()
{
    m_timer.stop();
    if (m_running) {
        m_running = false;
        emit runningChanged(false);
    }
    const QList<QNetworkReply *> replies = m_activeRequests.keys();
    for (QNetworkReply *reply : replies) {
        const auto it = m_activeRequests.constFind(reply);
        if (it == m_activeRequests.cend()) {
            continue;
        }
        it.value()->cancelled = true;
        reply->abort();
    }
    const QList<QAbstractSocket *> sockets = m_activeSockets.keys();
    for (QAbstractSocket *socket : sockets) {
        finishSocket(socket, RequestResult::Cancelled,
                     UiLanguage::text("Запрос отменён пользователем"));
    }
    const QList<QSerialPort *> serialPorts = m_activeSerialPorts.keys();
    for (QSerialPort *port : serialPorts) {
        finishSerial(port, RequestResult::Cancelled,
                     UiLanguage::text("Запрос отменён пользователем"));
    }
}

void RequestController::resetCounters()
{
    if (activeCount() != 0) {
        return;
    }
    m_counters = {};
    emitCounters();
}

void RequestController::dispatch(const RequestConfig &config)
{
    RequestLogEntry entry;
    entry.id = m_nextRequestId++;
    entry.collectionId = config.collectionId;
    entry.requestId = config.requestId;
    entry.collectionName = config.collectionName;
    entry.requestName = config.requestName;
    entry.sentAt = QDateTime::currentDateTime();
    entry.protocol = config.protocol;
    entry.method = config.method;
    entry.result = RequestResult::Pending;
    entry.url = buildRequestUrl(config);
    entry.configSnapshot = config;
    entry.configAvailable = true;
    entry.requestBody = config.protocol != RequestProtocol::Http || httpMethodHasBody(config.method)
        ? config.body : QString{};

    const QString validationError = validateRequestConfig(config);
    if (!validationError.isEmpty()) {
        entry.completedAt = entry.sentAt;
        entry.result = config.protocol == RequestProtocol::Serial
            ? RequestResult::SerialError : RequestResult::NetworkError;
        entry.errorText = validationError;
        ++m_counters.sent;
        emit requestStarted(entry);
        recordCompletion(entry);
        return;
    }
    entry.requestBytes = config.protocol == RequestProtocol::Http
        ? entry.requestBody.toUtf8() : parseSocketPayload(config.body).bytes;
    entry.requestBytesAvailable = true;
    QString skipReason;
    if (config.protocol == RequestProtocol::Serial) {
        const QString key = serialPortKey(config.serial.portName);
        for (const ActiveRequest *active : m_activeSerialPorts) {
            if (active->serialKey == key) {
                skipReason = UiLanguage::text("Порт %1 занят предыдущим запросом; "
                                            "дождитесь его завершения")
                                 .arg(config.serial.portName.trimmed());
                break;
            }
        }
    }
    if (skipReason.isEmpty() && config.protocol != RequestProtocol::Serial
        && activeCount() >= config.maxInFlight) {
        skipReason = UiLanguage::text("Достигнут лимит параллельных запросов: %1")
                         .arg(config.maxInFlight);
    }
    if (!skipReason.isEmpty()) {
        entry.completedAt = entry.sentAt;
        entry.result = RequestResult::Skipped;
        entry.errorText = skipReason;
        ++m_counters.skipped;
        emit requestSkipped(entry);
        emitCounters();
        return;
    }
    if (config.protocol == RequestProtocol::Http) {
        dispatchHttp(config, entry);
    } else if (config.protocol == RequestProtocol::Serial) {
        dispatchSerial(config, entry);
    } else {
        dispatchSocket(config, entry);
    }
}

void RequestController::dispatchHttp(const RequestConfig &config, const RequestLogEntry &entry)
{
    QNetworkRequest request(entry.url);
    request.setTransferTimeout(config.httpTimeoutMs);
    QHttpHeaders headers;
    QVector<HttpHeader> combinedHeaders;
    QHash<QByteArray, qsizetype> headerIndexes;
    for (const HttpHeader &header : config.headers) {
        if (!header.enabled || header.name.isEmpty())
            continue;
        const QByteArray key = header.name.toLower();
        const auto found = headerIndexes.constFind(key);
        if (found == headerIndexes.cend()) {
            headerIndexes.insert(key, combinedHeaders.size());
            combinedHeaders.append(header);
        } else {
            // QNetworkAccessManager can discard repeated QHttpHeaders fields.
            // Combine them explicitly so every enabled value reaches the server.
            combinedHeaders[*found].value += key == "cookie" ? "; " : ", ";
            combinedHeaders[*found].value += header.value;
        }
    }
    for (const HttpHeader &header : combinedHeaders)
        headers.append(QLatin1StringView(header.name), QLatin1StringView(header.value));
    if (!headers.contains(QHttpHeaders::WellKnownHeader::UserAgent))
        headers.append(QHttpHeaders::WellKnownHeader::UserAgent,
                       QStringLiteral("MultiSender/%1").arg(QCoreApplication::applicationVersion()));
    if (httpMethodHasBody(config.method) && !headers.contains(QHttpHeaders::WellKnownHeader::ContentType))
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, QLatin1StringView("application/json"));
    request.setHeaders(std::move(headers));
    auto *active = new ActiveRequest;
    active->entry = entry;
    active->entry.requestHeaders = requestHeaders(request);
    active->elapsed.start();
    QNetworkReply *reply = nullptr;
    if (config.method == HttpMethod::Get)
        reply = m_networkManager.get(request);
    else if (config.method == HttpMethod::Head)
        reply = m_networkManager.head(request);
    else
        reply = m_networkManager.sendCustomRequest(request, httpMethodName(config.method).toLatin1(),
                                                   entry.requestBytes);
    reply->setReadBufferSize(64 * 1024);
    active->timeout = new QTimer(reply);
    active->timeout->setSingleShot(true);
    active->timeout->setTimerType(Qt::PreciseTimer);
    m_activeRequests.insert(reply, active);
    connect(active->timeout, &QTimer::timeout, this, [this, reply, timeout = config.httpTimeoutMs] {
        if (ActiveRequest *current = m_activeRequests.value(reply)) {
            readHttpResponse(reply);
            current = m_activeRequests.value(reply);
            if (!current)
                return;
            current->forcedError = UiLanguage::text("Истекло время HTTP-запроса (%1 мс)").arg(timeout);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { readHttpResponse(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishReply(reply); });
    active->timeout->start(config.httpTimeoutMs);
    ++m_counters.sent;
    m_counters.active = activeCount();
    const RequestLogEntry startedEntry = active->entry;
    emit requestStarted(startedEntry);
    emitCounters();
}

void RequestController::dispatchSocket(const RequestConfig &config, const RequestLogEntry &entry)
{
    QAbstractSocket *socket = config.protocol == RequestProtocol::Tcp
        ? static_cast<QAbstractSocket *>(new QTcpSocket(this))
        : static_cast<QAbstractSocket *>(new QUdpSocket(this));
    if (config.protocol == RequestProtocol::Tcp)
        socket->setReadBufferSize(64 * 1024);
    auto *active = new ActiveRequest;
    active->entry = entry;
    active->payload = entry.requestBytes;
    active->timeoutMs = qMax(1, config.timeoutMs);
    active->waitForResponse = config.waitForResponse;
    active->timeout = new QTimer(socket);
    active->timeout->setSingleShot(true);
    active->timeout->setTimerType(Qt::PreciseTimer);
    active->elapsed.start();
    m_activeSockets.insert(socket, active);

    connect(active->timeout, &QTimer::timeout, this, [this, socket] {
        readSocketResponse(socket);
        const ActiveRequest *current = m_activeSockets.value(socket);
        if (!current) {
            return;
        }
        if (current->phase == ActiveRequest::SocketPhase::Reading && !current->response.isEmpty()) {
            finishSocket(socket, RequestResult::Success);
        } else {
            const QString operation = current->phase == ActiveRequest::SocketPhase::Reading
                ? UiLanguage::text("ожидания ответа") : UiLanguage::text("подключения или отправки");
            finishSocket(socket, RequestResult::NetworkError,
                         UiLanguage::text("Истекло время %1 (%2 мс)")
                             .arg(operation).arg(current->timeoutMs));
        }
    });
    connect(socket, &QAbstractSocket::connected, this, [this, socket] {
        if (ActiveRequest *current = m_activeSockets.value(socket)) {
            current->phase = ActiveRequest::SocketPhase::Writing;
            sendSocketPayload(socket);
        }
    });
    connect(socket, &QAbstractSocket::bytesWritten, this, [this, socket](qint64 bytes) {
        if (ActiveRequest *current = m_activeSockets.value(socket)) {
            current->bytesWritten += bytes;
            if (current->entry.protocol == RequestProtocol::Tcp) {
                sendSocketPayload(socket);
            }
        }
    });
    connect(socket, &QAbstractSocket::readyRead, this, [this, socket] {
        readSocketResponse(socket);
    });
    connect(socket, &QAbstractSocket::disconnected, this, [this, socket] {
        readSocketResponse(socket);
        const ActiveRequest *current = m_activeSockets.value(socket);
        if (!current) {
            return;
        }
        if (current->phase == ActiveRequest::SocketPhase::Reading && !current->response.isEmpty()) {
            finishSocket(socket, RequestResult::Success);
        } else {
            finishSocket(socket, RequestResult::NetworkError,
                         current->phase == ActiveRequest::SocketPhase::Reading
                             ? UiLanguage::text("Сервер закрыл соединение без ответа")
                             : UiLanguage::text("Соединение закрыто до завершения отправки"));
        }
    });
    connect(socket, &QAbstractSocket::errorOccurred, this,
            [this, socket](QAbstractSocket::SocketError error) {
        // Qt reports a normal TCP peer close before disconnected. Read the
        // final bytes there, after all readyRead notifications are delivered.
        if (error != QAbstractSocket::RemoteHostClosedError) {
            finishSocket(socket, RequestResult::NetworkError, socket->errorString());
        }
    });

    active->timeout->start(active->timeoutMs);
    ++m_counters.sent;
    m_counters.active = activeCount();
    emit requestStarted(entry);
    emitCounters();

    // Cancellation from requestStarted must precede synchronous local I/O.
    QTimer::singleShot(0, socket, [this, socket] {
        ActiveRequest *current = m_activeSockets.value(socket);
        if (!current) {
            return;
        }
        const QUrl url = current->entry.url;
        if (current->entry.protocol == RequestProtocol::Tcp) {
            socket->connectToHost(url.host(), static_cast<quint16>(url.port()));
            return;
        }
        QHostAddress address;
        if (address.setAddress(url.host())) {
            current->peerAddress = address;
            current->phase = ActiveRequest::SocketPhase::Writing;
            sendSocketPayload(socket);
            return;
        }
        current->lookupId = QHostInfo::lookupHost(url.host(), socket,
                                                [this, socket](const QHostInfo &info) {
            ActiveRequest *resolved = m_activeSockets.value(socket);
            if (!resolved) {
                return;
            }
            resolved->lookupId = -1;
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                finishSocket(socket, RequestResult::NetworkError,
                             info.error() == QHostInfo::NoError
                                 ? UiLanguage::text("Не найден адрес сервера") : info.errorString());
                return;
            }
            resolved->peerAddress = info.addresses().first();
            resolved->phase = ActiveRequest::SocketPhase::Writing;
            sendSocketPayload(socket);
        });
    });
}

void RequestController::dispatchSerial(const RequestConfig &config, const RequestLogEntry &entry)
{
    auto *port = new QSerialPort(this);
    auto *active = new ActiveRequest;
    active->entry = entry;
    active->serialKey = serialPortKey(config.serial.portName);
    active->payload = entry.requestBytes;
    port->setReadBufferSize(64 * 1024);
    active->timeoutMs = qMax(1, config.timeoutMs);
    active->waitForResponse = config.waitForResponse;
    active->timeout = new QTimer(port);
    active->timeout->setSingleShot(true);
    active->timeout->setTimerType(Qt::PreciseTimer);
    active->elapsed.start();
    m_activeSerialPorts.insert(port, active);

    connect(active->timeout, &QTimer::timeout, this, [this, port] {
        readSerialResponse(port);
        const ActiveRequest *current = m_activeSerialPorts.value(port);
        if (!current) {
            return;
        }
        if (current->phase == ActiveRequest::SocketPhase::Reading && !current->response.isEmpty()) {
            finishSerial(port, RequestResult::Success);
        } else {
            const QString operation = current->phase == ActiveRequest::SocketPhase::Reading
                ? UiLanguage::text("ожидания ответа") : UiLanguage::text("открытия или отправки");
            finishSerial(port, RequestResult::SerialError,
                         UiLanguage::text("Истекло время %1 через порт %2 (%3 мс)")
                             .arg(operation, current->serialKey).arg(current->timeoutMs));
        }
    });
    connect(port, &QSerialPort::bytesWritten, this, [this, port](qint64 bytes) {
        if (ActiveRequest *current = m_activeSerialPorts.value(port)) {
            current->bytesWritten += bytes;
            sendSerialPayload(port);
        }
    });
    connect(port, &QSerialPort::readyRead, this, [this, port] {
        readSerialResponse(port);
    });
    connect(port, &QSerialPort::errorOccurred, this, [this, port](QSerialPort::SerialPortError error) {
        // QSerialPort also emits NoError on a successful open.
        if (error != QSerialPort::NoError) {
            finishSerial(port, RequestResult::SerialError, serialErrorText(*port));
        }
    });

    active->timeout->start(active->timeoutMs);
    ++m_counters.sent;
    m_counters.active = activeCount();
    emit requestStarted(entry);
    emitCounters();

    QTimer::singleShot(0, port, [this, port, settings = config.serial] {
        if (!m_activeSerialPorts.contains(port)) {
            return;
        }
        port->setPortName(serialPortKey(settings.portName));
        if (!port->setBaudRate(settings.baudRate)
            || !port->setDataBits(settings.dataBits)
            || !port->setParity(settings.parity)
            || !port->setStopBits(settings.stopBits)
            || !port->setFlowControl(settings.flowControl)) {
            finishSerial(port, RequestResult::SerialError, serialErrorText(*port));
            return;
        }
        if (!m_activeSerialPorts.contains(port)) {
            return;
        }
        if (!port->open(QIODevice::ReadWrite)) {
            finishSerial(port, RequestResult::SerialError, serialErrorText(*port));
            return;
        }
        ActiveRequest *current = m_activeSerialPorts.value(port);
        if (!current) {
            return;
        }
        if (current->elapsed.elapsed() >= current->timeoutMs) {
            finishSerial(port, RequestResult::SerialError,
                         UiLanguage::text("Истекло время открытия порта %1 (%2 мс)")
                             .arg(current->serialKey).arg(current->timeoutMs));
            return;
        }
        current->phase = ActiveRequest::SocketPhase::Writing;
        sendSerialPayload(port);
    });
}

void RequestController::sendSerialPayload(QSerialPort *port)
{
    ActiveRequest *active = m_activeSerialPorts.value(port);
    if (!active || active->phase != ActiveRequest::SocketPhase::Writing || active->writing) {
        return;
    }
    active->writing = true;
    const qint64 remaining = active->payload.size() - active->bytesAccepted;
    const qint64 accepted = remaining > 0
        ? port->write(active->payload.constData() + active->bytesAccepted, remaining) : 0;
    // A synchronous error signal can finish this request inside write().
    active = m_activeSerialPorts.value(port);
    if (!active) {
        return;
    }
    active->writing = false;
    if (accepted < 0) {
        finishSerial(port, RequestResult::SerialError, serialErrorText(*port));
        return;
    }
    active->bytesAccepted += accepted;
    if (active->bytesAccepted != active->payload.size()
        || active->bytesWritten < active->payload.size()) {
        return;
    }
    if (!active->waitForResponse) {
        finishSerial(port, RequestResult::Success);
        return;
    }
    active->phase = ActiveRequest::SocketPhase::Reading;
    active->timeout->start(active->timeoutMs);
    readSerialResponse(port);
}

void RequestController::readSerialResponse(QSerialPort *port)
{
    if (!m_activeSerialPorts.contains(port) || !port->isReadable()) {
        return;
    }
    if (ActiveRequest *active = m_activeSerialPorts.value(port)) {
        // Keep bytes received during writing too; decoding happens once at completion.
        if (!readBounded(port, active->response)) {
            active->responseTruncated = true;
            finishSerial(port, RequestResult::SerialError, responseLimitError());
        }
    }
}

void RequestController::finishSerial(QSerialPort *port, RequestResult result,
                                     const QString &errorText)
{
    ActiveRequest *active = m_activeSerialPorts.take(port);
    if (!active) {
        return;
    }
    active->timeout->stop();
    disconnect(port, nullptr, this, nullptr);
    if (port->isReadable() && !active->responseTruncated) {
        active->responseTruncated = !readBounded(port, active->response);
    }
    RequestLogEntry entry = active->entry;
    entry.completedAt = QDateTime::currentDateTime();
    entry.elapsedMs = active->elapsed.elapsed();
    entry.responseBody = QString::fromUtf8(active->response);
    entry.responseBytes = active->response;
    entry.responseBytesAvailable = true;
    entry.responseTruncated = active->responseTruncated;
    entry.result = result;
    entry.errorText = errorText;
    if (entry.responseTruncated && entry.result != RequestResult::Cancelled) {
        entry.result = RequestResult::SerialError;
        entry.errorText = responseLimitError();
    }
    if (port->isOpen()) {
        port->close();
    }
    port->deleteLater();
    delete active;
    recordCompletion(entry);
}

void RequestController::sendSocketPayload(QAbstractSocket *socket)
{
    ActiveRequest *active = m_activeSockets.value(socket);
    if (!active || active->phase != ActiveRequest::SocketPhase::Writing || active->writing) {
        return;
    }
    active->writing = true;
    const bool isUdp = active->entry.protocol == RequestProtocol::Udp;
    const qint64 remaining = active->payload.size() - active->bytesAccepted;
    qint64 accepted = 0;
    if (isUdp) {
        auto *udp = static_cast<QUdpSocket *>(socket);
        accepted = udp->writeDatagram(active->payload, active->peerAddress,
                                     static_cast<quint16>(active->entry.url.port()));
    } else if (remaining > 0) {
        accepted = socket->write(active->payload.constData() + active->bytesAccepted, remaining);
    }
    // Errors can synchronously finish and delete the active request.
    active = m_activeSockets.value(socket);
    if (!active) {
        return;
    }
    active->writing = false;
    if (accepted < 0 || (isUdp && accepted != active->payload.size())) {
        finishSocket(socket, RequestResult::NetworkError, socket->errorString());
        return;
    }
    active->bytesAccepted += accepted;
    // UDP writes are atomic, including empty datagrams. TCP's accepted bytes
    // are only queued; wait until bytesWritten covers the whole payload.
    if (!isUdp && (active->bytesAccepted != active->payload.size()
                   || active->bytesWritten < active->payload.size())) {
        return;
    }
    if (!active->waitForResponse) {
        finishSocket(socket, RequestResult::Success);
        return;
    }
    active->phase = ActiveRequest::SocketPhase::Reading;
    active->timeout->start(active->timeoutMs);
    readSocketResponse(socket);
}

void RequestController::readSocketResponse(QAbstractSocket *socket)
{
    ActiveRequest *active = m_activeSockets.value(socket);
    if (!active) {
        return;
    }
    if (active->entry.protocol == RequestProtocol::Tcp) {
        if (socket->isReadable() && !readBounded(socket, active->response)) {
            active->responseTruncated = true;
            finishSocket(socket, RequestResult::NetworkError, responseLimitError());
        }
        return;
    }
    auto *udp = static_cast<QUdpSocket *>(socket);
    if (active->phase != ActiveRequest::SocketPhase::Reading) {
        return;
    }
    while (udp->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = udp->receiveDatagram();
        active = m_activeSockets.value(socket);
        if (!active) {
            return;
        }
        if (!datagram.isValid()) {
            finishSocket(socket, RequestResult::NetworkError, socket->errorString());
            return;
        }
        if (!datagram.senderAddress().isEqual(active->peerAddress,
                                             QHostAddress::ConvertV4MappedToIPv4)
            || datagram.senderPort() != active->entry.url.port()) {
            continue;
        }
        active->response = datagram.data();
        finishSocket(socket, RequestResult::Success);
        return;
    }
}

void RequestController::readHttpResponse(QNetworkReply *reply)
{
    ActiveRequest *active = m_activeRequests.value(reply);
    if (!active || !reply->isReadable() || active->responseTruncated)
        return;
    if (!readBounded(reply, active->response)) {
        active->responseTruncated = true;
        active->forcedError = responseLimitError();
        reply->abort();
    }
}

void RequestController::finishReply(QNetworkReply *reply)
{
    ActiveRequest *active = m_activeRequests.take(reply);
    if (!active) {
        reply->deleteLater();
        return;
    }
    active->timeout->stop();
    RequestLogEntry entry = active->entry;
    entry.completedAt = QDateTime::currentDateTime();
    entry.elapsedMs = active->elapsed.elapsed();
    // abort() closes the reply before finished(); do not read a closed device.
    if (reply->isReadable() && !active->responseTruncated
        && !readBounded(reply, active->response)) {
        active->responseTruncated = true;
        active->forcedError = responseLimitError();
    }
    entry.responseBytes = active->response;
    entry.responseBytesAvailable = true;
    entry.responseTruncated = active->responseTruncated;
    entry.responseBody = QString::fromUtf8(active->response);
    entry.responseHeaders = responseHeaders(*reply);
    entry.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (active->cancelled) {
        entry.result = RequestResult::Cancelled;
        entry.errorText = UiLanguage::text("Запрос отменён пользователем");
    } else if (!active->forcedError.isEmpty()) {
        entry.result = RequestResult::NetworkError;
        entry.errorText = active->forcedError;
    } else if (entry.httpStatus >= 400) {
        entry.result = RequestResult::HttpError;
        entry.errorText = reply->errorString();
    } else if (reply->error() != QNetworkReply::NoError) {
        entry.result = RequestResult::NetworkError;
        entry.errorText = reply->errorString();
    } else {
        entry.result = RequestResult::Success;
    }
    delete active;
    reply->deleteLater();
    recordCompletion(entry);
}

void RequestController::finishSocket(QAbstractSocket *socket, RequestResult result,
                                     const QString &errorText)
{
    ActiveRequest *active = m_activeSockets.take(socket);
    if (!active) {
        return;
    }
    active->timeout->stop();
    if (active->lookupId != -1) {
        QHostInfo::abortHostLookup(active->lookupId);
    }
    disconnect(socket, nullptr, this, nullptr);
    if (active->entry.protocol == RequestProtocol::Tcp && socket->isReadable()
        && !active->responseTruncated) {
        active->responseTruncated = !readBounded(socket, active->response);
    }
    RequestLogEntry entry = active->entry;
    entry.completedAt = QDateTime::currentDateTime();
    entry.elapsedMs = active->elapsed.elapsed();
    entry.responseBody = QString::fromUtf8(active->response);
    entry.responseBytes = active->response;
    entry.responseBytesAvailable = true;
    entry.responseTruncated = active->responseTruncated;
    entry.result = result;
    entry.errorText = errorText;
    if (entry.responseTruncated && entry.result != RequestResult::Cancelled) {
        entry.result = RequestResult::NetworkError;
        entry.errorText = responseLimitError();
    }
    socket->abort();
    socket->deleteLater();
    delete active;
    recordCompletion(entry);
}

void RequestController::recordCompletion(const RequestLogEntry &entry)
{
    ++m_counters.completed;
    if (entry.result != RequestResult::Success) {
        ++m_counters.errors;
    }
    m_counters.active = activeCount();
    emit requestCompleted(entry);
    emitCounters();
}

int RequestController::activeCount() const
{
    return static_cast<int>(m_activeRequests.size() + m_activeSockets.size()
                            + m_activeSerialPorts.size());
}

void RequestController::emitCounters()
{
    m_counters.active = activeCount();
    emit countersChanged(m_counters);
}
