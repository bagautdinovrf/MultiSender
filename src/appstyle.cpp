#include "appstyle.h"
#include "apptheme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeView>
#include <QVariant>

#include <algorithm>
#include <array>
#include <utility>

namespace {

QColor color(QStringView role) { return AppTheme::color(role); }

bool named(const QWidget *widget, QStringView name)
{
    return widget && widget->objectName() == name;
}

bool ancestorNamed(const QWidget *widget, QStringView name)
{
    for (const QWidget *parent = widget ? widget->parentWidget() : nullptr;
         parent; parent = parent->parentWidget())
        if (named(parent, name)) return true;
    return false;
}

bool addressControl(const QWidget *widget)
{
    return named(widget, u"requestProtocol") || named(widget, u"httpMethod")
        || named(widget, u"requestTarget") || named(widget, u"transportAddress");
}

bool flatEditor(const QWidget *widget)
{
    return named(widget, u"codeEditor")
        && (ancestorNamed(widget, u"requestEditorCard") || ancestorNamed(widget, u"logDetailsCard"));
}

bool headerEditor(const QWidget *widget)
{
    return qobject_cast<const QLineEdit *>(widget) && ancestorNamed(widget, u"httpHeadersTable");
}

bool comboPopup(const QWidget *widget)
{
    return qobject_cast<const QAbstractItemView *>(widget)
        && qobject_cast<const QComboBox *>(widget ? widget->parentWidget() : nullptr);
}

bool popupAncestor(const QWidget *widget)
{
    for (const QWidget *parent = widget ? widget->parentWidget() : nullptr;
         parent; parent = parent->parentWidget())
        if (qobject_cast<const QComboBox *>(parent)) return true;
    return false;
}

bool toolLike(const QWidget *widget)
{
    return named(widget, u"editNameButton") || named(widget, u"toolbarButton")
        || named(widget, u"themeButton") || named(widget, u"tabCloseButton");
}

bool hasSurface(const QWidget *widget)
{
    if (!widget) return false;
    constexpr std::array names{u"sidebar", u"appHeader", u"card", u"requestEditorCard",
        u"collectionEditorCard", u"logTableCard", u"logDetailsCard", u"overviewHero",
        u"requestAddressBar", u"logEmptyState", u"logToolbar", u"journalHeading",
        u"detailsHeading", u"journalToolbar", u"responseToolbar", u"shortcutPopup",
        u"brandMark", u"appEdition", u"bodyByteCount", u"statusPill", u"countBadge",
        u"journalCountBadge", u"keycap", u"workspaceTabBar", u"bodyViewMode"};
    return std::ranges::any_of(names, [widget](const auto name) { return named(widget, name); });
}

void roundedRect(QPainter *painter, const QRect &rect, const QColor &fill,
                 const QColor &border = Qt::transparent, qreal radius = 6,
                 Qt::PenStyle penStyle = Qt::SolidLine)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(fill.alpha() ? QBrush(fill) : QBrush(Qt::NoBrush));
    painter->setPen(border.alpha() ? QPen(border, 1, penStyle) : QPen(Qt::NoPen));
    painter->drawRoundedRect(QRectF(rect).adjusted(.5, .5, -.5, -.5), radius, radius);
    painter->restore();
}

void arrow(QPainter *painter, const QRect &rect, Qt::ArrowType type, const QColor &foreground)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(foreground, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const QPointF center = QRectF(rect).center();
    const qreal horizontal = std::min(3.5, rect.width() / 3.0);
    const qreal vertical = std::min(2.0, rect.height() / 3.0);
    QPolygonF points;
    switch (type) {
    case Qt::UpArrow:
        points = {{center.x() - horizontal, center.y() + vertical},
                  {center.x(), center.y() - vertical}, {center.x() + horizontal, center.y() + vertical}};
        break;
    case Qt::LeftArrow:
        points = {{center.x() + vertical, center.y() - horizontal},
                  {center.x() - vertical, center.y()}, {center.x() + vertical, center.y() + horizontal}};
        break;
    case Qt::RightArrow:
        points = {{center.x() - vertical, center.y() - horizontal},
                  {center.x() + vertical, center.y()}, {center.x() - vertical, center.y() + horizontal}};
        break;
    default:
        points = {{center.x() - horizontal, center.y() - vertical},
                  {center.x(), center.y() + vertical}, {center.x() + horizontal, center.y() - vertical}};
        break;
    }
    painter->drawPolyline(points);
    painter->restore();
}

void checkMark(QPainter *painter, const QRect &rect, const QColor &foreground, bool partial = false)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(foreground, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QRectF r(rect);
    if (partial) painter->drawLine(QPointF(r.left() + 4, r.center().y()), QPointF(r.right() - 4, r.center().y()));
    else painter->drawPolyline(QPolygonF{{r.left() + 4, r.center().y()},
                                       {r.left() + 7, r.bottom() - 4}, {r.right() - 3.5, r.top() + 4}});
    painter->restore();
}

struct ButtonColors {
    QColor foreground = color(u"secondary");
    QColor background = color(u"panel");
    QColor border = color(u"line");
    qreal radius = 6;
};

ButtonColors buttonColors(const QWidget *widget, QStyle::State state)
{
    const bool enabled = state.testFlag(QStyle::State_Enabled);
    const bool hover = enabled && state.testFlag(QStyle::State_MouseOver);
    const bool pressed = enabled && state.testFlag(QStyle::State_Sunken);
    const bool checked = enabled && state.testFlag(QStyle::State_On);
    const bool focus = enabled && state.testFlag(QStyle::State_HasFocus);
    ButtonColors result;
    if (hover) result = {color(u"text"), color(u"raised"), color(u"muted"), 6};
    if (focus) result.border = color(u"accentLine");
    if (named(widget, u"sendButton")) {
        result = {color(u"onAccent"), color(hover ? u"accentHover" : u"accent"),
                  color(hover ? u"accentHover" : u"accent"), 6};
    } else if (named(widget, u"primaryButton")) {
        if (widget->property("running").toBool())
            result = {color(u"red"), color(u"redSoft"), color(hover ? u"red" : u"redSoft"), 6};
        else result = {color(hover ? u"accent" : u"secondary"),
                       hover ? color(u"accentSoft") : QColor(Qt::transparent),
                       color(hover || focus ? u"accentLine" : u"line"), 6};
    } else if (named(widget, u"dangerButton")) {
        result = {color(u"red"), color(u"redSoft"), color(hover ? u"red" : u"redSoft"), 6};
    } else if (toolLike(widget)) {
        result = {color(hover ? u"accent" : u"secondary"), hover ? color(u"raised") : QColor(Qt::transparent),
                  hover ? color(u"line") : QColor(Qt::transparent), named(widget, u"tabCloseButton") ? 4.0 : 6.0};
        if (named(widget, u"tabCloseButton")) result.border = Qt::transparent;
        if (focus) result.border = color(u"accentLine");
    } else if (named(widget, u"breadcrumbButton")
               || (named(widget, u"compactButton") && ancestorNamed(widget, u"sidebar"))) {
        result = {color(hover ? u"text" : u"secondary"), hover ? color(u"raised") : QColor(Qt::transparent),
                  focus ? color(u"accentLine") : QColor(Qt::transparent), 6};
    } else if (named(widget, u"workspaceHomeButton")) {
        result = {color(u"text"), checked ? color(u"accentSoft") : hover ? color(u"raised") : QColor(Qt::transparent),
                  checked || focus ? color(u"accentLine") : QColor(Qt::transparent), 6};
    } else if (named(widget, u"dataDirectoryButton")) {
        result = {color(u"secondary"), hover ? color(u"panel") : QColor(Qt::transparent),
                  hover ? color(u"lineSoft") : QColor(Qt::transparent), 6};
    } else if (qobject_cast<const QTabBar *>(widget ? widget->parentWidget() : nullptr)) {
        result = {color(u"secondary"), color(hover ? u"raised" : u"sidebar"), Qt::transparent, 3};
    } else if (named(widget, u"transferButton") && hover) {
        result.border = color(u"accentLine");
    }
    if (pressed) {
        result.background = color(named(widget, u"sendButton") ? u"accentHover" : u"accentSoft");
        result.border = color(u"accentLine");
    }
    if (!enabled) {
        result.foreground = color(u"muted");
        result.background = color(named(widget, u"sendButton") ? u"raised" : u"panel");
        result.border = color(named(widget, u"primaryButton") || named(widget, u"dangerButton")
                              || named(widget, u"sendButton") ? u"line" : u"lineSoft");
    }
    return result;
}

QMargins buttonPadding(const QWidget *widget)
{
    if (named(widget, u"tabCloseButton") || named(widget, u"dataDirectoryButton")) return {};
    if (named(widget, u"sendButton")) return {17, 5, 17, 5};
    if (named(widget, u"compactButton")) return {8, 4, 8, 4};
    if (named(widget, u"transferButton")) return {10, 5, 10, 5};
    if (named(widget, u"languageButton")) return {8, 4, 18, 4};
    if (named(widget, u"breadcrumbButton")) return {4, 5, 4, 5};
    if (named(widget, u"workspaceHomeButton")) return {10, 9, 10, 9};
    if (toolLike(widget)) return {5, 5, 5, 5};
    if (qobject_cast<const QTabBar *>(widget ? widget->parentWidget() : nullptr)) return {2, 2, 2, 2};
    return {11, 6, 11, 6};
}

QMargins inputPadding(const QWidget *widget)
{
    if (headerEditor(widget)) return {10, 2, 9, 2};
    if (addressControl(widget)) return {8, 5, 8, 5};
    if (named(widget, u"editableTitleEdit")) return {7, 3, 7, 3};
    if (named(widget, u"sidebarSearch")) return {8, 6, 8, 6};
    if (named(widget, u"bodySearch")) return {8, 4, 8, 4};
    return {10, 6, 10, 6};
}

QMargins visualMargins(Qt::LayoutDirection direction, QMargins margins)
{
    if (direction == Qt::RightToLeft) return {margins.right(), margins.top(), margins.left(), margins.bottom()};
    return margins;
}

void inputFrame(QPainter *painter, const QStyleOption *option, const QWidget *widget)
{
    // Editable combos and spin boxes paint their frame on the outer control.
    // QLineEdit can still request PE_PanelLineEdit when hasFrame() is false.
    if (qobject_cast<const QLineEdit *>(widget)
        && (qobject_cast<const QComboBox *>(widget->parentWidget())
            || qobject_cast<const QAbstractSpinBox *>(widget->parentWidget()))) return;
    if (headerEditor(widget) || flatEditor(widget)) return;
    if (addressControl(widget)) {
        if (qobject_cast<const QComboBox *>(widget)) {
            painter->save();
            painter->setPen(color(u"lineSoft"));
            const int x = option->direction == Qt::RightToLeft ? option->rect.left() : option->rect.right();
            painter->drawLine(x, option->rect.top() + 4, x, option->rect.bottom() - 4);
            painter->restore();
        }
        return;
    }
    const bool enabled = option->state.testFlag(QStyle::State_Enabled);
    const bool focus = option->state.testFlag(QStyle::State_HasFocus)
        || (widget && widget->hasFocus());
    const QColor border = color(enabled && focus ? u"accentLine"
                                : enabled && option->state.testFlag(QStyle::State_MouseOver) ? u"muted" : u"line");
    roundedRect(painter, option->rect, color(enabled ? u"input" : u"bg"), border, 6,
                enabled ? Qt::SolidLine : Qt::DashLine);
}

void paintSurface(QPainter *painter, const QWidget *widget)
{
    const QRect rect = widget->rect();
    const auto line = [painter, &rect](QStringView role, bool top = false) {
        painter->setPen(color(role));
        const int y = top ? rect.top() : rect.bottom();
        painter->drawLine(rect.left(), y, rect.right(), y);
    };
    if (named(widget, u"sidebar") || named(widget, u"appHeader") || named(widget, u"workspaceTabBar")) {
        painter->fillRect(rect, color(u"sidebar"));
        if (named(widget, u"sidebar")) {
            painter->setPen(color(u"lineSoft"));
            const int x = widget->isRightToLeft() ? rect.left() : rect.right();
            painter->drawLine(x, rect.top(), x, rect.bottom());
        } else if (named(widget, u"appHeader")) line(u"lineSoft");
    } else if (named(widget, u"overviewHero")) {
        QLinearGradient gradient(rect.bottomLeft(), rect.topRight());
        gradient.setColorAt(0, color(u"bg"));
        gradient.setColorAt(.65, color(u"bg"));
        gradient.setColorAt(1, color(u"sidebar"));
        painter->fillRect(rect, gradient);
    } else if (named(widget, u"card") || named(widget, u"shortcutPopup")) {
        roundedRect(painter, rect, color(u"panel"), color(u"line"), 8);
    } else if (named(widget, u"requestAddressBar")) {
        const bool focused = widget->property("focused").toBool();
        roundedRect(painter, rect, color(focused ? u"panel" : u"input"), color(focused ? u"accent" : u"line"), 7);
    } else if (named(widget, u"brandMark")) {
        roundedRect(painter, rect, color(u"accentSoft"), color(u"accentLine"), 8);
    } else if (named(widget, u"statusPill")) {
        const QString state = widget->property("state").toString();
        const bool error = state == u"error";
        const bool running = !error && (state == u"running" || state == u"success"
            || widget->property("mode").toString() == u"running");
        roundedRect(painter, rect, running ? color(u"accentSoft") : error ? color(u"redSoft") : QColor(Qt::transparent),
                    color(running ? u"accentLine" : error ? u"redSoft" : u"line"), 5);
    } else if (named(widget, u"countBadge") || named(widget, u"keycap")) {
        roundedRect(painter, rect, color(u"raised"), named(widget, u"keycap") ? color(u"line") : QColor(Qt::transparent), 4);
    } else if (named(widget, u"appEdition") || named(widget, u"bodyByteCount") || named(widget, u"journalCountBadge")) {
        roundedRect(painter, rect, Qt::transparent, color(named(widget, u"journalCountBadge") ? u"line" : u"lineSoft"), 4);
    } else if (named(widget, u"bodyViewMode")) {
        roundedRect(painter, rect, color(u"panel"), Qt::transparent, 5);
    } else if (named(widget, u"logToolbar") || named(widget, u"journalHeading") || named(widget, u"detailsHeading")) {
        painter->fillRect(rect, color(u"panel"));
        line(u"lineSoft");
    } else {
        painter->fillRect(rect, color(u"bg"));
        if (named(widget, u"journalToolbar")) line(u"lineSoft");
    }
}

void setTextColor(QPalette &palette, const QColor &foreground)
{
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Active, role, foreground);
        palette.setColor(QPalette::Inactive, role, foreground);
    }
}

} // namespace

AppStyle::AppStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
    setObjectName(QStringLiteral("MultiSenderNative"));
}

void AppStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (!widget || m_configured.contains(widget)) return;
    m_configured.insert(widget);
    connect(widget, &QObject::destroyed, this, [this, widget] {
        m_configured.remove(widget);
        m_paletteWidgets.remove(widget);
        m_surfaces.remove(widget);
    });
    connect(widget, &QObject::objectNameChanged, this, [this, widget] {
        // A few factory-created controls are named after their first polish.
        // Only that control is configured; palette changes never take this path.
        configureWidget(widget);
        if (m_paletteWidgets.contains(widget) || m_surfaces.contains(widget) || qobject_cast<QAbstractButton *>(widget))
            widget->installEventFilter(this);
        refreshPalette(widget);
        widget->updateGeometry();
        widget->update();
    });
    configureWidget(widget);
    if (m_paletteWidgets.contains(widget) || m_surfaces.contains(widget) || qobject_cast<QAbstractButton *>(widget))
        widget->installEventFilter(this);
    refreshPalette(widget);
}

void AppStyle::unpolish(QWidget *widget)
{
    QProxyStyle::unpolish(widget);
    // Keep the one-time configuration across Qt's native top-level ThemeChange
    // and callers temporarily changing an individual widget's style.
}

void AppStyle::configureWidget(QWidget *widget)
{
    const QString name = widget->objectName();
    QFont font = widget->font();
    int fontSize = -1;
    QFont::Weight weight = QFont::Normal;
    bool monospace = false;
    if (qobject_cast<QLabel *>(widget)) {
        m_paletteWidgets.insert(widget);
        if (name == u"appTitle" || name == u"sidebarTitle") { fontSize = 14; weight = QFont::DemiBold; }
        else if (name == u"brandMark") { fontSize = 19; weight = QFont::DemiBold; }
        else if (name == u"sectionTitle") { fontSize = 12; weight = QFont::DemiBold; }
        else if (name == u"editableTitleLabel") {
            fontSize = ancestorNamed(widget, u"collectionEditorCard") ? 23
                : ancestorNamed(widget, u"requestEditorCard") ? 14 : 16;
            weight = QFont::Medium;
        } else if (name == u"overviewTitle") { fontSize = 33; weight = QFont::Medium; }
        else if (name == u"overviewMetricValue") { fontSize = 23; weight = QFont::Medium; }
        else if (name == u"statValue") { fontSize = 16; weight = QFont::Medium; }
        else if (name == u"emptyTitle" || name == u"logEmptyTitle") { fontSize = 15; weight = QFont::Medium; }
        else if (name == u"journalPrompt") { fontSize = 13; weight = QFont::DemiBold; monospace = true; }
        else if (name == u"sidebarSection" || name == u"eyebrow" || name == u"dataDirectoryTitle") {
            fontSize = 10; weight = QFont::DemiBold;
        } else if (name == u"appEdition" || name == u"bodyByteCount" || name == u"journalCountBadge" || name == u"dataDirectoryHint") fontSize = 9;
        else if (name == u"workspaceCaption" || name == u"subtitleLabel" || name == u"emptyHint" || name == u"helperText") fontSize = 11;
        else if (name == u"fieldLabel" || name == u"statName" || name == u"detailsMetadata"
                 || name == u"connectionSummary" || name == u"statusPill" || name == u"validationLabel"
                 || name == u"overviewMetricLabel" || name == u"workspaceHomeCaption" || name == u"countBadge"
                 || name == u"statusText" || name == u"savedStatus" || name == u"keycap") fontSize = 10;
        for (const QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
            if (qobject_cast<const QStatusBar *>(parent)) { fontSize = 10; break; }
        if (name == u"urlSummary" || name == u"detailsMetadata") { monospace = true; fontSize = 10; }
        QMargins padding;
        if (name == u"appEdition") padding = {7, 3, 7, 3};
        else if (name == u"bodyByteCount") padding = {6, 3, 6, 3};
        else if (name == u"statusPill") padding = {9, 4, 9, 4};
        else if (name == u"countBadge") padding = {6, 1, 6, 1};
        else if (name == u"journalCountBadge") padding = {6, 1, 6, 1};
        else if (name == u"keycap") padding = {6, 3, 6, 3};
        else if (name == u"editableTitleLabel" || name == u"detailsMetadata") padding = {0, 2, 0, 2};
        if (!padding.isNull()) widget->setContentsMargins(padding);
    } else if (qobject_cast<QAbstractButton *>(widget)) {
        widget->setAttribute(Qt::WA_Hover);
        fontSize = 11;
        if (name == u"sendButton") { fontSize = 12; weight = QFont::DemiBold; }
        else if (name == u"workspaceHomeButton") { fontSize = 14; weight = QFont::DemiBold; }
        else if (name == u"compactButton" || name == u"transferButton" || name == u"languageButton" || name == u"breadcrumbButton") fontSize = 10;
        if (qobject_cast<QCheckBox *>(widget)) m_paletteWidgets.insert(widget);
    } else if (qobject_cast<QLineEdit *>(widget) || qobject_cast<QComboBox *>(widget) || qobject_cast<QAbstractSpinBox *>(widget)) {
        widget->setAttribute(Qt::WA_Hover);
        if (qobject_cast<QLineEdit *>(widget)) m_paletteWidgets.insert(widget);
        if (name == u"sidebarSearch") fontSize = 11;
        else if (name == u"bodySearch" || name == u"requestProtocol" || name == u"httpMethod") fontSize = 10;
        else if (name == u"editableTitleEdit") { fontSize = ancestorNamed(widget, u"requestEditorCard") ? 14 : 16; weight = QFont::Medium; }
        if (name == u"httpMethod" || name == u"requestProtocol") weight = QFont::Medium;
        if (name == u"httpMethod" || name == u"requestTarget" || name == u"transportAddress") monospace = true;
        if (qobject_cast<QComboBox *>(widget)) m_paletteWidgets.insert(widget);
        if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
            // The style's content rectangle supplies all padding, also for spin-box editors.
            if (qobject_cast<QAbstractSpinBox *>(edit->parentWidget()) || qobject_cast<QComboBox *>(edit->parentWidget()))
                edit->setFrame(false);
        }
    } else if (qobject_cast<QPlainTextEdit *>(widget) || qobject_cast<QTextEdit *>(widget)) {
        m_paletteWidgets.insert(widget);
        if (name == u"codeEditor" || name == u"detailsEditor") { fontSize = 11; monospace = true; }
        if (flatEditor(widget)) static_cast<QFrame *>(widget)->setFrameShape(QFrame::NoFrame);
    } else if (auto *view = qobject_cast<QAbstractItemView *>(widget)) {
        m_paletteWidgets.insert(widget);
        view->setMouseTracking(true);
        view->viewport()->setAttribute(Qt::WA_Hover);
        if (auto *header = qobject_cast<QHeaderView *>(view)) {
            fontSize = named(header->parentWidget(), u"logTable") ? 9 : 10;
            weight = QFont::Medium;
        } else if (qobject_cast<QTreeView *>(view) || name == u"logTable") fontSize = 11;
        if (!qobject_cast<QHeaderView *>(view))
            view->setFrameShape(name == u"collectionRequests" ? QFrame::StyledPanel : QFrame::NoFrame);
        if (popupAncestor(view)) view->setContentsMargins(4, 4, 4, 4);
        else if (qobject_cast<QTreeView *>(view) || name == u"collectionRequests") view->setContentsMargins(2, 2, 2, 2);
    } else if (auto *tabs = qobject_cast<QTabBar *>(widget)) {
        tabs->setAttribute(Qt::WA_Hover);
        fontSize = name == u"workspaceTabBar" || name == u"journalFilters" || name == u"bodyViewMode" || name == u"innerDetailsTabBar" ? 10 : 11;
    } else if (qobject_cast<QStatusBar *>(widget)) {
        fontSize = 10;
        m_paletteWidgets.insert(widget);
    } else if (auto *scroll = qobject_cast<QScrollArea *>(widget)) {
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        if (name == u"requestEditorScrollArea") m_paletteWidgets.insert(widget);
    } else if (qobject_cast<QMenu *>(widget)) {
        m_paletteWidgets.insert(widget);
        widget->setAttribute(Qt::WA_Hover);
    }
    if (fontSize > 0) {
        font.setPixelSize(fontSize);
        font.setWeight(weight);
    }
    if (monospace) { font.setFamily(QStringLiteral("Consolas")); font.setStyleHint(QFont::Monospace); font.setFixedPitch(true); }
    if (fontSize > 0 || monospace) widget->setFont(font);
    if (hasSurface(widget)) {
        m_surfaces.insert(widget);
        widget->setAutoFillBackground(false);
        if (auto *frame = qobject_cast<QFrame *>(widget)) frame->setFrameShape(QFrame::NoFrame);
    }
}

void AppStyle::refreshPalette(QWidget *widget)
{
    if (!widget || !m_paletteWidgets.contains(widget)) return;
    // Explicit brushes must start with the new application palette, never
    // with a widget palette carrying the previous theme's resolved colors.
    QPalette palette = QApplication::palette();
    const QString name = widget->objectName();
    if (qobject_cast<QLabel *>(widget)) {
        QStringView role = u"text";
        constexpr std::array secondaryNames{u"workspaceCaption", u"subtitleLabel", u"emptyHint", u"helperText",
            u"countBadge", u"journalCountBadge", u"savedStatus", u"journalPrompt", u"keycap", u"dataDirectoryTitle"};
        constexpr std::array mutedNames{u"appEdition", u"sidebarSection", u"fieldLabel", u"statName", u"bodyByteCount",
            u"urlSummary", u"detailsMetadata", u"connectionSummary", u"eyebrow", u"overviewMetricLabel",
            u"workspaceHomeCaption", u"statusText", u"dataDirectoryHint"};
        if (std::ranges::any_of(secondaryNames, [widget](auto value) { return named(widget, value); })) role = u"secondary";
        if (std::ranges::any_of(mutedNames, [widget](auto value) { return named(widget, value); })) role = u"muted";
        if (name == u"brandMark") role = u"accent";
        if (name == u"statusPill") {
            const QString state = widget->property("state").toString();
            role = state == u"error" ? u"red" : state == u"running" || state == u"success"
                || widget->property("mode").toString() == u"running" ? u"accent" : u"secondary";
        }
        if (name == u"validationLabel") {
            const QString state = widget->property("state").toString();
            role = state == u"valid" ? u"accent" : state == u"invalid" ? u"red" : u"text";
        }
        for (const QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
            if (qobject_cast<const QStatusBar *>(parent)) { role = name == u"savedStatus" ? u"secondary" : u"muted"; break; }
        setTextColor(palette, color(role));
    } else if (qobject_cast<QCheckBox *>(widget)) {
        setTextColor(palette, color(u"secondary"));
    } else if (qobject_cast<QLineEdit *>(widget)) {
        if (name == u"transportAddress") setTextColor(palette, color(u"secondary"));
        if (addressControl(widget) || headerEditor(widget)
            || qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) || qobject_cast<QComboBox *>(widget->parentWidget()))
            palette.setColor(QPalette::Base, Qt::transparent);
        palette.setColor(QPalette::Disabled, QPalette::Text, color(u"secondary"));
    } else if (qobject_cast<QComboBox *>(widget)) {
        if (name == u"requestProtocol") {
            const QString protocol = widget->property("protocol").toString();
            setTextColor(palette, color(protocol == u"TCP" ? u"blue" : protocol == u"UDP" ? u"purple" : protocol == u"COM" ? u"orange" : u"text"));
        } else if (name == u"httpMethod") {
            const QString method = widget->property("method").toString();
            setTextColor(palette, color(method == u"POST" || method == u"PUT" ? u"orange" : method == u"PATCH" ? u"blue" : method == u"DELETE" ? u"red" : u"accent"));
        }
    } else if (qobject_cast<QPlainTextEdit *>(widget) || qobject_cast<QTextEdit *>(widget)) {
        palette.setColor(QPalette::Base, color(flatEditor(widget) ? u"bg" : u"input"));
    } else if (qobject_cast<QHeaderView *>(widget)) {
        palette.setColor(QPalette::Base, color(u"bg"));
        palette.setColor(QPalette::Window, color(u"bg"));
        setTextColor(palette, color(u"muted"));
    } else if (qobject_cast<QAbstractItemView *>(widget)) {
        const bool tree = qobject_cast<QTreeView *>(widget);
        const bool popup = comboPopup(widget) || popupAncestor(widget);
        palette.setColor(QPalette::Base, color(tree ? u"sidebar" : popup ? u"panel" : u"bg"));
        palette.setColor(QPalette::Window, palette.color(QPalette::Base));
        palette.setColor(QPalette::Mid, color(u"lineSoft"));
        setTextColor(palette, color(tree || qobject_cast<QTableView *>(widget) ? u"secondary" : u"text"));
    } else if (qobject_cast<QStatusBar *>(widget)) {
        palette.setColor(QPalette::Window, color(u"sidebar"));
        setTextColor(palette, color(u"muted"));
    } else if (qobject_cast<QMenu *>(widget)) {
        palette.setColor(QPalette::Window, color(u"panel"));
        setTextColor(palette, color(u"secondary"));
    } else if (qobject_cast<QScrollArea *>(widget)) {
        palette.setColor(QPalette::Base, color(u"bg"));
        palette.setColor(QPalette::Window, color(u"bg"));
    }
    const bool wasRefreshing = m_refreshingPalette;
    m_refreshingPalette = true;
    if (widget->palette() != palette) widget->setPalette(palette);
    m_refreshingPalette = wasRefreshing;
}

void AppStyle::refreshTheme()
{
    // This set contains only controls that need specialized text/surface roles.
    // Painting the other controls reads semantic colors directly.
    for (QWidget *widget : std::as_const(m_paletteWidgets)) refreshPalette(widget);
}

bool AppStyle::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget) return false;
    if (event->type() == QEvent::Paint && m_surfaces.contains(widget)) {
        QPainter painter(widget);
        paintSurface(&painter, widget);
    } else if (event->type() == QEvent::PaletteChange && !m_refreshingPalette) {
        refreshPalette(widget);
    } else if (event->type() == QEvent::DynamicPropertyChange) {
        const auto *change = static_cast<QDynamicPropertyChangeEvent *>(event);
        const QByteArray &name = change->propertyName();
        if (name == "state" || name == "mode" || name == "method" || name == "protocol"
            || name == "running" || name == "focused") {
            refreshPalette(widget);
            widget->update();
        }
    } else if (event->type() == QEvent::EnabledChange || event->type() == QEvent::LayoutDirectionChange) {
        widget->update();
    }
    return false;
}

void AppStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                             QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case PE_PanelLineEdit: case PE_FrameLineEdit:
        inputFrame(painter, option, widget);
        return;
    case PE_Frame:
        if (qobject_cast<const QPlainTextEdit *>(widget) || qobject_cast<const QTextEdit *>(widget)) {
            inputFrame(painter, option, widget);
            return;
        }
        if (named(widget, u"collectionRequests")) { roundedRect(painter, option->rect, Qt::transparent, color(u"line"), 7); return; }
        if (named(widget, u"httpHeadersTable")) return;
        break;
    case PE_FrameFocusRect:
        if (option->state.testFlag(State_Enabled))
            roundedRect(painter, option->rect, Qt::transparent, color(u"accentLine"), 3, Qt::DotLine);
        return;
    case PE_PanelButtonCommand: case PE_PanelButtonTool: {
        const auto colors = buttonColors(widget, option->state);
        roundedRect(painter, option->rect, colors.background, colors.border, colors.radius);
        return;
    }
    case PE_IndicatorCheckBox: case PE_IndicatorItemViewItemCheck: {
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool checked = option->state.testFlag(State_On) || option->state.testFlag(State_NoChange);
        roundedRect(painter, option->rect, color(checked ? u"accent" : u"input"),
                    color(!enabled ? u"line" : checked || option->state.testFlag(State_MouseOver)
                          || option->state.testFlag(State_HasFocus) ? u"accent" : u"muted"), 3);
        if (checked) checkMark(painter, option->rect, color(u"onAccent"), option->state.testFlag(State_NoChange));
        return;
    }
    case PE_IndicatorRadioButton: {
        const bool checked = option->state.testFlag(State_On);
        roundedRect(painter, option->rect, color(u"input"), color(checked || option->state.testFlag(State_HasFocus) ? u"accent" : u"muted"), option->rect.width() / 2.0);
        if (checked) roundedRect(painter, option->rect.adjusted(4, 4, -4, -4), color(u"accent"), Qt::transparent, option->rect.width() / 2.0);
        return;
    }
    case PE_IndicatorArrowDown: case PE_IndicatorArrowUp: case PE_IndicatorArrowLeft: case PE_IndicatorArrowRight:
        arrow(painter, option->rect, element == PE_IndicatorArrowUp ? Qt::UpArrow
              : element == PE_IndicatorArrowLeft ? Qt::LeftArrow
              : element == PE_IndicatorArrowRight ? Qt::RightArrow : Qt::DownArrow,
              color(option->state.testFlag(State_Enabled) ? u"secondary" : u"muted"));
        return;
    case PE_IndicatorBranch:
        if (option->state.testFlag(State_Children))
            arrow(painter, option->rect.adjusted(3, 3, -3, -3), option->state.testFlag(State_Open) ? Qt::DownArrow
                  : option->direction == Qt::RightToLeft ? Qt::LeftArrow : Qt::RightArrow, color(u"muted"));
        return;
    case PE_IndicatorTabClose:
        AppTheme::icon(u"close").paint(painter, option->rect.adjusted(2, 2, -2, -2));
        return;
    case PE_PanelItemViewItem: {
        const bool tree = qobject_cast<const QTreeView *>(widget);
        const bool popup = comboPopup(widget) || popupAncestor(widget);
        const bool selected = option->state.testFlag(State_Selected);
        const bool hover = option->state.testFlag(State_MouseOver);
        if (tree) {
            if (selected || hover) roundedRect(painter, option->rect, color(selected ? u"accentSoft" : u"raised"),
                                               selected ? color(u"accentLine") : QColor(Qt::transparent), 5);
        } else {
            if (selected || hover) painter->fillRect(option->rect, color(selected ? u"accentSoft"
                : named(widget, u"collectionRequests") || popup ? u"panel" : u"raised"));
            else if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option)) {
                if (item->backgroundBrush.style() != Qt::NoBrush) painter->fillRect(option->rect, item->backgroundBrush);
                else if (item->features.testFlag(QStyleOptionViewItem::Alternate)) painter->fillRect(option->rect, color(u"panel"));
            }
            if (qobject_cast<const QTableView *>(widget) && !named(widget, u"httpHeadersTable") && !named(widget, u"logTable")) {
                painter->save();
                painter->setPen(color(u"lineSoft"));
                painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
                painter->restore();
            }
        }
        return;
    }
    case PE_FrameTabWidget: {
        painter->fillRect(option->rect, color(u"bg"));
        if (named(widget, u"detailsTabs") || named(widget, u"innerDetailsTabs") || named(widget, u"editorTabs")) {
            painter->save();
            painter->setPen(color(named(widget, u"innerDetailsTabs") ? u"lineSoft" : u"line"));
            painter->drawLine(option->rect.topLeft(), option->rect.topRight());
            painter->restore();
        }
        return;
    }
    case PE_FrameTabBarBase: case PE_FrameStatusBarItem:
        return;
    case PE_PanelStatusBar:
        painter->fillRect(option->rect, color(u"sidebar"));
        painter->save();
        painter->setPen(color(u"lineSoft"));
        painter->drawLine(option->rect.topLeft(), option->rect.topRight());
        painter->restore();
        return;
    case PE_PanelMenu:
        roundedRect(painter, option->rect, color(u"panel"), color(u"line"), 6);
        return;
    case PE_FrameMenu:
        return;
    case PE_PanelTipLabel:
        roundedRect(painter, option->rect, color(u"raised"), color(u"line"), 4);
        return;
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void AppStyle::drawControl(ControlElement element, const QStyleOption *option,
                           QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case CE_PushButton: {
        drawPrimitive(PE_PanelButtonCommand, option, painter, widget);
        drawControl(CE_PushButtonLabel, option, painter, widget);
        return;
    }
    case CE_PushButtonBevel:
        drawPrimitive(PE_PanelButtonCommand, option, painter, widget);
        return;
    case CE_PushButtonLabel: {
        const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
        if (!button) break;
        const auto colors = buttonColors(widget, option->state);
        QRect contents = subElementRect(SE_PushButtonContents, option, widget);
        if (option->state.testFlag(State_Sunken)) contents.translate(0, 1);
        const bool hasMenu = button->features.testFlag(QStyleOptionButton::HasMenu);
        if (hasMenu) {
            const QRect menu = visualRect(option->direction, option->rect,
                QRect(option->rect.right() - 15, option->rect.center().y() - 5, 10, 10));
            arrow(painter, menu, Qt::DownArrow, colors.foreground);
        }
        const int iconWidth = button->icon.isNull() ? 0 : button->iconSize.width();
        const int gap = iconWidth && !button->text.isEmpty() ? 6 : 0;
        const int textWidth = button->fontMetrics.horizontalAdvance(button->text);
        const int totalWidth = std::min(contents.width(), iconWidth + gap + textWidth);
        const bool alignStart = named(widget, u"workspaceHomeButton") || named(widget, u"dataDirectoryButton");
        QRect block(contents.left() + (alignStart ? 0 : (contents.width() - totalWidth) / 2), contents.top(), totalWidth, contents.height());
        if (iconWidth) {
            const QRect iconRect = visualRect(option->direction, contents,
                QRect(block.left(), block.center().y() - button->iconSize.height() / 2, iconWidth, button->iconSize.height()));
            button->icon.paint(painter, iconRect, Qt::AlignCenter,
                option->state.testFlag(State_Enabled) ? QIcon::Normal : QIcon::Disabled,
                option->state.testFlag(State_On) ? QIcon::On : QIcon::Off);
        }
        QRect textRect(block.left() + iconWidth + gap, block.top(), std::max(0, block.width() - iconWidth - gap), block.height());
        textRect = visualRect(option->direction, contents, textRect);
        QPalette palette = button->palette;
        palette.setColor(QPalette::ButtonText, colors.foreground);
        drawItemText(painter, textRect, Qt::AlignVCenter | Qt::AlignHCenter | Qt::TextShowMnemonic,
                     palette, option->state.testFlag(State_Enabled), button->text, QPalette::ButtonText);
        return;
    }
    case CE_ToolButtonLabel: {
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (!tool) break;
        auto label = *tool;
        setTextColor(label.palette, buttonColors(widget, option->state).foreground);
        label.state &= ~State_HasFocus;
        QProxyStyle::drawControl(element, &label, painter, widget);
        return;
    }
    case CE_ComboBoxLabel: {
        const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
        if (!combo) break;
        if (combo->editable) return;
        QRect contents = subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
        painter->save();
        painter->setClipRect(contents);
        if (!combo->currentIcon.isNull()) {
            const QRect iconRect = visualRect(combo->direction, contents, QRect(contents.left(), contents.center().y() - combo->iconSize.height() / 2,
                                                                              combo->iconSize.width(), combo->iconSize.height()));
            combo->currentIcon.paint(painter, iconRect, Qt::AlignCenter, combo->state.testFlag(State_Enabled) ? QIcon::Normal : QIcon::Disabled);
            if (combo->direction == Qt::RightToLeft) contents.adjust(0, 0, -combo->iconSize.width() - 6, 0);
            else contents.adjust(combo->iconSize.width() + 6, 0, 0, 0);
        }
        drawItemText(painter, contents, Qt::AlignVCenter | Qt::AlignAbsolute
                     | (combo->direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft), combo->palette,
                     combo->state.testFlag(State_Enabled), combo->fontMetrics.elidedText(combo->currentText, Qt::ElideRight, contents.width()), QPalette::Text);
        painter->restore();
        return;
    }
    case CE_Header: {
        const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
        if (!header) break;
        drawControl(CE_HeaderSection, option, painter, widget);
        QRect labelRect = subElementRect(SE_HeaderLabel, option, widget);
        if (header->sortIndicator != QStyleOptionHeader::None) {
            const int margin = named(widget, u"httpHeadersHeader") ? 10 : 8;
            const QRect arrowRect = visualRect(option->direction, option->rect,
                QRect(option->rect.right() - margin - 10, option->rect.center().y() - 5, 10, 10));
            arrow(painter, arrowRect, header->sortIndicator == QStyleOptionHeader::SortUp ? Qt::UpArrow : Qt::DownArrow, color(u"muted"));
            if (option->direction == Qt::RightToLeft) labelRect.adjust(16, 0, 0, 0);
            else labelRect.adjust(0, 0, -16, 0);
        }
        if (const auto *headerV2 = qstyleoption_cast<const QStyleOptionHeaderV2 *>(option)) {
            auto label = *headerV2;
            label.rect = labelRect;
            drawControl(CE_HeaderLabel, &label, painter, widget);
        } else {
            auto label = *header;
            label.rect = labelRect;
            drawControl(CE_HeaderLabel, &label, painter, widget);
        }
        return;
    }
    case CE_HeaderSection:
        painter->fillRect(option->rect, color(u"bg"));
        if (!named(widget, u"httpHeadersHeader")) {
            painter->save();
            painter->setPen(color(u"lineSoft"));
            painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
            painter->restore();
        }
        return;
    case CE_HeaderEmptyArea:
        painter->fillRect(option->rect, color(u"bg"));
        return;
    case CE_HeaderLabel: {
        const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
        if (!header) break;
        if (const auto *headerV2 = qstyleoption_cast<const QStyleOptionHeaderV2 *>(option)) {
            auto label = *headerV2;
            setTextColor(label.palette, color(u"muted"));
            QProxyStyle::drawControl(element, &label, painter, widget);
            return;
        }
        auto label = *header;
        setTextColor(label.palette, color(u"muted"));
        QProxyStyle::drawControl(element, &label, painter, widget);
        return;
    }
    case CE_TabBarTab: {
        drawControl(CE_TabBarTabShape, option, painter, widget);
        drawControl(CE_TabBarTabLabel, option, painter, widget);
        return;
    }
    case CE_TabBarTabShape: {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (!tab) break;
        const bool selected = tab->state.testFlag(State_Selected);
        const bool hover = tab->state.testFlag(State_MouseOver);
        const bool workspace = named(widget, u"workspaceTabBar");
        if (named(widget, u"journalFilters") || named(widget, u"bodyViewMode")) {
            roundedRect(painter, tab->rect.adjusted(named(widget, u"bodyViewMode") ? 2 : 1, 2,
                        named(widget, u"bodyViewMode") ? -2 : -1, -2),
                selected ? color(u"raised") : hover ? color(named(widget, u"journalFilters") ? u"panel" : u"raised") : QColor(Qt::transparent),
                selected && named(widget, u"journalFilters") ? color(u"line") : QColor(Qt::transparent), named(widget, u"journalFilters") ? 4 : 3);
        } else {
            if (workspace || hover) painter->fillRect(tab->rect, color(workspace ? selected ? u"bg" : hover ? u"panel" : u"sidebar" : u"raised"));
            painter->save();
            if (workspace) {
                painter->setPen(color(u"lineSoft"));
                const int x = tab->direction == Qt::RightToLeft ? tab->rect.left() : tab->rect.right();
                painter->drawLine(x, tab->rect.top(), x, tab->rect.bottom());
            }
            if (selected || workspace) {
                painter->setPen(QPen(color(selected ? u"accent" : u"lineSoft"), 2));
                painter->drawLine(tab->rect.left(), tab->rect.bottom() - 1, tab->rect.right(), tab->rect.bottom() - 1);
            }
            painter->restore();
        }
        if (tab->state.testFlag(State_HasFocus) && tab->state.testFlag(State_Enabled))
            roundedRect(painter, tab->rect.adjusted(3, 3, -3, -3), Qt::transparent, color(u"accentLine"), 3, Qt::DotLine);
        return;
    }
    case CE_TabBarTabLabel: {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (!tab) break;
        auto label = *tab;
        const bool selected = tab->state.testFlag(State_Selected);
        const bool hover = tab->state.testFlag(State_MouseOver);
        const bool segmented = named(widget, u"journalFilters") || named(widget, u"bodyViewMode");
        setTextColor(label.palette, color(selected || hover ? u"text" : segmented ? u"muted" : u"secondary"));
        label.state &= ~State_HasFocus;
        QProxyStyle::drawControl(element, &label, painter, widget);
        return;
    }
    case CE_MenuItem: {
        const auto *menu = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!menu) break;
        if (menu->menuItemType == QStyleOptionMenuItem::Separator) {
            painter->save();
            painter->setPen(color(u"line"));
            painter->drawLine(menu->rect.left() + 5, menu->rect.center().y(), menu->rect.right() - 5, menu->rect.center().y());
            painter->restore();
            return;
        }
        const bool enabled = menu->state.testFlag(State_Enabled);
        const bool selected = enabled && menu->state.testFlag(State_Selected);
        if (selected) roundedRect(painter, menu->rect, color(u"raised"), Qt::transparent, 4);
        const QColor foreground = color(!enabled ? u"muted" : selected ? u"text" : u"secondary");
        const int leading = menu->maxIconWidth > 0 || menu->menuHasCheckableItems ? std::max(16, menu->maxIconWidth) + 8 : 0;
        QRect textRect = menu->rect.adjusted(11 + leading, 0, -22, 0);
        textRect = visualRect(menu->direction, menu->rect, textRect);
        const QRect iconRect = visualRect(menu->direction, menu->rect, QRect(menu->rect.left() + 10, menu->rect.center().y() - 8, 16, 16));
        if (!menu->icon.isNull()) menu->icon.paint(painter, iconRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled, menu->checked ? QIcon::On : QIcon::Off);
        else if (menu->checked) {
            if (menu->checkType == QStyleOptionMenuItem::Exclusive)
                roundedRect(painter, iconRect.adjusted(5, 5, -5, -5), foreground, Qt::transparent, 3);
            else checkMark(painter, iconRect, foreground);
        }
        painter->save();
        painter->setPen(foreground);
        if (menu->menuItemType == QStyleOptionMenuItem::DefaultItem) { QFont font = menu->font; font.setBold(true); painter->setFont(font); }
        else painter->setFont(menu->font);
        const int tabIndex = menu->text.indexOf(u'\t');
        const QString caption = menu->text.left(tabIndex < 0 ? menu->text.size() : tabIndex);
        const int startAlignment = Qt::AlignVCenter | Qt::TextShowMnemonic | Qt::AlignAbsolute | (menu->direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft);
        painter->drawText(textRect, startAlignment, caption);
        if (tabIndex >= 0) painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignAbsolute | (menu->direction == Qt::RightToLeft ? Qt::AlignLeft : Qt::AlignRight), menu->text.mid(tabIndex + 1));
        if (menu->menuItemType == QStyleOptionMenuItem::SubMenu) {
            const QRect arrowRect = visualRect(menu->direction, menu->rect, QRect(menu->rect.right() - 15, menu->rect.center().y() - 5, 10, 10));
            arrow(painter, arrowRect, menu->direction == Qt::RightToLeft ? Qt::LeftArrow : Qt::RightArrow, foreground);
        }
        painter->restore();
        return;
    }
    case CE_Splitter:
        if (option->state.testFlag(State_MouseOver)) painter->fillRect(option->rect, color(u"accentLine"));
        return;
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void AppStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                  QPainter *painter, const QWidget *widget) const
{
    if (control == CC_ComboBox) {
        const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
        if (!combo) return;
        inputFrame(painter, option, widget);
        arrow(painter, subControlRect(control, option, SC_ComboBoxArrow, widget), Qt::DownArrow,
              color(combo->state.testFlag(State_Enabled) ? u"secondary" : u"muted"));
        return;
    }
    if (control == CC_SpinBox) {
        const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
        if (!spin) return;
        inputFrame(painter, option, widget);
        if (spin->buttonSymbols == QAbstractSpinBox::NoButtons) return;
        for (const auto sub : {SC_SpinBoxUp, SC_SpinBoxDown}) {
            const QRect rect = subControlRect(control, option, sub, widget);
            const bool stepEnabled = spin->state.testFlag(State_Enabled)
                && spin->stepEnabled.testFlag(sub == SC_SpinBoxUp ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled);
            const bool active = stepEnabled && spin->activeSubControls.testFlag(sub);
            if (active && spin->state.testFlag(State_MouseOver)) roundedRect(painter, rect,
                color(spin->state.testFlag(State_Sunken) ? u"accentSoft" : u"raised"), Qt::transparent, 4);
            if (stepEnabled) arrow(painter, rect, sub == SC_SpinBoxUp ? Qt::UpArrow : Qt::DownArrow, color(u"secondary"));
        }
        return;
    }
    if (control == CC_ScrollBar) {
        const auto *scroll = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!scroll) return;
        QRect handle = subControlRect(control, option, SC_ScrollBarSlider, widget);
        if (scroll->orientation == Qt::Vertical) handle.adjust(2, 0, -2, 0);
        else handle.adjust(0, 2, 0, -2);
        const bool hover = scroll->state.testFlag(State_MouseOver) && scroll->activeSubControls.testFlag(SC_ScrollBarSlider);
        roundedRect(painter, handle, color(hover ? u"muted" : u"line"), Qt::transparent, 3);
        return;
    }
    if (control == CC_ToolButton) {
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (!tool) return;
        drawPrimitive(PE_PanelButtonTool, option, painter, widget);
        auto label = *tool;
        label.rect = subControlRect(control, option, SC_ToolButton, widget);
        const auto padding = visualMargins(option->direction, buttonPadding(widget));
        label.rect = label.rect.marginsRemoved(padding);
        if (tool->state.testFlag(State_Sunken)) label.rect.translate(0, 1);
        drawControl(CE_ToolButtonLabel, &label, painter, widget);
        if (tool->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) || tool->features.testFlag(QStyleOptionToolButton::HasMenu)) {
            QRect menuRect = subControlRect(control, option, SC_ToolButtonMenu, widget);
            if (menuRect.isEmpty()) menuRect = visualRect(option->direction, option->rect, QRect(option->rect.right() - 13, option->rect.center().y() - 5, 10, 10));
            arrow(painter, menuRect, Qt::DownArrow, buttonColors(widget, option->state).foreground);
        }
        return;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

int AppStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_DefaultFrameWidth:
        if (named(widget, u"collectionRequests")) return 1;
        return addressControl(widget) || headerEditor(widget) || flatEditor(widget)
            || qobject_cast<const QScrollArea *>(widget) || qobject_cast<const QAbstractItemView *>(widget) ? 0 : 1;
    case PM_ButtonMargin: return 0;
    case PM_ButtonDefaultIndicator: return 0;
    case PM_ButtonShiftHorizontal: return 0;
    case PM_ButtonShiftVertical: return 0;
    case PM_MenuButtonIndicator: return 14;
    case PM_SmallIconSize: case PM_ButtonIconSize: return 16;
    case PM_IndicatorWidth: case PM_IndicatorHeight: case PM_ExclusiveIndicatorWidth: case PM_ExclusiveIndicatorHeight: return 16;
    case PM_CheckBoxLabelSpacing: case PM_RadioButtonLabelSpacing: return 7;
    case PM_ScrollBarExtent: return 10;
    case PM_ScrollBarSliderMin: return 28;
    case PM_TabBarTabHSpace: return named(widget, u"journalFilters") ? 14 : named(widget, u"bodyViewMode") ? 22
        : named(widget, u"editorTabs") ? 30 : named(widget, u"detailsTabBar") || named(widget, u"innerDetailsTabBar") || named(widget, u"workspaceTabBar") ? 24 : 26;
    case PM_TabBarTabVSpace: return named(widget, u"bodyViewMode") ? 12 : named(widget, u"journalFilters") ? 14
        : named(widget, u"innerDetailsTabBar") ? 14 : named(widget, u"detailsTabBar") || named(widget, u"editorTabs") ? 16 : named(widget, u"workspaceTabBar") ? 20 : 18;
    case PM_TabBarTabShiftHorizontal: case PM_TabBarTabShiftVertical: case PM_TabBarBaseHeight: case PM_TabBarBaseOverlap: return 0;
    case PM_TabBarScrollButtonWidth: return 20;
    case PM_TabCloseIndicatorWidth: case PM_TabCloseIndicatorHeight: return 16;
    case PM_HeaderMargin: return named(widget, u"httpHeadersHeader") ? 10 : 8;
    case PM_MenuPanelWidth: return 1;
    case PM_MenuHMargin: case PM_MenuVMargin: return 5;
    case PM_MenuTearoffHeight: return 8;
    case PM_ToolTipLabelFrameWidth: return 7;
    case PM_SpinBoxFrameWidth: return 1;
    default: return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int AppStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_Widget_Animate: return 0;
    case SH_ComboBox_Popup: return 0;
    case SH_ItemView_ShowDecorationSelected: return 1;
    case SH_ItemView_ActivateItemOnSingleClick: return 0;
    case SH_ScrollBar_Transient: return 0;
    case SH_Menu_AllowActiveAndDisabled: return 0;
    case SH_Table_GridLineColor: return color(u"lineSoft").rgb();
    default: return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QSize AppStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                const QSize &contentsSize, const QWidget *widget) const
{
    if (type == CT_PushButton || type == CT_ToolButton) {
        const QMargins padding = buttonPadding(widget);
        const int minimum = named(widget, u"compactButton") || named(widget, u"languageButton") ? 17
            : named(widget, u"transferButton") ? 22 : 18;
        QSize size(contentsSize.width() + padding.left() + padding.right() + 2,
                   std::max(contentsSize.height(), minimum) + padding.top() + padding.bottom() + 2);
        if (const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option))
            if (button->features.testFlag(QStyleOptionButton::HasMenu)) size.rwidth() += 14;
        if (const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option))
            if (tool->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) || tool->features.testFlag(QStyleOptionToolButton::HasMenu)) size.rwidth() += 14;
        return size;
    }
    if (type == CT_LineEdit || type == CT_ComboBox || type == CT_SpinBox) {
        const QMargins padding = inputPadding(widget);
        const int frame = addressControl(widget) || headerEditor(widget) ? 0 : 2;
        const int minimum = addressControl(widget) ? 22 : named(widget, u"bodySearch") ? 15 : 18;
        int extra = type == CT_ComboBox ? 22 : type == CT_SpinBox ? 22 : 0;
        if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option))
            if (spin->buttonSymbols == QAbstractSpinBox::NoButtons) extra = 0;
        return {contentsSize.width() + padding.left() + padding.right() + extra + frame,
                std::max(contentsSize.height(), minimum) + padding.top() + padding.bottom() + frame};
    }
    if (type == CT_ItemViewItem) {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        if (qobject_cast<const QTreeView *>(widget)) size.setHeight(std::max(size.height(), 36));
        else if (named(widget, u"collectionRequests")) { size.rwidth() += 20; size.setHeight(std::max(size.height() + 10, 40)); }
        else if (comboPopup(widget) || popupAncestor(widget)) { size.rwidth() += 14; size.setHeight(std::max(size.height() + 4, 29)); }
        else if (qobject_cast<const QTableView *>(widget) && !named(widget, u"httpHeadersTable")) size += QSize(16, 12);
        return size;
    }
    if (type == CT_HeaderSection) {
        QSize size = contentsSize + QSize(16, named(widget, u"httpHeadersHeader") ? 14 : 15);
        return size;
    }
    if (type == CT_MenuItem) {
        const auto *menu = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (menu && menu->menuItemType == QStyleOptionMenuItem::Separator) return {contentsSize.width(), 11};
        const int leading = menu && (menu->maxIconWidth > 0 || menu->menuHasCheckableItems) ? std::max(16, menu->maxIconWidth) + 8 : 0;
        return {contentsSize.width() + 33 + leading, std::max(contentsSize.height(), 16) + 14};
    }
    if (type == CT_TabBarTab) {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        if (named(widget, u"workspaceTabBar")) size.setWidth(std::max(size.width(), 124));
        return size;
    }
    return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
}

QRect AppStyle::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    if (element == SE_PushButtonContents) {
        const QMargins padding = visualMargins(option->direction, buttonPadding(widget));
        return option->rect.adjusted(1, 1, -1, -1).marginsRemoved(padding);
    }
    if (element == SE_LineEditContents) {
        if (auto *line = qobject_cast<const QLineEdit *>(widget)) {
            if (!line->hasFrame() && (qobject_cast<const QAbstractSpinBox *>(line->parentWidget()) || qobject_cast<const QComboBox *>(line->parentWidget()))) return option->rect;
        }
        const int frame = addressControl(widget) || headerEditor(widget) ? 0 : 1;
        return option->rect.adjusted(frame, frame, -frame, -frame).marginsRemoved(inputPadding(widget));
    }
    if (element == SE_HeaderLabel) {
        const int margin = named(widget, u"httpHeadersHeader") ? 10 : 8;
        return option->rect.adjusted(margin, 0, -margin, 0);
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

QRect AppStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                              SubControl subControl, const QWidget *widget) const
{
    const QRect rect = option->rect;
    if (control == CC_ComboBox) {
        const int frame = addressControl(widget) ? 0 : 1;
        const QRect contents = rect.adjusted(frame, frame, -frame, -frame);
        const int arrowWidth = addressControl(widget) ? 20 : 22;
        if (subControl == SC_ComboBoxArrow) return visualRect(option->direction, rect,
            QRect(contents.right() - arrowWidth + 1, contents.top(), arrowWidth, contents.height()));
        if (subControl == SC_ComboBoxEditField) {
            QRect edit = contents.marginsRemoved(inputPadding(widget));
            if (option->direction == Qt::RightToLeft) edit.adjust(arrowWidth, 0, 0, 0);
            else edit.adjust(0, 0, -arrowWidth, 0);
            return edit;
        }
        if (subControl == SC_ComboBoxFrame || subControl == SC_ComboBoxListBoxPopup) return rect;
    }
    if (control == CC_SpinBox) {
        const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
        const int buttonWidth = spin && spin->buttonSymbols == QAbstractSpinBox::NoButtons ? 0 : 21;
        const QRect contents = rect.adjusted(1, 1, -1, -1);
        if (subControl == SC_SpinBoxUp || subControl == SC_SpinBoxDown) {
            if (!buttonWidth) return {};
            QRect button(contents.right() - buttonWidth + 1, contents.top(), buttonWidth, contents.height() / 2);
            if (subControl == SC_SpinBoxDown) { button.setTop(contents.top() + contents.height() / 2); button.setBottom(contents.bottom()); }
            return visualRect(option->direction, rect, button);
        }
        if (subControl == SC_SpinBoxEditField) {
            QRect edit = contents.marginsRemoved(inputPadding(widget));
            if (option->direction == Qt::RightToLeft) edit.adjust(buttonWidth, 0, 0, 0);
            else edit.adjust(0, 0, -buttonWidth, 0);
            return edit;
        }
        if (subControl == SC_SpinBoxFrame) return rect;
    }
    if (control == CC_ToolButton) {
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        const bool menu = tool && (tool->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) || tool->features.testFlag(QStyleOptionToolButton::HasMenu));
        const int menuWidth = menu ? 14 : 0;
        if (subControl == SC_ToolButtonMenu) return menu ? visualRect(option->direction, rect,
            QRect(rect.right() - menuWidth + 1, rect.top(), menuWidth, rect.height())) : QRect{};
        if (subControl == SC_ToolButton) {
            if (option->direction == Qt::RightToLeft) return rect.adjusted(menuWidth, 0, 0, 0);
            return rect.adjusted(0, 0, -menuWidth, 0);
        }
    }
    if (control == CC_ScrollBar) {
        const auto *scroll = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!scroll) return {};
        const bool vertical = scroll->orientation == Qt::Vertical;
        const QRect groove = vertical ? rect.adjusted(0, 2, 0, -2) : rect.adjusted(2, 0, -2, 0);
        const int length = std::max(0, vertical ? groove.height() : groove.width());
        const qint64 range = qint64(scroll->maximum) - scroll->minimum;
        const qint64 page = std::max(0, scroll->pageStep);
        const int handleLength = range <= 0 ? length : int(std::clamp<qint64>(page * length / (range + page), std::min(28, length), length));
        const int offset = range <= 0 ? 0 : sliderPositionFromValue(scroll->minimum, scroll->maximum, scroll->sliderPosition,
                                                                   length - handleLength, scroll->upsideDown);
        const QRect slider = vertical ? QRect(groove.left(), groove.top() + offset, groove.width(), handleLength)
                                      : QRect(groove.left() + offset, groove.top(), handleLength, groove.height());
        if (subControl == SC_ScrollBarGroove) return groove;
        if (subControl == SC_ScrollBarSlider) return visualRect(option->direction, rect, slider);
        if (subControl == SC_ScrollBarSubLine || subControl == SC_ScrollBarAddLine) return {};
        const QRect before = vertical ? QRect(groove.left(), groove.top(), groove.width(), offset)
                                      : QRect(groove.left(), groove.top(), offset, groove.height());
        const QRect after = vertical ? QRect(groove.left(), slider.bottom() + 1, groove.width(), length - offset - handleLength)
                                     : QRect(slider.right() + 1, groove.top(), length - offset - handleLength, groove.height());
        if (subControl == SC_ScrollBarSubPage) return visualRect(option->direction, rect, scroll->upsideDown ? after : before);
        if (subControl == SC_ScrollBarAddPage) return visualRect(option->direction, rect, scroll->upsideDown ? before : after);
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QStyle::SubControl AppStyle::hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                                  const QPoint &position, const QWidget *widget) const
{
    if (control == CC_ScrollBar) {
        for (const auto sub : {SC_ScrollBarSlider, SC_ScrollBarSubPage, SC_ScrollBarAddPage})
            if (subControlRect(control, option, sub, widget).contains(position)) return sub;
        return SC_None;
    }
    if (control == CC_ComboBox) {
        if (subControlRect(control, option, SC_ComboBoxArrow, widget).contains(position)) return SC_ComboBoxArrow;
        return option->rect.contains(position) ? SC_ComboBoxEditField : SC_None;
    }
    if (control == CC_SpinBox) {
        for (const auto sub : {SC_SpinBoxUp, SC_SpinBoxDown, SC_SpinBoxEditField})
            if (subControlRect(control, option, sub, widget).contains(position)) return sub;
        return SC_None;
    }
    if (control == CC_ToolButton) {
        if (subControlRect(control, option, SC_ToolButtonMenu, widget).contains(position)) return SC_ToolButtonMenu;
        return option->rect.contains(position) ? SC_ToolButton : SC_None;
    }
    return QProxyStyle::hitTestComplexControl(control, option, position, widget);
}
