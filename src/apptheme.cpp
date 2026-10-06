#include "apptheme.h"
#include "appstyle.h"

#include <QApplication>
#include <QFont>
#include <QPointer>
#include <QFontDatabase>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QStyleHints>
#include <QWidget>

#include <array>
#include <utility>

namespace {

AppTheme::Theme activeTheme = AppTheme::Theme::Dark;

QString interfaceFontFamily()
{
    // Prefer the interface font families available on this machine.
    static const QString family = [] {
        const auto available = QFontDatabase::families();
        for (const auto &candidate : {QStringLiteral("Inter"),
                                     QStringLiteral("Segoe UI Variable Text"),
                                     QStringLiteral("Segoe UI")}) {
            if (available.contains(candidate, Qt::CaseInsensitive)) return candidate;
        }
        return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    }();
    return family;
}

struct ThemeColor {
    const char *name;
    const char *dark;
    const char *light;
};

constexpr std::array colors {
    ThemeColor{"bg", "#101316", "#f7f9f8"},
    ThemeColor{"sidebar", "#14181c", "#eef2ef"},
    ThemeColor{"panel", "#181d22", "#ffffff"},
    ThemeColor{"raised", "#20262c", "#e8efeb"},
    ThemeColor{"input", "#11161a", "#f4f7f5"},
    ThemeColor{"line", "#2a3137", "#d4dfd9"},
    ThemeColor{"lineSoft", "#232a30", "#e5ebe7"},
    ThemeColor{"text", "#e7edea", "#1e342a"},
    ThemeColor{"secondary", "#a1aca9", "#536b60"},
    ThemeColor{"muted", "#81908a", "#687d71"},
    ThemeColor{"accent", "#79dfb5", "#16794e"},
    ThemeColor{"accentHover", "#9aebca", "#12673f"},
    ThemeColor{"accentSoft", "#203e33", "#ddf1e5"},
    ThemeColor{"accentLine", "#365e4d", "#b0d8c1"},
    ThemeColor{"onAccent", "#102b20", "#ffffff"},
    ThemeColor{"blue", "#89bdf6", "#326fb0"},
    ThemeColor{"orange", "#efbd82", "#a56821"},
    ThemeColor{"purple", "#baa0e9", "#855abd"},
    ThemeColor{"red", "#ef9290", "#bd4a48"},
    ThemeColor{"redSoft", "#412a2b", "#fce8e6"}
};

// A vector engine keeps small toolbar icons sharp at every Windows display scale.
class LineIconEngine final : public QIconEngine
{
public:
    LineIconEngine(QString name, QString colorRole, QColor foreground)
        : m_name(std::move(name)), m_colorRole(std::move(colorRole)), m_foreground(foreground) {}

    QIconEngine *clone() const override { return new LineIconEngine(m_name, m_colorRole, m_foreground); }

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal size = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - size) / 2,
                     rect.y() + (rect.height() - size) / 2);
        p->scale(size / 24., size / 24.);
        QColor foreground = m_colorRole.isEmpty() ? m_foreground : AppTheme::color(m_colorRole);
        if (mode == QIcon::Disabled) foreground.setAlpha(85);
        p->setPen(QPen(foreground, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        const auto line = [p](qreal x1, qreal y1, qreal x2, qreal y2) {
            p->drawLine(QPointF(x1, y1), QPointF(x2, y2));
        };
        const auto poly = [p](std::initializer_list<QPointF> points) {
            p->drawPolyline(QPolygonF(points));
        };
        if (m_name == QLatin1String("sun")) {
            p->drawEllipse(QPointF(12, 12), 4, 4);
            line(12, 2, 12, 4); line(12, 20, 12, 22);
            line(2, 12, 4, 12); line(20, 12, 22, 12);
            line(5, 5, 6.5, 6.5); line(17.5, 17.5, 19, 19);
            line(5, 19, 6.5, 17.5); line(17.5, 6.5, 19, 5);
        } else if (m_name == QLatin1String("moon")) {
            QPainterPath path;
            path.moveTo(20.5, 13.2);
            path.cubicTo(19.9, 20.5, 10.6, 23.4, 5.8, 18);
            path.cubicTo(0.6, 12.6, 3.9, 4.2, 10.7, 3.4);
            path.cubicTo(7.4, 10.2, 13.6, 16.2, 20.5, 13.2);
            p->drawPath(path);
        } else if (m_name == QLatin1String("send")) {
            p->drawPolygon(QPolygonF{{3, 10}, {21, 3}, {14, 21}, {11, 13}});
            line(11, 13, 21, 3);
        } else if (m_name == QLatin1String("play")) {
            p->drawPolygon(QPolygonF{{8, 4}, {20, 12}, {8, 20}});
        } else if (m_name == QLatin1String("stop")) {
            p->drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
        } else if (m_name == QLatin1String("plus")) {
            line(5, 12, 19, 12); line(12, 5, 12, 19);
        } else if (m_name == QLatin1String("folder") || m_name == QLatin1String("folder-plus")) {
            QPainterPath path;
            path.moveTo(3, 7); path.lineTo(3, 19); path.lineTo(21, 19);
            path.lineTo(21, 7); path.lineTo(11, 7); path.lineTo(9, 4);
            path.lineTo(3, 4); path.closeSubpath(); p->drawPath(path);
            if (m_name == QLatin1String("folder-plus")) {
                line(9, 13, 15, 13); line(12, 10, 12, 16);
            }
        } else if (m_name == QLatin1String("search")) {
            p->drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5); line(15.5, 15.5, 21, 21);
        } else if (m_name == QLatin1String("copy")) {
            p->drawRoundedRect(QRectF(8, 8, 13, 13), 2, 2);
            poly({{16, 5}, {16, 3}, {3, 3}, {3, 16}, {5, 16}});
        } else if (m_name == QLatin1String("trash")) {
            line(4, 6, 20, 6); poly({{9, 6}, {9, 3}, {15, 3}, {15, 6}});
            poly({{6, 6}, {7, 21}, {17, 21}, {18, 6}});
            line(10, 10, 10, 17); line(14, 10, 14, 17);
        } else if (m_name == QLatin1String("code")) {
            poly({{7, 6}, {2, 12}, {7, 18}}); poly({{17, 6}, {22, 12}, {17, 18}});
            line(14, 3, 10, 21);
        } else if (m_name == QLatin1String("chevron")) {
            poly({{9, 5}, {16, 12}, {9, 19}});
        } else if (m_name == QLatin1String("check")) {
            poly({{4, 12}, {9, 17}, {20, 6}});
        } else if (m_name == QLatin1String("minus")) {
            line(5, 12, 19, 12);
        } else if (m_name == QLatin1String("error")) {
            p->drawEllipse(QRectF(3, 3, 18, 18));
            line(12, 7, 12, 13); line(12, 16, 12, 17);
        } else if (m_name == QLatin1String("close")) {
            line(6, 6, 18, 18); line(18, 6, 6, 18);
        } else if (m_name == QLatin1String("edit")) {
            poly({{14, 5}, {19, 10}, {8, 21}, {3, 21}, {3, 16}, {17, 2}, {22, 7}, {19, 10}});
        } else if (m_name == QLatin1String("refresh")) {
            p->drawArc(QRectF(4, 4, 16, 16), 40 * 16, 290 * 16);
            poly({{16, 3}, {20, 5}, {20, 10}});
        } else if (m_name == QLatin1String("terminal")) {
            p->drawRoundedRect(QRectF(2, 4, 20, 16), 3, 3);
            poly({{6, 9}, {9, 12}, {6, 15}}); line(12, 15, 17, 15);
        } else if (m_name == QLatin1String("home")) {
            poly({{3, 10}, {12, 3}, {21, 10}, {21, 21}, {15, 21}, {15, 14}, {9, 14}, {9, 21}, {3, 21}, {3, 10}});
        } else if (m_name == QLatin1String("bolt") || m_name == QLatin1String("activity")) {
            poly({{2, 12}, {6, 12}, {10, 3}, {14, 21}, {18, 12}, {22, 12}});
        } else if (m_name == QLatin1String("download") || m_name == QLatin1String("upload")) {
            const bool down = m_name == QLatin1String("download");
            line(12, 3, 12, 15);
            if (down) poly({{7, 10}, {12, 15}, {17, 10}});
            else poly({{7, 8}, {12, 3}, {17, 8}});
            poly({{4, 15}, {4, 21}, {20, 21}, {20, 15}});
        } else if (m_name == QLatin1String("settings")) {
            line(3, 6, 21, 6); line(3, 12, 21, 12); line(3, 18, 21, 18);
            p->setBrush(AppTheme::color(u"panel"));
            p->drawEllipse(QPointF(8, 6), 2, 2);
            p->drawEllipse(QPointF(16, 12), 2, 2);
            p->drawEllipse(QPointF(10, 18), 2, 2);
        } else if (m_name == QLatin1String("headers")) {
            p->drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
            line(3, 9, 21, 9); line(10, 9, 10, 20);
            line(3, 14, 21, 14);
        } else if (m_name == QLatin1String("database")) {
            p->drawEllipse(QRectF(4, 3, 16, 6));
            line(4, 6, 4, 18); line(20, 6, 20, 18);
            p->drawArc(QRectF(4, 9, 16, 6), 180 * 16, 180 * 16);
            p->drawArc(QRectF(4, 15, 16, 6), 180 * 16, 180 * 16);
        } else if (m_name == QLatin1String("external")) {
            poly({{7, 17}, {18, 6}, {10, 6}}); line(18, 6, 18, 14);
        } else if (m_name == QLatin1String("keyboard")) {
            p->drawRoundedRect(QRectF(2, 5, 20, 14), 2, 2);
            for (int y : {9, 12}) for (int x : {6, 10, 14, 18}) line(x, y, x + .2, y);
            line(7, 16, 17, 16);
        } else if (m_name == QLatin1String("clock")) {
            p->drawEllipse(QPointF(12, 12), 9, 9); poly({{12, 6}, {12, 12}, {16, 14}});
        } else {
            p->drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
            line(9, 8, 15, 8); line(9, 12, 15, 12); line(9, 16, 13, 16);
        }
        p->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    QString m_name;
    QString m_colorRole;
    QColor m_foreground;
};

} // namespace

namespace AppTheme {

Theme current() { return activeTheme; }
bool isDark() { return activeTheme == Theme::Dark; }

QColor color(QStringView role)
{
    // Parse theme colors once instead of parsing hex strings on every paint.
    static const auto resolved = [] {
        std::array<std::array<QColor, 2>, colors.size()> result;
        for (std::size_t index = 0; index < colors.size(); ++index) {
            result[index][0] = QColor(QLatin1StringView(colors[index].dark));
            result[index][1] = QColor(QLatin1StringView(colors[index].light));
        }
        return result;
    }();
    for (std::size_t index = 0; index < colors.size(); ++index) {
        if (role == QLatin1StringView(colors[index].name))
            return resolved[index][isDark() ? 0 : 1];
    }
    return resolved[7][isDark() ? 0 : 1];
}

QIcon icon(QStringView name, QColor overrideColor)
{
    const QString colorRole = overrideColor.isValid() ? QString{} : QStringLiteral("secondary");
    return QIcon(new LineIconEngine(name.toString(), colorRole, overrideColor));
}

QIcon icon(QStringView name, QStringView colorRole)
{
    // Keep the semantic role in the engine so existing icons follow the palette
    // without visiting every button/tab or changing their geometry on a switch.
    return QIcon(new LineIconEngine(name.toString(), colorRole.toString(), {}));
}

QString themeName(Theme theme)
{
    return theme == Theme::Light ? QStringLiteral("light") : QStringLiteral("dark");
}

Theme fromName(QStringView name)
{
    return name.compare(u"light", Qt::CaseInsensitive) == 0 ? Theme::Light : Theme::Dark;
}

QColor operationColor(QStringView operation)
{
    if (operation == u"GET") return color(u"accent");
    if (operation == u"POST" || operation == u"COM") return color(u"orange");
    if (operation == u"UDP") return color(u"purple");
    if (operation == u"DELETE") return color(u"red");
    return color(u"blue");
}

void apply(Theme theme)
{
    const bool changed = activeTheme != theme;
    activeTheme = theme;
    if (!qApp) return;
    static QPointer<AppStyle> appStyle;
    const bool initialize = !appStyle || QApplication::style() != appStyle;
    if (!initialize && !changed) return;
    if (initialize) {
        appStyle = new AppStyle;
        QApplication::setStyle(appStyle);
        QFont font(interfaceFontFamily());
        font.setPixelSize(12);
        font.setWeight(QFont::Normal);
        font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);
        qApp->setFont(font);
    }
    QList<QPointer<QWidget>> repaintWindows;
    for (auto *window : QApplication::topLevelWidgets()) {
        if (window->updatesEnabled()) {
            repaintWindows.append(window);
            window->setUpdatesEnabled(false);
        }
    }
    QGuiApplication::styleHints()->setColorScheme(isDark() ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    QPalette palette;
    palette.setColor(QPalette::Window, color(u"bg"));
    palette.setColor(QPalette::WindowText, color(u"text"));
    palette.setColor(QPalette::Base, color(u"input"));
    palette.setColor(QPalette::AlternateBase, color(u"panel"));
    palette.setColor(QPalette::Text, color(u"text"));
    palette.setColor(QPalette::PlaceholderText, color(u"muted"));
    palette.setColor(QPalette::Button, color(u"panel"));
    palette.setColor(QPalette::ButtonText, color(u"secondary"));
    palette.setColor(QPalette::BrightText, color(u"onAccent"));
    palette.setColor(QPalette::Light, color(u"raised"));
    palette.setColor(QPalette::Midlight, color(u"line"));
    palette.setColor(QPalette::Mid, color(u"line"));
    palette.setColor(QPalette::Dark, color(u"sidebar"));
    palette.setColor(QPalette::Shadow, color(u"bg"));
    palette.setColor(QPalette::Highlight, color(u"accentSoft"));
    palette.setColor(QPalette::HighlightedText, color(u"text"));
    palette.setColor(QPalette::Accent, color(u"accent"));
    palette.setColor(QPalette::Link, color(u"accent"));
    palette.setColor(QPalette::LinkVisited, color(u"purple"));
    palette.setColor(QPalette::ToolTipBase, color(u"raised"));
    palette.setColor(QPalette::ToolTipText, color(u"text"));
    for (QPalette::ColorRole role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, color(u"muted"));
    qApp->setPalette(palette);
    appStyle->refreshTheme();
    for (const auto &window : repaintWindows)
        if (window) window->setUpdatesEnabled(true);
}

} // namespace AppTheme
