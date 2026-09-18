#include "apptheme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QStyleFactory>
#include <QStyleHints>
#include <QVariant>
#include <QWidget>

#include <array>
#include <utility>

namespace {

AppTheme::Theme activeTheme = AppTheme::Theme::Dark;

QString interfaceFontFamily()
{
    // Match Electron's CSS stack, using only families available on this machine.
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
    LineIconEngine(QString name, QColor foreground)
        : m_name(std::move(name)), m_foreground(foreground) {}

    QIconEngine *clone() const override { return new LineIconEngine(m_name, m_foreground); }

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal size = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - size) / 2,
                     rect.y() + (rect.height() - size) / 2);
        p->scale(size / 24., size / 24.);
        QColor foreground = m_foreground;
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
        } else if (m_name == QLatin1String("folder")) {
            QPainterPath path;
            path.moveTo(3, 7); path.lineTo(3, 19); path.lineTo(21, 19);
            path.lineTo(21, 7); path.lineTo(11, 7); path.lineTo(9, 4);
            path.lineTo(3, 4); path.closeSubpath(); p->drawPath(path);
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
    QColor m_foreground;
};

} // namespace

namespace AppTheme {

Theme current() { return activeTheme; }
bool isDark() { return activeTheme == Theme::Dark; }

QColor color(QStringView role)
{
    for (const auto &entry : colors) {
        if (role == QLatin1StringView(entry.name))
            return QColor(QLatin1StringView(isDark() ? entry.dark : entry.light));
    }
    return color(u"text");
}

QIcon icon(QStringView name, QColor overrideColor)
{
    return QIcon(new LineIconEngine(name.toString(),
                                   overrideColor.isValid() ? overrideColor : color(u"secondary")));
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

QString styleSheet()
{
    QString sheet = QStringLiteral(R"(
        QWidget { color: @text; font-family: "@interfaceFont"; font-size: 12px; }
        QMainWindow, QDialog, QMessageBox, QWidget#centralWidget { background: @bg; }
        QWidget#sidebar { background: @sidebar; border-right: 1px solid @lineSoft; }
        QWidget#appHeader { background: @sidebar; border-bottom: 1px solid @lineSoft; }
        QFrame#card { background: @panel; border: 1px solid @line; border-radius: 8px; }
        QFrame#requestEditorCard, QFrame#collectionEditorCard, QFrame#logTableCard, QFrame#logDetailsCard { background: @bg; border: none; border-radius: 0; }
        QWidget#overviewHero { background: qlineargradient(x1: 0, y1: 1, x2: 1, y2: 0, stop: 0 @bg, stop: 0.65 @bg, stop: 1 @sidebar); border: none; }
        QScrollArea { border: none; background: transparent; }
        QScrollArea#requestEditorScrollArea > QWidget { background: @bg; }
        QLabel { background: transparent; border: none; }
        QLabel#appTitle { font-size: 14px; font-weight: 600; }
        QLabel#brandMark { background: @accentSoft; color: @accent; border: 1px solid @accentLine; border-radius: 8px; font-size: 19px; font-weight: 600; }
        QLabel#appEdition { color: @muted; font-size: 9px; padding: 2px 6px; border: 1px solid @lineSoft; border-radius: 4px; }
        QLabel#workspaceCaption { color: @secondary; font-size: 11px; }
        QLabel#sidebarTitle { color: @text; font-size: 14px; font-weight: 600; }
        QLabel#sidebarSection { color: @muted; font-size: 10px; font-weight: 600; }
        QLabel#sectionTitle { font-size: 12px; font-weight: 600; }
        QLabel#editableTitleLabel { font-size: 16px; font-weight: 500; padding: 2px 0; }
        QLineEdit#editableTitleEdit { font-size: 16px; font-weight: 500; padding: 3px 7px; }
        QFrame#requestEditorCard QLabel#editableTitleLabel, QFrame#requestEditorCard QLineEdit#editableTitleEdit { font-size: 14px; font-weight: 500; }
        QFrame#collectionEditorCard QLabel#editableTitleLabel { font-size: 23px; }
        QLabel#fieldLabel, QLabel#statName { color: @muted; font-size: 10px; }
        QLabel#statValue { font-size: 16px; font-weight: 500; }
        QLabel#subtitleLabel, QLabel#urlSummary, QLabel#emptyHint, QLabel#helperText { color: @secondary; font-size: 11px; }
        QLabel#urlSummary { font-family: "Consolas"; color: @muted; font-size: 10px; }
        QLabel#detailsMetadata { color: @muted; background: transparent; border: none; padding: 2px 0; font-family: "Consolas"; font-size: 10px; }
        QLabel#connectionSummary { color: @muted; font-size: 10px; }
        QLabel#eyebrow { color: @muted; font-size: 10px; font-weight: 600; }
        QLabel#statusPill { border: 1px solid @line; border-radius: 5px; padding: 3px 8px; font-size: 10px; background: transparent; color: @secondary; }
        QLabel#statusPill[mode="running"], QLabel#statusPill[state="running"], QLabel#statusPill[state="success"] { background: @accentSoft; color: @accent; border-color: @accentLine; }
        QLabel#statusPill[state="error"] { background: @redSoft; color: @red; border-color: @redSoft; }
        QLabel#validationLabel { font-size: 10px; }
        QLabel#validationLabel[state="valid"] { color: @accent; }
        QLabel#validationLabel[state="invalid"] { color: @red; }
        QLabel#overviewTitle { font-size: 33px; font-weight: 500; }
        QLabel#overviewMetricValue { color: @text; font-size: 23px; font-weight: 500; }
        QLabel#overviewMetricLabel { color: @muted; font-size: 10px; }
        QLabel#workspaceHomeCaption { color: @muted; font-size: 10px; }
        QLabel#emptyTitle, QLabel#logEmptyTitle { font-size: 15px; font-weight: 500; }
        QLabel#countBadge { background: @raised; color: @secondary; padding: 1px 6px; border-radius: 4px; font-size: 10px; }
        QLabel#statusText, QLabel#savedStatus { color: @muted; font-size: 10px; }
        QLabel#savedStatus { color: @secondary; }
        QWidget#logEmptyState { background: @bg; }
        QWidget#logToolbar { background: @panel; border-bottom: 1px solid @lineSoft; }
        QWidget#journalHeading, QWidget#detailsHeading { background: @panel; border: none; border-bottom: 1px solid @lineSoft; }
        QWidget#journalToolbar { background: @bg; border: none; border-bottom: 1px solid @lineSoft; }
        QWidget#responseToolbar { background: @bg; border: none; }
        QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
            color: @text; background: @input; border: 1px solid @line; border-radius: 6px;
            padding: 6px 10px; selection-background-color: @accentSoft; selection-color: @text;
        }
        QLineEdit, QSpinBox, QComboBox { min-height: 18px; }
        QLineEdit:hover:enabled, QSpinBox:hover:enabled, QComboBox:hover:enabled { border-color: @muted; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 1px solid @accentLine; }
        QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: @secondary; background: @bg; border: 1px dashed @line; }
        QLineEdit#sidebarSearch { background: @input; padding: 6px 8px; font-size: 11px; }
        QLineEdit#bodySearch { min-height: 15px; padding: 4px 8px; font-size: 10px; }
        QPlainTextEdit#codeEditor, QPlainTextEdit#detailsEditor { font-family: "Consolas"; font-size: 11px; background: @input; }
        QFrame#requestEditorCard QPlainTextEdit#codeEditor, QFrame#logDetailsCard QPlainTextEdit#codeEditor { background: @bg; border: none; border-radius: 0; padding: 0; }
        QFrame#requestAddressBar { background: @input; border: 1px solid @line; border-radius: 7px; }
        QComboBox#requestProtocol, QComboBox#httpMethod, QLineEdit#requestTarget, QLineEdit#transportAddress { background: transparent; border: none; border-radius: 0; padding: 5px 8px; min-height: 22px; }
        QComboBox#requestProtocol, QComboBox#httpMethod { border-right: 1px solid @lineSoft; padding-right: 20px; font-weight: 600; }
        QComboBox#httpMethod { color: @accent; font-family: "Consolas"; }
        QComboBox#httpMethod[method="POST"], QComboBox#httpMethod[method="PUT"] { color: @orange; }
        QComboBox#httpMethod[method="PATCH"] { color: @blue; }
        QComboBox#httpMethod[method="DELETE"] { color: @red; }
        QLineEdit#requestTarget, QLineEdit#transportAddress { font-family: "Consolas"; }
        QLineEdit#transportAddress { color: @secondary; }
        QComboBox { padding-right: 24px; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox::down-arrow { image: url(@chevronIcon); width: 12px; height: 12px; }
        QComboBox QAbstractItemView { color: @text; background: @panel; border: 1px solid @line; outline: none; padding: 4px; selection-background-color: @accentSoft; selection-color: @text; }
        QComboBox QAbstractItemView::item { min-height: 25px; padding: 2px 7px; border: none; }
        QSpinBox { padding-right: 22px; }
        QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 21px; border: none; background: transparent; border-top-right-radius: 5px; margin: 1px 1px 0 0; }
        QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 21px; border: none; background: transparent; border-bottom-right-radius: 5px; margin: 0 1px 1px 0; }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @raised; }
        QSpinBox::up-button:pressed, QSpinBox::down-button:pressed { background: @accentSoft; }
        QSpinBox::up-arrow { image: url(@spinUpIcon); width: 10px; height: 6px; }
        QSpinBox::down-arrow { image: url(@spinDownIcon); width: 10px; height: 6px; }
        QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled { image: none; }
        QCheckBox { color: @secondary; spacing: 7px; font-size: 11px; background: transparent; }
        QCheckBox:disabled { color: @muted; }
        QCheckBox::indicator, QTableView::indicator { width: 14px; height: 14px; background: @input; border: 1px solid @muted; border-radius: 3px; }
        QCheckBox::indicator:hover, QTableView::indicator:hover { border-color: @accent; }
        QCheckBox::indicator:checked, QTableView::indicator:checked { image: url(@checkIcon); background: @accent; border-color: @accent; }
        QCheckBox::indicator:disabled, QTableView::indicator:disabled { border-color: @line; }
        QPushButton, QToolButton { color: @secondary; background: @panel; border: 1px solid @line; border-radius: 6px; padding: 6px 11px; font-size: 11px; }
        QPushButton { min-height: 18px; }
        QPushButton:hover, QToolButton:hover { color: @text; background: @raised; border-color: @muted; }
        QPushButton:pressed, QToolButton:pressed { background: @accentSoft; border-color: @accentLine; }
        QPushButton:focus, QToolButton:focus { border-color: @accentLine; }
        QPushButton:disabled, QToolButton:disabled { color: @muted; background: @panel; border-color: @lineSoft; }
        QPushButton#sendButton { color: @onAccent; background: @accent; border-color: @accent; font-weight: 600; padding: 5px 14px; }
        QPushButton#sendButton:hover:enabled { background: @accentHover; border-color: @accentHover; }
        QPushButton#sendButton:disabled { color: @muted; background: @raised; border-color: @line; }
        QPushButton#primaryButton { color: @secondary; background: transparent; border-color: @line; }
        QPushButton#primaryButton:hover:enabled { color: @accent; background: @accentSoft; border-color: @accentLine; }
        QPushButton#primaryButton[running="true"] { color: @red; background: @redSoft; border-color: @redSoft; }
        QPushButton#primaryButton[running="true"]:hover { border-color: @red; }
        QPushButton#primaryButton:disabled { color: @muted; background: @panel; border-color: @line; }
        QPushButton#dangerButton { color: @red; background: @redSoft; border-color: @redSoft; }
        QPushButton#dangerButton:hover:enabled { border-color: @red; }
        QPushButton#dangerButton:disabled { color: @muted; background: @panel; border-color: @line; }
    )");
    sheet += QStringLiteral(R"(
        QPushButton#compactButton { min-height: 17px; padding: 4px 8px; font-size: 10px; }
        QPushButton#languageButton { min-height: 17px; padding: 4px 18px 4px 8px; font-size: 10px; }
        QPushButton#breadcrumbButton { color: @secondary; background: transparent; border: 1px solid transparent; padding: 5px 4px; font-size: 10px; }
        QPushButton#breadcrumbButton:hover { color: @text; background: @raised; }
        QPushButton#breadcrumbButton:focus { border-color: @accentLine; }
        QWidget#sidebar QPushButton#compactButton { background: transparent; border-color: transparent; }
        QWidget#sidebar QPushButton#compactButton:hover { color: @text; background: @raised; }
        QWidget#sidebar QPushButton#compactButton:focus { border-color: @accentLine; }
        QPushButton#workspaceHomeButton { color: @text; background: transparent; border: 1px solid transparent; text-align: left; font-size: 14px; font-weight: 600; padding: 9px 10px; }
        QPushButton#workspaceHomeButton:hover { background: @raised; }
        QPushButton#workspaceHomeButton:checked { background: @accentSoft; border-color: @accentLine; }
        QToolButton#editNameButton, QPushButton#toolbarButton, QToolButton#toolbarButton, QPushButton#themeButton, QToolButton#themeButton { background: transparent; border: 1px solid transparent; padding: 5px; }
        QToolButton#editNameButton:hover, QPushButton#toolbarButton:hover, QToolButton#toolbarButton:hover, QPushButton#themeButton:hover, QToolButton#themeButton:hover { background: @raised; color: @accent; border-color: @line; }
        QToolButton#toolbarButton[popupMode="1"] { padding-right: 19px; }
        QToolButton#toolbarButton::menu-button { subcontrol-origin: padding; subcontrol-position: top right; width: 14px; border: none; background: transparent; }
        QToolButton#toolbarButton::menu-button:hover { background: @raised; }
        QToolButton#toolbarButton::menu-arrow { image: url(@chevronIcon); width: 10px; height: 10px; }
        QTreeWidget { color: @secondary; background: @sidebar; border: none; outline: none; padding: 2px; }
        QTreeWidget::item { min-height: 28px; border: 1px solid transparent; border-radius: 5px; padding: 3px 4px; }
        QTreeWidget::item:hover { background: @raised; }
        QTreeWidget::item:selected { color: @text; background: @accentSoft; border-color: @accentLine; }
        QTreeWidget::branch { background: transparent; }
        QListWidget#collectionRequests { color: @text; background: @bg; border: 1px solid @line; border-radius: 7px; outline: none; padding: 2px; }
        QListWidget#collectionRequests::item { min-height: 30px; padding: 5px 10px; border: none; border-bottom: 1px solid @lineSoft; }
        QListWidget#collectionRequests::item:hover { background: @panel; }
        QListWidget#collectionRequests::item:selected { color: @text; background: @accentSoft; }
        QTableView { color: @secondary; background: @bg; alternate-background-color: @panel; border: none; gridline-color: @lineSoft; selection-background-color: @accentSoft; selection-color: @text; outline: none; }
        QTableView::item { padding: 6px 8px; border-bottom: 1px solid @lineSoft; }
        QTableView#logTable { font-size: 11px; }
        QTableView#logTable QHeaderView::section { font-size: 9px; padding: 8px; }
        QTableView::item:hover { background: @raised; }
        QTableView::item:selected { background: @accentSoft; }
        QHeaderView { background: @bg; }
        QHeaderView::section { color: @muted; background: @bg; border: none; border-bottom: 1px solid @lineSoft; padding: 7px 8px; font-size: 10px; font-weight: 500; }
        QTableWidget#httpHeadersTable { border: 1px solid @lineSoft; border-radius: 5px; }
        QTableWidget#httpHeadersTable QLineEdit { border: none; border-radius: 0; padding: 2px 6px; }
        QTableCornerButton::section { background: @panel; border: none; }
        QTabWidget::pane { border: none; background: @bg; }
        QTabWidget#workspaceTabs::pane { top: -1px; }
        QTabBar::tab { color: @secondary; background: transparent; border: none; border-bottom: 2px solid transparent; padding: 8px 13px; font-size: 11px; }
        QTabBar::tab:hover { color: @text; background: @raised; }
        QTabBar::tab:selected { color: @text; border-bottom-color: @accent; }
        QTabBar#workspaceTabBar { background: @sidebar; }
        QTabBar#workspaceTabBar::tab { background: @sidebar; border-right: 1px solid @lineSoft; border-bottom: 2px solid @lineSoft; padding: 9px 12px; min-width: 100px; font-size: 10px; }
        QTabBar#workspaceTabBar::tab:hover { background: @panel; }
        QTabBar#workspaceTabBar::tab:selected { background: @bg; border-bottom-color: @accent; }
        QTabBar#workspaceTabBar::close-button { margin-right: 3px; }
        QTabBar QAbstractButton { background: @sidebar; border: none; border-radius: 3px; padding: 2px; }
        QTabBar QAbstractButton:hover { background: @raised; }
        QToolButton#tabCloseButton { background: transparent; border: none; padding: 0; border-radius: 4px; }
        QToolButton#tabCloseButton:hover { background: @raised; }
        QTabWidget#detailsTabs::pane { border-top: 1px solid @line; }
        QTabWidget#innerDetailsTabs::pane { border-top: 1px solid @lineSoft; }
        QTabBar#detailsTabBar::tab { padding: 7px 12px; }
        QTabBar#innerDetailsTabBar::tab { padding: 6px 12px; font-size: 10px; }
        QTabWidget#editorTabs::pane { border-top: 1px solid @line; }
        QTabBar#editorTabs::tab { padding: 7px 15px; }
        QTabBar#bodyViewMode { background: @panel; border: none; border-radius: 5px; }
        QTabBar#bodyViewMode::tab { color: @muted; background: transparent; border: none; border-radius: 3px; margin: 2px; padding: 4px 9px; font-size: 10px; }
        QTabBar#bodyViewMode::tab:hover { color: @text; background: @raised; }
        QTabBar#bodyViewMode::tab:selected { color: @text; background: @raised; border: none; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle { background: @line; border-radius: 3px; }
        QScrollBar::handle:vertical { min-height: 28px; }
        QScrollBar::handle:horizontal { min-width: 28px; }
        QScrollBar::handle:hover { background: @muted; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
        QSplitter::handle { background: @lineSoft; border: none; }
        QSplitter::handle:hover { background: @accentLine; }
        QStatusBar { color: @muted; background: @sidebar; border-top: 1px solid @lineSoft; font-size: 10px; }
        QStatusBar::item { border: none; }
        QStatusBar QLabel { color: @muted; font-size: 10px; }
        QMenu { color: @secondary; background: @panel; border: 1px solid @line; padding: 5px; border-radius: 6px; }
        QMenu::item { padding: 7px 22px 7px 11px; border-radius: 4px; }
        QMenu::item:selected { color: @text; background: @raised; }
        QMenu::item:disabled { color: @muted; }
        QMenu::separator { background: @line; height: 1px; margin: 5px; }
        QToolTip { color: @text; background: @raised; border: 1px solid @line; padding: 6px 9px; }
    )");
    if (qApp && qApp->isRightToLeft()) sheet += QStringLiteral(R"(
        QWidget#sidebar { border-right: none; border-left: 1px solid @lineSoft; }
        QPushButton#workspaceHomeButton { text-align: right; }
        QPushButton#languageButton { padding: 4px 8px 4px 18px; }
        QToolButton#toolbarButton[popupMode="1"] { padding-right: 5px; padding-left: 19px; }
        QToolButton#toolbarButton::menu-button { subcontrol-position: top left; }
        QMenu::item { padding: 7px 11px 7px 22px; }
    )");
    sheet.replace(QStringLiteral("@interfaceFont"), interfaceFontFamily());
    sheet.replace(QStringLiteral("@checkIcon"), isDark()
        ? QStringLiteral(":/Installer/check-dark.svg") : QStringLiteral(":/Installer/check-light.svg"));
    sheet.replace(QStringLiteral("@chevronIcon"), isDark()
        ? QStringLiteral(":/Installer/chevron-dark.svg") : QStringLiteral(":/Installer/chevron-light.svg"));
    sheet.replace(QStringLiteral("@spinUpIcon"), isDark()
        ? QStringLiteral(":/Installer/spin-up-dark.svg") : QStringLiteral(":/Installer/spin-up-light.svg"));
    sheet.replace(QStringLiteral("@spinDownIcon"), isDark()
        ? QStringLiteral(":/Installer/spin-down-dark.svg") : QStringLiteral(":/Installer/spin-down-light.svg"));
    // Longer tokens must go first: @accentSoft contains @accent.
    for (auto it = colors.rbegin(); it != colors.rend(); ++it)
        sheet.replace(QLatin1Char('@') + QString::fromLatin1(it->name),
                      QLatin1StringView(isDark() ? it->dark : it->light));
    return sheet;
}

void apply(Theme theme)
{
    activeTheme = theme;
    if (!qApp) return;
    if (!qApp->property("multiSenderFusionStyle").toBool()) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        qApp->setProperty("multiSenderFusionStyle", true);
        QFont font(interfaceFontFamily());
        font.setPixelSize(12);
        font.setWeight(QFont::Normal);
        font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);
        qApp->setFont(font);
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
    qApp->setStyleSheet(styleSheet());
    for (QWidget *widget : QApplication::allWidgets()) {
        if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
            const QString name = button->property("iconName").toString();
            if (!name.isEmpty()) {
                const QString tone = button->property("iconTone").toString();
                button->setIcon(icon(name, tone.isEmpty() ? QColor{} : color(tone)));
            }
        }
        widget->update();
    }
}

} // namespace AppTheme
