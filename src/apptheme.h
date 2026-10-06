#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringView>

namespace AppTheme {

enum class Theme { Dark, Light };

Theme current();
bool isDark();
QColor color(QStringView role);
QColor operationColor(QStringView operation);
QIcon icon(QStringView name, QColor overrideColor = {});
QIcon icon(QStringView name, QStringView colorRole);
void apply(Theme theme);
QString themeName(Theme theme);
Theme fromName(QStringView name);

} // namespace AppTheme
