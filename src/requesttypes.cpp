#include "uilanguage.h"
#include "requesttypes.h"
#include "payloadencoding.h"

#include <QRegularExpression>
#include <QUrlQuery>

QString requestProtocolName(RequestProtocol protocol)
{
    switch (protocol) {
    case RequestProtocol::Http: return QStringLiteral("HTTP");
    case RequestProtocol::Tcp: return QStringLiteral("TCP");
    case RequestProtocol::Udp: return QStringLiteral("UDP");
    case RequestProtocol::Serial: return QStringLiteral("COM");
    }
    return QStringLiteral("HTTP");
}

RequestProtocol requestProtocolFromString(const QString &value)
{
    if (value.compare(QStringLiteral("TCP"), Qt::CaseInsensitive) == 0)
        return RequestProtocol::Tcp;
    if (value.compare(QStringLiteral("UDP"), Qt::CaseInsensitive) == 0)
        return RequestProtocol::Udp;
    if (value.compare(QStringLiteral("COM"), Qt::CaseInsensitive) == 0)
        return RequestProtocol::Serial;
    return RequestProtocol::Http;
}

QString requestOperationName(RequestProtocol protocol, HttpMethod method)
{
    return protocol == RequestProtocol::Http ? httpMethodName(method)
                                             : requestProtocolName(protocol);
}

QString httpMethodName(HttpMethod method)
{
    switch (method) {
    case HttpMethod::Get:
        return QStringLiteral("GET");
    case HttpMethod::Post:
        return QStringLiteral("POST");
    case HttpMethod::Put: return QStringLiteral("PUT");
    case HttpMethod::Patch: return QStringLiteral("PATCH");
    case HttpMethod::Delete: return QStringLiteral("DELETE");
    case HttpMethod::Head: return QStringLiteral("HEAD");
    case HttpMethod::Options: return QStringLiteral("OPTIONS");
    }

    return QStringLiteral("GET");
}

HttpMethod httpMethodFromString(const QString &value)
{
    for (HttpMethod method : {HttpMethod::Post, HttpMethod::Put, HttpMethod::Patch,
                              HttpMethod::Delete, HttpMethod::Head, HttpMethod::Options}) {
        if (value.compare(httpMethodName(method), Qt::CaseInsensitive) == 0)
            return method;
    }
    return HttpMethod::Get;
}

bool httpMethodHasBody(HttpMethod method)
{
    return method == HttpMethod::Post || method == HttpMethod::Put
        || method == HttpMethod::Patch || method == HttpMethod::Delete;
}

QString requestResultName(RequestResult result, int httpStatus)
{
    switch (result) {
    case RequestResult::Pending:
        return UiLanguage::text("Выполняется");
    case RequestResult::Success:
        return httpStatus > 0 ? UiLanguage::text("Успешно · %1").arg(httpStatus)
                              : UiLanguage::text("Успешно");
    case RequestResult::HttpError:
        return httpStatus > 0 ? QStringLiteral("HTTP · %1").arg(httpStatus)
                              : UiLanguage::text("HTTP-ошибка");
    case RequestResult::NetworkError:
        return UiLanguage::text("Сетевая ошибка");
    case RequestResult::SerialError:
        return UiLanguage::text("Ошибка COM-порта");
    case RequestResult::Cancelled:
        return UiLanguage::text("Отменено");
    case RequestResult::Skipped:
        return UiLanguage::text("Пропущено");
    }

    return {};
}

QUrl buildRequestUrl(const RequestConfig &config)
{
    QUrl url;
    url.setScheme(config.protocol == RequestProtocol::Http ? config.httpScheme
                                                          : requestProtocolName(config.protocol).toLower());
    if (config.protocol == RequestProtocol::Serial) {
        url.setPath(u'/' + config.serial.portName.trimmed());
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("baud"), QString::number(config.serial.baudRate));
        query.addQueryItem(QStringLiteral("data"), QString::number(config.serial.dataBits));
        QString parity;
        switch (config.serial.parity) {
        case QSerialPort::NoParity: parity = QStringLiteral("none"); break;
        case QSerialPort::EvenParity: parity = QStringLiteral("even"); break;
        case QSerialPort::OddParity: parity = QStringLiteral("odd"); break;
        case QSerialPort::SpaceParity: parity = QStringLiteral("space"); break;
        case QSerialPort::MarkParity: parity = QStringLiteral("mark"); break;
        }
        query.addQueryItem(QStringLiteral("parity"), parity);
        query.addQueryItem(QStringLiteral("stop"), config.serial.stopBits == QSerialPort::OneAndHalfStop
                              ? QStringLiteral("1.5") : QString::number(config.serial.stopBits));
        query.addQueryItem(QStringLiteral("flow"), config.serial.flowControl == QSerialPort::HardwareControl
                              ? QStringLiteral("hardware") : config.serial.flowControl == QSerialPort::SoftwareControl
                              ? QStringLiteral("software") : QStringLiteral("none"));
        url.setQuery(query);
        return url;
    }
    url.setHost(config.host.trimmed());
    url.setPort(config.port);
    if (config.protocol == RequestProtocol::Http) {
        const QUrl targetUrl(config.target, QUrl::StrictMode);
        url.setPath(targetUrl.path());
        url.setQuery(targetUrl.query());
    }
    return url;
}

QString validateRequestConfig(const RequestConfig &config)
{
    if (config.protocol == RequestProtocol::Serial) {
        const auto &serial = config.serial;
        static const QRegularExpression portPattern(QStringLiteral("^COM[1-9][0-9]*$"),
                                                     QRegularExpression::CaseInsensitiveOption);
        QString portName = serial.portName.trimmed();
        if (portName.startsWith(QStringLiteral("\\\\.\\"))) {
            portName.remove(0, 4);
        }
        if (!portPattern.match(portName).hasMatch()) {
            return UiLanguage::text("Укажите имя COM-порта, например COM3");
        }
        if (serial.baudRate < 1) {
            return UiLanguage::text("Скорость COM-порта должна быть больше нуля");
        }
        if (serial.dataBits < QSerialPort::Data5 || serial.dataBits > QSerialPort::Data8) {
            return UiLanguage::text("Укажите от 5 до 8 бит данных");
        }
        if (serial.parity != QSerialPort::NoParity && serial.parity != QSerialPort::EvenParity
            && serial.parity != QSerialPort::OddParity && serial.parity != QSerialPort::SpaceParity
            && serial.parity != QSerialPort::MarkParity) {
            return UiLanguage::text("Неверная настройка чётности COM-порта");
        }
        if (serial.stopBits != QSerialPort::OneStop && serial.stopBits != QSerialPort::OneAndHalfStop
            && serial.stopBits != QSerialPort::TwoStop) {
            return UiLanguage::text("Укажите 1, 1.5 или 2 стоп-бита");
        }
        if ((serial.stopBits == QSerialPort::OneAndHalfStop && serial.dataBits != QSerialPort::Data5)
            || (serial.stopBits == QSerialPort::TwoStop && serial.dataBits == QSerialPort::Data5)) {
            return UiLanguage::text("При 5 битах данных допустимы 1 или 1.5 стоп-бита; при 6–8 — 1 или 2");
        }
        if (serial.flowControl != QSerialPort::NoFlowControl
            && serial.flowControl != QSerialPort::HardwareControl
            && serial.flowControl != QSerialPort::SoftwareControl) {
            return UiLanguage::text("Неверная настройка управления потоком COM-порта");
        }
    } else {
        const QString host = config.host.trimmed();
        if (host.isEmpty()) {
            return UiLanguage::text("Укажите адрес сервера");
        }
        if (host.contains(u'/') || host.contains(QStringLiteral("://")) || host.contains(u' ')) {
            return UiLanguage::text("В адресе укажите только имя хоста или IP без схемы и пути");
        }
        if (config.port == 0) {
            return UiLanguage::text("Порт должен быть от 1 до 65535");
        }
    }
    if (config.protocol == RequestProtocol::Http) {
        if (config.httpScheme != QStringLiteral("http") && config.httpScheme != QStringLiteral("https")) {
            return UiLanguage::text("Выберите схему HTTP или HTTPS");
        }
        if (config.httpTimeoutMs < 1 || config.httpTimeoutMs > 86'400'000) {
            return UiLanguage::text("Тайм-аут HTTP должен быть от 1 до 86 400 000 мс");
        }
        if (httpMethodHasBody(config.method) && config.body.toUtf8().size() > MaxPayloadBytes) {
            return UiLanguage::text("Тело HTTP-запроса не должно превышать 16 МиБ");
        }
        for (const HttpHeader &header : config.headers) {
            if (!header.enabled || (header.name.isEmpty() && header.value.isEmpty()))
                continue;
            if (header.name.isEmpty())
                return UiLanguage::text("Укажите имя включённого HTTP-заголовка");
            for (const char ch : header.name) {
                const auto byte = static_cast<unsigned char>(ch);
                const bool token = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
                    || (byte >= '0' && byte <= '9') || QByteArrayView("!#$%&'*+-.^_`|~").contains(ch);
                if (!token)
                    return UiLanguage::text("Имя HTTP-заголовка содержит недопустимые символы");
            }
            for (const char ch : header.value) {
                const auto byte = static_cast<unsigned char>(ch);
                if ((byte < 32 && byte != '\t') || byte == 127)
                    return UiLanguage::text("Значение HTTP-заголовка %1 содержит управляющие символы")
                        .arg(QString::fromLatin1(header.name));
            }
            const QByteArray lower = header.name.toLower();
            if (lower == "content-length" || lower == "host" || lower == "transfer-encoding")
                return UiLanguage::text("Заголовок %1 рассчитывается транспортом автоматически; отключите эту строку")
                    .arg(QString::fromLatin1(header.name));
        }
        if (config.target.isEmpty() || !config.target.startsWith(u'/')) {
            return UiLanguage::text("Путь должен начинаться с символа /");
        }
        const QUrl targetUrl(config.target, QUrl::StrictMode);
        if (!targetUrl.isValid() || !targetUrl.isRelative() || !targetUrl.host().isEmpty()
            || config.target.startsWith(QStringLiteral("//")) || targetUrl.hasFragment()) {
            return UiLanguage::text("Путь или query-параметры имеют неверный формат");
        }
    } else {
        if (config.timeoutMs < 1 || config.timeoutMs > 86'400'000) {
            return UiLanguage::text("Тайм-аут должен быть от 1 до 86 400 000 мс");
        }
        const SocketPayload payload = parseSocketPayload(config.body);
        if (!payload.error.isEmpty()) {
            return payload.error;
        }
        if (payload.bytes.size() > MaxPayloadBytes) {
            return UiLanguage::text("Данные не должны превышать 16 МиБ после преобразования");
        }
        if (config.protocol == RequestProtocol::Udp && payload.bytes.size() > 65'507) {
            return UiLanguage::text("Данные UDP не должны превышать 65 507 байт после преобразования");
        }
    }
    if (config.intervalMs < 1) {
        return UiLanguage::text("Интервал должен быть не меньше 1 мс");
    }
    if (config.protocol != RequestProtocol::Serial && config.maxInFlight < 1) {
        return UiLanguage::text("Лимит запросов должен быть не меньше 1");
    }

    const QUrl url = buildRequestUrl(config);
    if (!url.isValid() || (config.protocol != RequestProtocol::Serial && url.host().isEmpty())) {
        return UiLanguage::text("Не удалось сформировать корректный URL");
    }

    return {};
}

RequestConfig resolveRequestConfig(const CollectionDefinition &collection,
                                   const RequestDefinition &request)
{
    RequestConfig result;
    result.protocol = request.protocol;
    result.host = request.host.overridden ? request.host.value : collection.host;
    result.port = request.port.overridden ? request.port.value : collection.port;
    result.intervalMs = request.intervalMs.overridden
        ? request.intervalMs.value : collection.intervalMs;
    result.maxInFlight = request.maxInFlight.overridden
        ? request.maxInFlight.value : collection.maxInFlight;
    result.target = request.target;
    result.method = request.method;
    result.body = request.body;
    result.headers = request.headers;
    result.httpScheme = request.httpScheme;
    result.httpTimeoutMs = request.httpTimeoutMs;
    result.waitForResponse = request.waitForResponse;
    result.timeoutMs = request.timeoutMs;
    result.serial = request.serial;
    result.collectionId = collection.id;
    result.requestId = request.id;
    result.collectionName = collection.name;
    result.requestName = request.name;
    return result;
}
