#include "uilanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QPointer>
#include <QMap>
#include <QVariant>
#include <QAction>

#include <memory>

namespace {
QString activeLanguage = QStringLiteral("ru");
class CatalogueTranslator final : public QTranslator {
public:
    QJsonObject messages;
    QString translate(const char *context, const char *source, const char *, int) const override {
        if (qstrcmp(context, "MultiSender") != 0) return {};
        return messages.value(QString::fromUtf8(source)).toString();
    }
    bool isEmpty() const override { return messages.isEmpty(); }
};
std::unique_ptr<CatalogueTranslator> catalogue;
std::unique_ptr<QTranslator> qtTranslator;
class Bindings final : public QObject {
public:
    explicit Bindings(QObject *parent) : QObject(parent) {}
    QMap<QByteArray, std::function<void()>> captions;
    std::function<void()> refresh;
};
QList<QPointer<Bindings>> bindings;
Bindings *bindingsFor(QObject *owner) {
    if (auto *existing = owner->property("uiLanguageBindings").value<QObject *>())
        return static_cast<Bindings *>(existing);
    auto *item = new Bindings(owner);
    owner->setProperty("uiLanguageBindings", QVariant::fromValue(static_cast<QObject *>(item)));
    bindings.append(item);
    return item;
}
}

namespace UiLanguage {
QAction *action(QAction *value, const char *source) {
    set(value, "text", source);
    return value;
}
void bind(QObject *owner, const QByteArray &key, std::function<void()> update) {
    bindingsFor(owner)->captions.insert(key, update);
    update();
}
void set(QObject *object, const char *property, const char *source) {
    const QByteArray key(property), original(source);
    bind(object, key, [object, key, original] {
        object->setProperty(key.constData(), text(original.constData()));
    });
}
void onChange(QObject *owner, std::function<void()> update) {
    bindingsFor(owner)->refresh = std::move(update);
}
void retranslate() {
    bindings.removeIf([](const auto &item) { return item.isNull(); });
    const auto snapshot = bindings;
    for (const auto &item : snapshot) {
        if (!item) continue;
        const auto captions = item->captions;
        for (const auto &update : captions) update();
    }
    for (const auto &item : snapshot)
        if (item && item->refresh) item->refresh();
}
QString text(const char *source) { return QCoreApplication::translate("MultiSender", source); }
QStringList available() { return {QStringLiteral("ru"), QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("pt"), QStringLiteral("tr"), QStringLiteral("ar")}; }
QString normalized(const QString &language) {
    const auto code = language.toLower().section(u'_', 0, 0).section(u'-', 0, 0);
    return available().contains(code) ? code : QStringLiteral("ru");
}
QString nativeName(const QString &language) {
    if (language == QStringLiteral("en")) return QStringLiteral("English");
    if (language == QStringLiteral("es")) return QString::fromUtf8("Español");
    if (language == QStringLiteral("pt")) return QString::fromUtf8("Português");
    if (language == QStringLiteral("tr")) return QString::fromUtf8("Türkçe");
    if (language == QStringLiteral("ar")) return QString::fromUtf8("العربية");
    return QString::fromUtf8("Русский");
}
QString current() { return activeLanguage; }
bool apply(const QString &language) {
    const auto code = normalized(language);
    auto next = std::make_unique<CatalogueTranslator>();
    if (code != QStringLiteral("ru")) {
        QFile file(QStringLiteral(":/i18n/%1.json").arg(code));
        if (!file.open(QIODevice::ReadOnly)) return false;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject() || document.object().isEmpty()) return false;
        next->messages = document.object();
    }
    if (catalogue) QCoreApplication::removeTranslator(catalogue.get());
    if (qtTranslator) QCoreApplication::removeTranslator(qtTranslator.get());
    catalogue = std::move(next);
    qtTranslator = std::make_unique<QTranslator>();
    const QString qtLocale = code == QStringLiteral("pt") ? QStringLiteral("pt_BR") : code;
    const QString directory = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    if (qtTranslator->load(QStringLiteral("qt_") + qtLocale, directory)
        || qtTranslator->load(QStringLiteral("qtbase_") + qtLocale, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qtTranslator.get());
    if (!catalogue->isEmpty()) QCoreApplication::installTranslator(catalogue.get());
    activeLanguage = code;
    QLocale::setDefault(QLocale(qtLocale));
    return true;
}
}
