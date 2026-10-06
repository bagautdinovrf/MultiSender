#include "uilanguage.h"
#include "workspaceexchange.h"

#include "workspacestore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStringView>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace {

constexpr qint64 MaximumFileBytes = 32LL * 1024 * 1024;
constexpr qsizetype MaximumCollections = 10'000;
constexpr qsizetype MaximumRequests = 100'000;
constexpr qsizetype MaximumRequestsPerCollection = 10'000;
constexpr qsizetype MaximumHeaders = 1'000;

class Validator final
{
public:
    bool validate(const QJsonObject &root)
    {
        if (!integer(root, u"schemaVersion", UiLanguage::text("файл"), 1, 1)) {
            return false;
        }
        const QJsonValue collectionsValue = root.value(QStringLiteral("collections"));
        if (!collectionsValue.isArray()) {
            return fail(UiLanguage::text("Файл не содержит список коллекций MultiSender."));
        }
        const QJsonArray collections = collectionsValue.toArray();
        if (collections.size() > MaximumCollections) {
            return fail(UiLanguage::text("В файле больше %1 коллекций.").arg(MaximumCollections));
        }
        qsizetype requestCount = 0;
        for (qsizetype index = 0; index < collections.size(); ++index) {
            const QString path = UiLanguage::text("Коллекция %1").arg(index + 1);
            if (!collections.at(index).isObject()) {
                return wrongType(path, u"", UiLanguage::text("объект"));
            }
            const QJsonObject collection = collections.at(index).toObject();
            if (!identifier(collection, path) || !string(collection, u"name", path)
                || !connection(collection, path)) {
                return false;
            }
            const QJsonValue requestsValue = collection.value(QStringLiteral("requests"));
            if (!requestsValue.isArray()) {
                return wrongType(path, u"requests", UiLanguage::text("список запросов"));
            }
            const QJsonArray requests = requestsValue.toArray();
            requestCount += requests.size();
            if (requests.size() > MaximumRequestsPerCollection || requestCount > MaximumRequests) {
                return fail(UiLanguage::text("Слишком много запросов: не более %1 в коллекции "
                                           "и %2 во всём файле.")
                                .arg(MaximumRequestsPerCollection).arg(MaximumRequests));
            }
            for (qsizetype requestIndex = 0; requestIndex < requests.size(); ++requestIndex) {
                const QString requestPath = path + UiLanguage::text(", запрос %1").arg(requestIndex + 1);
                if (!requests.at(requestIndex).isObject()) {
                    return wrongType(requestPath, u"", UiLanguage::text("объект"));
                }
                if (!request(requests.at(requestIndex).toObject(), requestPath)) {
                    return false;
                }
            }
        }
        return true;
    }

    QString error() const { return m_error; }

private:
    bool fail(const QString &message)
    {
        m_error = message;
        return false;
    }

    bool wrongType(QStringView path, QStringView field, QStringView expected)
    {
        const QString location = field.isEmpty() ? path.toString()
            : path.toString() + QStringLiteral(" → ") + field.toString();
        return fail(UiLanguage::text("%1: ожидается %2.").arg(location, expected));
    }

    bool string(const QJsonObject &object, QStringView field, QStringView path)
    {
        const QJsonValue value = object.value(field);
        return value.isUndefined() || value.isString() || wrongType(path, field, UiLanguage::text("строка"));
    }

    bool boolean(const QJsonObject &object, QStringView field, QStringView path)
    {
        const QJsonValue value = object.value(field);
        return value.isUndefined() || value.isBool() || wrongType(path, field, UiLanguage::text("логическое значение"));
    }

    bool integer(const QJsonObject &object, QStringView field, QStringView path,
                 int minimum, int maximum)
    {
        const QJsonValue value = object.value(field);
        if (value.isUndefined()) {
            return true;
        }
        if (!value.isDouble()) {
            return wrongType(path, field, UiLanguage::text("целое число"));
        }
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number
            || number < minimum || number > maximum) {
            return fail(UiLanguage::text("%1 → %2: нужно целое число от %3 до %4.")
                            .arg(path, field).arg(minimum).arg(maximum));
        }
        return true;
    }

    bool enumeration(const QJsonObject &object, QStringView field, QStringView path,
                     std::initializer_list<int> values)
    {
        const QJsonValue value = object.value(field);
        if (value.isUndefined()) {
            return true;
        }
        if (!integer(object, field, path, 0, std::numeric_limits<int>::max())) {
            return false;
        }
        for (const int allowed : values) {
            if (value.toInt() == allowed) {
                return true;
            }
        }
        return fail(UiLanguage::text("%1 → %2: неизвестное значение %3.")
                        .arg(path, field).arg(value.toInt()));
    }

    bool namedValue(const QJsonObject &object, QStringView field, QStringView path,
                    std::initializer_list<QStringView> values)
    {
        if (!string(object, field, path)) {
            return false;
        }
        const QJsonValue value = object.value(field);
        if (value.isUndefined()) {
            return true;
        }
        const QString name = value.toString();
        for (const QStringView allowed : values) {
            if (name.compare(allowed, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return fail(UiLanguage::text("%1 → %2: неизвестное значение «%3».")
                        .arg(path, field, name));
    }

    bool identifier(const QJsonObject &object, QStringView path)
    {
        const QJsonValue value = object.value(QStringLiteral("id"));
        if (value.isUndefined()) {
            return true;
        }
        if (!value.isString()) {
            return wrongType(path, u"id", UiLanguage::text("UUID в виде строки"));
        }
        const QString text = value.toString();
        const QUuid id(text);
        const bool canonical = text.compare(id.toString(QUuid::WithoutBraces), Qt::CaseInsensitive) == 0
            || text.compare(id.toString(QUuid::WithBraces), Qt::CaseInsensitive) == 0;
        if (id.isNull() || !canonical) {
            return fail(UiLanguage::text("%1 → id: некорректный или пустой UUID.").arg(path));
        }
        if (m_ids.contains(id)) {
            return fail(UiLanguage::text("%1 → id: один UUID используется несколько раз.").arg(path));
        }
        m_ids.insert(id);
        return true;
    }

    bool connection(const QJsonObject &object, QStringView path)
    {
        return string(object, u"host", path)
            && integer(object, u"port", path, 0, 65'535)
            && integer(object, u"intervalMs", path, 1, 86'400'000)
            && integer(object, u"maxInFlight", path, 1, 100'000);
    }

    bool serial(const QJsonObject &object, QStringView path)
    {
        const QJsonValue value = object.value(QStringLiteral("serial"));
        if (value.isUndefined()) {
            return true;
        }
        if (!value.isObject()) {
            return wrongType(path, u"serial", UiLanguage::text("объект параметров COM-порта"));
        }
        const QJsonObject settings = value.toObject();
        const QString serialPath = path.toString() + QStringLiteral(" → serial");
        return string(settings, u"portName", serialPath)
            && integer(settings, u"baudRate", serialPath, 1, std::numeric_limits<qint32>::max())
            && enumeration(settings, u"dataBits", serialPath, {5, 6, 7, 8})
            && enumeration(settings, u"parity", serialPath, {0, 2, 3, 4, 5})
            && enumeration(settings, u"stopBits", serialPath, {1, 2, 3})
            && enumeration(settings, u"flowControl", serialPath, {0, 1, 2});
    }

    bool headers(const QJsonObject &object, QStringView path)
    {
        const QJsonValue value = object.value(QStringLiteral("headers"));
        if (value.isUndefined()) {
            return true;
        }
        if (!value.isArray()) {
            return wrongType(path, u"headers", UiLanguage::text("список HTTP-заголовков"));
        }
        const QJsonArray array = value.toArray();
        if (array.size() > MaximumHeaders) {
            return fail(UiLanguage::text("%1: больше %2 HTTP-заголовков.").arg(path).arg(MaximumHeaders));
        }
        for (qsizetype index = 0; index < array.size(); ++index) {
            const QString headerPath = path.toString() + QStringLiteral(" → headers[%1]").arg(index + 1);
            if (!array.at(index).isObject()) {
                return wrongType(headerPath, u"", UiLanguage::text("объект заголовка"));
            }
            const QJsonObject header = array.at(index).toObject();
            if (!string(header, u"name", headerPath) || !string(header, u"value", headerPath)
                || !boolean(header, u"enabled", headerPath)) {
                return false;
            }
        }
        return true;
    }

    bool request(const QJsonObject &object, QStringView path)
    {
        return identifier(object, path) && string(object, u"name", path)
            && namedValue(object, u"protocol", path, {u"HTTP", u"TCP", u"UDP", u"COM"})
            && namedValue(object, u"method", path, {u"GET", u"POST", u"PUT", u"PATCH",
                                                    u"DELETE", u"HEAD", u"OPTIONS"})
            && namedValue(object, u"httpScheme", path, {u"http", u"https"})
            && string(object, u"target", path) && string(object, u"body", path)
            && boolean(object, u"waitForResponse", path)
            && integer(object, u"timeoutMs", path, 1, 86'400'000)
            && integer(object, u"httpTimeoutMs", path, 1, 86'400'000)
            && connection(object, path)
            && boolean(object, u"overrideHost", path)
            && boolean(object, u"overridePort", path)
            && boolean(object, u"overrideIntervalMs", path)
            && boolean(object, u"overrideMaxInFlight", path)
            && serial(object, path) && headers(object, path);
    }

    QSet<QUuid> m_ids;
    QString m_error;
};

bool report(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

bool WorkspaceExchange::read(const QString &path, Workspace *workspace, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!workspace) {
        return report(error, UiLanguage::text("Не задано рабочее пространство для импорта."));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return report(error, UiLanguage::text("Не удалось открыть файл: %1").arg(file.errorString()));
    }
    if (file.size() > MaximumFileBytes) {
        return report(error, UiLanguage::text("Размер файла коллекций превышает 32 МБ."));
    }
    QByteArray data = file.read(MaximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        return report(error, UiLanguage::text("Не удалось прочитать файл: %1").arg(file.errorString()));
    }
    if (data.size() > MaximumFileBytes) {
        return report(error, UiLanguage::text("Размер файла коллекций превышает 32 МБ."));
    }
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return report(error, UiLanguage::text("Ошибка JSON в позиции %1: %2.")
                                 .arg(parseError.offset).arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return report(error, UiLanguage::text("Файл должен содержать объект коллекций MultiSender."));
    }
    const QJsonObject root = document.object();
    Validator validator;
    if (!validator.validate(root)) {
        return report(error, validator.error());
    }
    Workspace imported;
    if (!WorkspaceStore::workspaceFromJson(root, &imported, error)) {
        return false;
    }
    *workspace = std::move(imported);
    return true;
}

bool WorkspaceExchange::write(const QString &path, const Workspace &workspace, QString *error)
{
    if (error) {
        error->clear();
    }
    const QByteArray data = QJsonDocument(WorkspaceStore::workspaceToJson(workspace))
                                .toJson(QJsonDocument::Indented);
    if (data.size() > MaximumFileBytes) {
        return report(error, UiLanguage::text("Рабочее пространство превышает 32 МБ. "
                                            "Экспортируйте коллекции по отдельности."));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return report(error, UiLanguage::text("Не удалось открыть файл для экспорта: %1")
                                 .arg(file.errorString()));
    }
    if (file.write(data) != data.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return report(error, UiLanguage::text("Не удалось записать экспорт: %1").arg(reason));
    }
    if (!file.commit()) {
        return report(error, UiLanguage::text("Не удалось сохранить экспорт: %1").arg(file.errorString()));
    }
    return true;
}

Workspace WorkspaceExchange::merged(const Workspace &current, const Workspace &incoming)
{
    Workspace result = current;
    QSet<QUuid> ids;
    for (const CollectionDefinition &collection : current.collections) {
        ids.insert(collection.id);
        for (const RequestDefinition &request : collection.requests) {
            ids.insert(request.id);
        }
    }
    const auto freshId = [&ids] {
        QUuid id;
        do {
            id = QUuid::createUuid();
        } while (ids.contains(id));
        ids.insert(id);
        return id;
    };
    result.collections.reserve(current.collections.size() + incoming.collections.size());
    for (CollectionDefinition collection : incoming.collections) {
        collection.id = freshId();
        for (RequestDefinition &request : collection.requests) {
            request.id = freshId();
        }
        result.collections.append(std::move(collection));
    }
    return result;
}
