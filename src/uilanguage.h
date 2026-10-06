#pragma once

#include <QString>
#include <QStringList>
#include <QObject>
#include <functional>
#include <QIcon>
#include <QVariant>

class QTabBar;
class QComboBox;
class QAction;

namespace UiLanguage {
// Bind only application-owned captions; never infer translations from user text.
void bind(QObject *owner, const QByteArray &key, std::function<void()> update);
void set(QObject *object, const char *property, const char *source);
void onChange(QObject *owner, std::function<void()> update);
void retranslate();
int addTab(QTabBar *tabs, const char *source, const QIcon &icon = {});
void addItem(QComboBox *combo, const char *source, const QVariant &data = {});
QAction *action(QAction *action, const char *source);
QString text(const char *source);
QString normalized(const QString &language);
QStringList available();
QString nativeName(const QString &language);
bool apply(const QString &language);
QString current();
}
