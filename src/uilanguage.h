#pragma once

#include <QString>
#include <QStringList>

namespace UiLanguage {
inline constexpr int RestartExitCode = 76;
QString text(const char *source);
QString normalized(const QString &language);
QStringList available();
QString nativeName(const QString &language);
bool apply(const QString &language);
QString current();
}
