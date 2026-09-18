#include "mainwindow.h"
#include "apptheme.h"
#include "codeeditor.h"
#include "logpane.h"
#include "httpheaderseditor.h"
#include "workspaceexchange.h"
#include "collectionrequestdelegate.h"
#include "uilanguage.h"

#include "payloadhighlighter.h"
#include "requestcontroller.h"
#include "requestfilterproxymodel.h"
#include "requestlogmodel.h"

#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QFrame>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTabBar>
#include <QTableView>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace {

class ThemeIcon final : public QWidget
{
public:
    ThemeIcon(QString name, QWidget *parent) : QWidget(parent), m_name(std::move(name))
    {
        setFixedSize(16, 16);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        AppTheme::icon(m_name).paint(&painter, contentsRect());
    }
private:
    QString m_name;
};

class OperationBadge final : public QWidget
{
public:
    explicit OperationBadge(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setOperation(const QString &operation)
    {
        m_operation = operation;
        QFont badgeFont = font();
        badgeFont.setPixelSize(8);
        badgeFont.setWeight(QFont::DemiBold);
        setFont(badgeFont);
        setFixedSize(qMax(31, QFontMetrics(badgeFont).horizontalAdvance(operation)) + 12, 17);
        setAccessibleName(operation);
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor foreground = AppTheme::operationColor(m_operation);
        QColor background = foreground;
        background.setAlpha(AppTheme::isDark() ? 28 : 22);
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        const QRect badgeRect = isRightToLeft() ? rect().adjusted(0, 0, -4, 0) : rect().adjusted(4, 0, 0, 0);
        painter.drawRoundedRect(badgeRect, 3, 3);
        painter.setPen(foreground);
        QFont badgeFont = font();
        badgeFont.setPixelSize(8);
        badgeFont.setWeight(QFont::DemiBold);
        painter.setFont(badgeFont);
        painter.drawText(badgeRect, Qt::AlignCenter, m_operation);
    }
private:
    QString m_operation;
};

void requestTabLabel(QTabWidget *tabs, int index, const QString &operation, const QString &name)
{
    auto *badge = static_cast<OperationBadge *>(tabs->tabBar()->tabButton(index, QTabBar::LeftSide));
    if (!badge) {
        badge = new OperationBadge(tabs->tabBar());
        tabs->tabBar()->setTabButton(index, QTabBar::LeftSide, badge);
    }
    badge->setOperation(operation);
    tabs->setTabText(index, name);
    tabs->setTabToolTip(index, operation + QStringLiteral(" · ") + name);
}

constexpr int TypeRole = Qt::UserRole;
constexpr int IdRole = Qt::UserRole + 1;
constexpr int SplitterHandleWidth = 7;

class CollectionTreeWidget final : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

    QRect visualRect(const QModelIndex &index) const override
    {
        QRect rect = QTreeWidget::visualRect(index);
        if (rect.isValid() && index.column() == 0 && index.parent().isValid()) {
            // Requests need a small nesting offset, not an unused disclosure slot.
            constexpr int RequestIndentation = 8;
            const int reclaimed = std::max(0, indentation() - RequestIndentation);
            if (isRightToLeft()) rect.adjust(0, 0, reclaimed, 0);
            else rect.adjust(-reclaimed, 0, 0, 0);
        }
        return rect;
    }
};

class RequestNameDelegate final : public QStyledItemDelegate
{
public:
    explicit RequestNameDelegate(QTreeWidget *tree)
        : QStyledItemDelegate(tree), m_tree(tree)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(compactOption(option, index));
        if (!index.parent().isValid()) {
            initStyleOption(&item, index);
            const QString count = QString::number(index.model()->rowCount(index));
            QFont countFont = item.font;
            countFont.setPixelSize(10);
            countFont.setWeight(QFont::Normal);
            const int countWidth = QFontMetrics(countFont).horizontalAdvance(count) + 16;
            const QRect countRect = QStyle::visualRect(item.direction, item.rect,
                QRect(item.rect.right() - countWidth + 1, item.rect.top(), countWidth, item.rect.height()));
            item.font.setWeight(QFont::DemiBold);
            item.fontMetrics = QFontMetrics(item.font);
            if (m_tree->isRightToLeft()) item.rect.adjust(countWidth, 0, 0, 0);
            else item.rect.adjust(0, 0, -countWidth, 0);
            const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
            painter->save();
            painter->setFont(countFont);
            painter->setPen(AppTheme::color(u"muted"));
            painter->drawText(countRect, Qt::AlignCenter, count);
            painter->restore();
            return;
        }
        initStyleOption(&item, index);
        const QString name = item.text;
        const QString operation = index.siblingAtColumn(1).data().toString();
        item.text.clear();
        const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);

        const QColor foreground = AppTheme::operationColor(operation);
        QColor fill = foreground;
        fill.setAlpha(AppTheme::isDark() ? 32 : 24);
        QRect badge = item.rect.adjusted(6, 0, -6, 0);
        badge.setTop(item.rect.center().y() - 8);
        badge.setHeight(17);
        const int width = badgeWidth(item);
        if (m_tree->isRightToLeft()) badge.setLeft(badge.right() - width + 1);
        else badge.setWidth(width);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(badge, 4, 4);
        QFont badgeFont = item.font;
        badgeFont.setPixelSize(8);
        badgeFont.setWeight(QFont::DemiBold);
        painter->setFont(badgeFont);
        painter->setPen(foreground);
        painter->drawText(badge, Qt::AlignCenter, operation);

        const QRect textRect = nameRect(item);
        painter->setFont(item.font);
        painter->setPen(item.palette.color(item.state.testFlag(QStyle::State_Selected)
            ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignAbsolute
            | (m_tree->isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft),
            item.fontMetrics.elidedText(name, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(compactOption(option, index));
        initStyleOption(&item, index);
        if (index.parent().isValid()) {
            editor->setGeometry(nameRect(item));
            return;
        }
        QStyledItemDelegate::updateEditorGeometry(editor, item, index);
    }

private:
    static int badgeWidth(const QStyleOptionViewItem &option)
    {
        QFont font = option.font;
        font.setPixelSize(8);
        font.setWeight(QFont::DemiBold);
        return QFontMetrics(font).horizontalAdvance(QStringLiteral("OPTIONS")) + 10;
    }

    QRect nameRect(const QStyleOptionViewItem &option) const
    {
        const int prefix = badgeWidth(option) + 14;
        return m_tree->isRightToLeft() ? option.rect.adjusted(8, 0, -prefix, 0)
                                      : option.rect.adjusted(prefix, 0, -8, 0);
    }

    QStyleOptionViewItem compactOption(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
    {
        QStyleOptionViewItem compact(option);
        if (index.column() == 0 && index.parent().isValid()) {
            const QRect target = m_tree->visualRect(index);
            if (target.isValid()) {
                // Editors and drag previews may already use the compact width.
                // Preserve their coordinate system and avoid applying the shift twice.
                if (m_tree->isRightToLeft()) compact.rect.setWidth(target.width());
                else compact.rect.setLeft(compact.rect.right() - target.width() + 1);
            }
        }
        return compact;
    }

    QTreeWidget *m_tree;
};

using RequestBodyEditor = CodeEditor;

class ActivePageStack final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize{}; }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize{}; }
    bool hasHeightForWidth() const override { return currentWidget() && currentWidget()->hasHeightForWidth(); }
    int heightForWidth(int width) const override { return currentWidget() ? currentWidget()->heightForWidth(width) : -1; }
};

void buttonIcon(QAbstractButton *button, const QString &name, const QString &tone = {})
{
    button->setProperty("iconName", name);
    button->setProperty("iconTone", tone);
    button->setIcon(AppTheme::icon(name, tone.isEmpty() ? QColor{} : AppTheme::color(tone)));
    button->setIconSize(QSize(16, 16));
    button->setCursor(Qt::PointingHandCursor);
}

class ThinSplitterHandle final : public QSplitterHandle
{
public:
    ThinSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool highlighted = isEnabled() && (underMouse() || m_dragging);
        if (!highlighted) return;
        const int thickness = 5;
        QRect line = rect();
        if (orientation() == Qt::Horizontal) {
            line.setLeft((width() - thickness) / 2);
            line.setWidth(thickness);
        } else {
            line.setTop((height() - thickness) / 2);
            line.setHeight(thickness);
        }
        QPainter painter(this);
        painter.fillRect(line, AppTheme::color(highlighted ? u"accentLine" : u"lineSoft"));
    }

    void enterEvent(QEnterEvent *event) override
    {
        QSplitterHandle::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QSplitterHandle::leaveEvent(event);
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) m_dragging = true;
        QSplitterHandle::mousePressEvent(event);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) m_dragging = false;
        QSplitterHandle::mouseReleaseEvent(event);
        update();
    }

private:
    bool m_dragging = false;
};

class ThinSplitter final : public QSplitter
{
public:
    ThinSplitter(Qt::Orientation orientation, QWidget *parent)
        : QSplitter(orientation, parent)
    {
        // Keep the grab area stable; only the painted line changes on hover.
        setHandleWidth(SplitterHandleWidth);
        setChildrenCollapsible(false);
    }

protected:
    QSplitterHandle *createHandle() override
    {
        return new ThinSplitterHandle(orientation(), this);
    }
};

void restoreSplitterState(QSplitter *splitter, const QByteArray &state)
{
    const Qt::Orientation orientation = splitter->orientation();
    if (!state.isEmpty()) splitter->restoreState(state);
    // Qt also serializes appearance and behavior, including legacy handle widths.
    splitter->setOrientation(orientation);
    splitter->setHandleWidth(SplitterHandleWidth);
    splitter->setChildrenCollapsible(false);
}

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldLabel"));
    return label;
}

QFrame *card(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

QLabel *counterLabel(const QString &caption, QLabel **value, QWidget *parent,
                     QHBoxLayout *layout)
{
    auto *box = new QVBoxLayout;
    box->setSpacing(0);
    auto *name = new QLabel(caption, parent);
    name->setObjectName(QStringLiteral("statName"));
    *value = new QLabel(QStringLiteral("0"), parent);
    (*value)->setObjectName(QStringLiteral("statValue"));
    box->addWidget(name);
    box->addWidget(*value);
    layout->addLayout(box);
    return name;
}

} // namespace

class EditableTitle final : public QWidget
{
public:
    EditableTitle(const QString &text, std::function<void(const QString &)> changed,
                  QWidget *parent = nullptr)
        : QWidget(parent)
        , m_changed(std::move(changed))
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        layout->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_label = new QLabel(text, this);
        m_label->setObjectName(QStringLiteral("editableTitleLabel"));
        m_label->setCursor(Qt::PointingHandCursor);
        m_label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
        m_label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_label->installEventFilter(this);
        layout->addWidget(m_label);
        m_editButton = new QToolButton(this);
        m_editButton->setObjectName(QStringLiteral("editNameButton"));
        buttonIcon(m_editButton, QStringLiteral("edit"));
        m_editButton->setToolTip(UiLanguage::text("Переименовать"));
        m_editButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(m_editButton);
        m_editor = new QLineEdit(this);
        m_editor->setObjectName(QStringLiteral("editableTitleEdit"));
        m_editor->setAlignment(Qt::AlignLeading);
        m_editor->installEventFilter(this);
        m_editor->hide();
        layout->addWidget(m_editor, 1);
        connect(m_editButton, &QToolButton::clicked, this, [this] { beginEdit(); });
    }

    void setText(const QString &text)
    {
        if (m_editing) {
            return;
        }
        m_label->setText(text);
        updateGeometry();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_label && event->type() == QEvent::MouseButtonDblClick) {
            beginEdit();
            return true;
        }
        if (watched == m_editor && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                finishEdit(false);
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                finishEdit(true);
                return true;
            }
        }
        if (watched == m_editor && event->type() == QEvent::FocusOut) {
            finishEdit(true);
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void beginEdit()
    {
        if (m_editing) {
            return;
        }
        m_editing = true;
        m_original = m_label->text();
        m_editor->setText(m_original);
        m_label->hide();
        m_editButton->hide();
        m_editor->show();
        updateGeometry();
        m_editor->setFocus();
        m_editor->selectAll();
    }

    void finishEdit(bool commit)
    {
        if (!m_editing) {
            return;
        }
        m_editing = false;
        const QString candidate = m_editor->text().trimmed();
        const QString result = commit && !candidate.isEmpty() ? candidate : m_original;
        m_editor->hide();
        m_label->setText(result);
        m_label->show();
        m_editButton->show();
        updateGeometry();
        if (commit && result != m_original && m_changed) {
            m_changed(result);
        }
    }

    QLabel *m_label = nullptr;
    QToolButton *m_editButton = nullptr;
    QLineEdit *m_editor = nullptr;
    QString m_original;
    std::function<void(const QString &)> m_changed;
    bool m_editing = false;
};

class RequestTab final : public QWidget
{
public:
    struct Actions {
        std::function<void(const RequestDefinition &)> changed;
        std::function<void()> openCollection;
        std::function<void()> sendOnce;
        std::function<void()> start;
        std::function<void()> stop;
        std::function<void()> clearLog;
        std::function<void(const QByteArray &)> splitterChanged;
        std::function<void(const QByteArray &)> editorSplitterChanged;
        std::function<void(int)> transportBodyHeightChanged;
        std::function<void(bool)> autoScrollChanged;
        std::function<void(bool)> prettyJsonChanged;
        std::function<void(const RequestLogEntry &)> replay;
    };

    RequestTab(const RequestDefinition &request, const CollectionDefinition &collection,
               RequestLogModel *model, const ApplicationSettings &settings,
               Actions actions, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_request(request)
        , m_collection(collection)
        , m_actions(std::move(actions))
        , m_postSplitterState(settings.requestEditorSplitterState)
        , m_transportBodyHeight(settings.transportBodyHeight)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_editorSplitter = new ThinSplitter(Qt::Vertical, this);
        m_editorScroll = new QScrollArea(m_editorSplitter);
        m_editorScroll->setObjectName(QStringLiteral("requestEditorScrollArea"));
        m_editorScroll->setWidgetResizable(true);
        m_editorScroll->setFrameShape(QFrame::NoFrame);
        m_editorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *editorCard = card(m_editorScroll);
        editorCard->setObjectName(QStringLiteral("requestEditorCard"));
        m_editorCard = editorCard;
        auto *editorLayout = new QVBoxLayout(editorCard);
        editorLayout->setContentsMargins(20, 12, 20, 10);
        editorLayout->setSpacing(10);
        // Scroll a short viewport instead of compressing fields below their text.
        editorLayout->setVerticalSizeConstraint(QLayout::SetMinimumSize);
        auto *header = new QHBoxLayout;
        m_collectionLink = new QPushButton(collection.name, editorCard);
        m_collectionLink->setObjectName(QStringLiteral("breadcrumbButton"));
        m_collectionLink->setMaximumWidth(220);
        m_collectionLink->setToolTip(UiLanguage::text("Открыть коллекцию"));
        buttonIcon(m_collectionLink, QStringLiteral("folder"));
        header->addWidget(m_collectionLink);
        connect(m_collectionLink, &QPushButton::clicked, this, [this] { if (m_actions.openCollection) m_actions.openCollection(); });
        auto *breadcrumbSeparator = fieldLabel(QStringLiteral("/"), editorCard);
        header->addWidget(breadcrumbSeparator);
        m_title = new EditableTitle(request.name, [this](const QString &name) {
            m_request.name = name;
            if (m_actions.changed) m_actions.changed(m_request);
        }, editorCard);
        header->addWidget(m_title, 0, Qt::AlignLeft | Qt::AlignVCenter);
        header->addStretch();
        m_effectiveUrl = new QLabel(editorCard);
        m_effectiveUrl->setObjectName(QStringLiteral("urlSummary"));
        m_effectiveUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_effectiveUrl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_effectiveUrl->setMinimumWidth(80);
        m_effectiveUrl->setMaximumWidth(420);
        editorLayout->addLayout(header);

        auto *mainRow = new QHBoxLayout;
        mainRow->setSpacing(8);
        auto *addressBar = new QFrame(editorCard);
        addressBar->setLayoutDirection(Qt::LeftToRight);
        addressBar->setObjectName(QStringLiteral("requestAddressBar"));
        addressBar->setFixedHeight(42);
        auto *addressLayout = new QHBoxLayout(addressBar);
        addressLayout->setContentsMargins(0, 0, 0, 0);
        addressLayout->setSpacing(0);
        mainRow->addWidget(addressBar, 1);
        m_protocol = new QComboBox(editorCard);
        m_protocol->setObjectName(QStringLiteral("requestProtocol"));
        m_protocol->setAccessibleName(UiLanguage::text("Протокол"));
        for (const auto protocol : {RequestProtocol::Http, RequestProtocol::Tcp,
                                    RequestProtocol::Udp, RequestProtocol::Serial}) {
            m_protocol->addItem(requestProtocolName(protocol), static_cast<int>(protocol));
        }
        m_protocol->setFixedWidth(85);
        addressLayout->addWidget(m_protocol);
        m_method = new QComboBox(editorCard);
        m_method->setAccessibleName(UiLanguage::text("HTTP-метод"));
        m_method->setObjectName(QStringLiteral("httpMethod"));
        for (const auto method : {HttpMethod::Get, HttpMethod::Post, HttpMethod::Put,
             HttpMethod::Patch, HttpMethod::Delete, HttpMethod::Head, HttpMethod::Options})
            m_method->addItem(httpMethodName(method), static_cast<int>(method));
        m_method->setFixedWidth(95);
        addressLayout->addWidget(m_method);
        m_target = new QLineEdit(editorCard);
        m_target->setAccessibleName(UiLanguage::text("Путь HTTP-запроса"));
        m_target->setObjectName(QStringLiteral("requestTarget"));
        m_target->setPlaceholderText(QStringLiteral("/path?query=value"));
        addressLayout->addWidget(m_target, 1);
        m_transportAddress = new QLineEdit(editorCard);
        m_transportAddress->setReadOnly(true);
        m_transportAddress->setObjectName(QStringLiteral("transportAddress"));
        m_transportAddress->setAccessibleName(UiLanguage::text("Адрес соединения"));
        m_transportAddress->setToolTip(UiLanguage::text("Адрес и порт задаются на вкладке «Параметры»"));
        addressLayout->addWidget(m_transportAddress, 1);
        m_send = new QPushButton(UiLanguage::text("Отправить") + QStringLiteral("    ↵"), editorCard);
        m_send->setObjectName(QStringLiteral("sendButton"));
        m_send->setFixedHeight(42);
        m_send->setMinimumWidth(160);
        m_send->setAccessibleName(UiLanguage::text("Отправить"));
        mainRow->addWidget(m_send);
        m_start = new QPushButton(UiLanguage::text("Старт"), editorCard);
        m_start->setObjectName(QStringLiteral("primaryButton"));
        m_start->setFixedHeight(42);
        m_start->setMinimumWidth(90);
        mainRow->addWidget(m_start);
        m_stop = new QPushButton(UiLanguage::text("Стоп"), editorCard);
        m_stop->setObjectName(QStringLiteral("dangerButton"));
        m_stop->hide();
        editorLayout->addLayout(mainRow);
        auto *statusRow = new QHBoxLayout;
        statusRow->setSpacing(16);
        m_validation = new QLabel(editorCard);
        m_validation->setObjectName(QStringLiteral("validationLabel"));
        m_validation->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        statusRow->addWidget(m_validation);
        m_effectiveUrl->setObjectName(QStringLiteral("connectionSummary"));
        m_effectiveUrl->setMaximumWidth(QWIDGETSIZE_MAX);
        statusRow->addWidget(m_effectiveUrl, 1);
        m_runtime = new QLabel(editorCard);
        m_runtime->setObjectName(QStringLiteral("connectionSummary"));
        statusRow->addWidget(m_runtime);
        editorLayout->addLayout(statusRow);
        m_editorTabs = new QTabBar(editorCard);
        m_editorTabs->setObjectName(QStringLiteral("editorTabs"));
        m_editorTabs->setExpanding(false);
        m_editorTabs->setDrawBase(false);
        m_editorTabs->setIconSize(QSize(14, 14));
        m_editorTabs->setProperty("tabIconNames", QStringList{QStringLiteral("code"), QStringLiteral("headers"), QStringLiteral("settings")});
        m_editorTabs->addTab(AppTheme::icon(u"code"), UiLanguage::text("Тело запроса"));
        m_editorTabs->addTab(AppTheme::icon(u"headers"), UiLanguage::text("Заголовки"));
        m_editorTabs->addTab(AppTheme::icon(u"settings"), UiLanguage::text("Параметры"));
        editorLayout->addWidget(m_editorTabs);
        m_editorPages = new ActivePageStack(editorCard);
        auto *bodyPage = new QWidget(m_editorPages);
        auto *bodyLayout = new QVBoxLayout(bodyPage);
        bodyLayout->setContentsMargins(0, 4, 0, 0);
        bodyLayout->setSpacing(8);
        m_editorPages->addWidget(bodyPage);
        m_headers = new HttpHeadersEditor(m_editorPages);
        m_editorPages->addWidget(m_headers);
        auto *settingsPage = new QWidget(m_editorPages);
        auto *settingsLayout = new QVBoxLayout(settingsPage);
        settingsLayout->setContentsMargins(0, 4, 0, 0);
        settingsLayout->setSpacing(10);
        settingsLayout->addWidget(createSerialOptions(settingsPage));
        m_editorPages->addWidget(settingsPage);
        editorLayout->addWidget(m_editorPages, 1);
        buttonIcon(m_send, QStringLiteral("send"), QStringLiteral("onAccent"));
        buttonIcon(m_start, QStringLiteral("play"));
        buttonIcon(m_stop, QStringLiteral("stop"), QStringLiteral("red"));
        m_send->setToolTip(UiLanguage::text("Отправить один раз · Ctrl+Enter"));
        m_start->setText(UiLanguage::text("Цикл"));
        m_start->setToolTip(UiLanguage::text("Запустить периодическую отправку · F5"));
        m_stop->setToolTip(UiLanguage::text("Остановить отправку · F5"));

        auto *common = new QGridLayout;
        m_commonGrid = common;
        common->setHorizontalSpacing(10);
        m_hostOwn = new QCheckBox(editorCard);
        m_portOwn = new QCheckBox(editorCard);
        m_intervalOwn = new QCheckBox(editorCard);
        m_parallelOwn = new QCheckBox(editorCard);
        const auto addHeader = [common, editorCard](QCheckBox *own, const QString &text,
                                                    int column) {
            auto *header = new QWidget(editorCard);
            auto *layout = new QHBoxLayout(header);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(4);
            own->setAccessibleName(UiLanguage::text("Своё значение: %1").arg(text));
            own->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            layout->addWidget(own);
            layout->addWidget(fieldLabel(text, header));
            layout->addStretch();
            common->addWidget(header, 0, column);
            return header;
        };
        m_hostHeader = addHeader(m_hostOwn, UiLanguage::text("АДРЕС"), 0);
        m_portHeader = addHeader(m_portOwn, UiLanguage::text("ПОРТ"), 1);
        addHeader(m_intervalOwn, UiLanguage::text("ИНТЕРВАЛ, МС"), 2);
        m_parallelHeader = addHeader(m_parallelOwn, UiLanguage::text("ПАРАЛЛЕЛЬНО"), 3);
        m_host = new QLineEdit(editorCard);
        m_host->setLayoutDirection(Qt::LeftToRight);
        m_port = new QSpinBox(editorCard);
        m_port->setRange(1, 65535);
        m_interval = new QSpinBox(editorCard);
        m_interval->setRange(1, 86'400'000);
        m_interval->setGroupSeparatorShown(true);
        m_parallel = new QSpinBox(editorCard);
        m_parallel->setRange(1, 100'000);
        m_parallel->setGroupSeparatorShown(true);
        common->addWidget(m_host, 1, 0);
        common->addWidget(m_port, 1, 1);
        common->addWidget(m_interval, 1, 2);
        common->addWidget(m_parallel, 1, 3);
        common->setColumnStretch(0, 3);
        common->setColumnStretch(1, 1);
        common->setColumnStretch(2, 2);
        common->setColumnStretch(3, 2);
        settingsLayout->addLayout(common);

        m_httpOptions = new QWidget(settingsPage);
        auto *httpLayout = new QHBoxLayout(m_httpOptions);
        httpLayout->setContentsMargins(0, 4, 0, 4);
        httpLayout->addWidget(fieldLabel(UiLanguage::text("СОЕДИНЕНИЕ"), m_httpOptions));
        m_httpScheme = new QComboBox(m_httpOptions);
        m_httpScheme->setObjectName(QStringLiteral("httpScheme"));
        m_httpScheme->addItem(QStringLiteral("HTTP"), QStringLiteral("http"));
        m_httpScheme->addItem(QStringLiteral("HTTPS"), QStringLiteral("https"));
        m_httpScheme->setToolTip(UiLanguage::text("HTTPS проверяет сертификат сервера. Порт задаётся отдельно."));
        httpLayout->addWidget(m_httpScheme);
        httpLayout->addSpacing(16);
        httpLayout->addWidget(fieldLabel(UiLanguage::text("ТАЙМ-АУТ HTTP, МС"), m_httpOptions));
        m_httpTimeout = new QSpinBox(m_httpOptions);
        m_httpTimeout->setObjectName(QStringLiteral("httpTimeout"));
        m_httpTimeout->setRange(1, 86'400'000);
        m_httpTimeout->setGroupSeparatorShown(true);
        m_httpTimeout->setToolTip(UiLanguage::text("Полный лимит времени HTTP-запроса, включая получение ответа."));
        httpLayout->addWidget(m_httpTimeout);
        httpLayout->addStretch();
        settingsLayout->addWidget(m_httpOptions);

        m_socketOptions = new QWidget(editorCard);
        auto *socketOptionsLayout = new QHBoxLayout(m_socketOptions);
        socketOptionsLayout->setContentsMargins(0, 4, 0, 4);
        m_waitForResponse = new QCheckBox(UiLanguage::text("Ожидать ответ"), m_socketOptions);
        m_waitForResponse->setObjectName(QStringLiteral("waitForResponse"));
        socketOptionsLayout->addWidget(m_waitForResponse);
        socketOptionsLayout->addSpacing(16);
        socketOptionsLayout->addWidget(fieldLabel(UiLanguage::text("ТАЙМ-АУТ, МС"), m_socketOptions));
        m_timeout = new QSpinBox(m_socketOptions);
        m_timeout->setObjectName(QStringLiteral("socketTimeout"));
        m_timeout->setAccessibleName(UiLanguage::text("Тайм-аут, мс"));
        m_timeout->setRange(1, 86'400'000);
        m_timeout->setGroupSeparatorShown(true);
        m_timeout->setToolTip(UiLanguage::text("Лимит подключения и отправки; при ожидании ответа — длительность окна приёма после отправки"));
        socketOptionsLayout->addWidget(m_timeout);
        socketOptionsLayout->addStretch();
        settingsLayout->addWidget(m_socketOptions);
        m_transportHint = new QLabel(editorCard);
        m_transportHint->setObjectName(QStringLiteral("subtitleLabel"));
        m_transportHint->setWordWrap(true);
        settingsLayout->addWidget(m_transportHint);
        auto *inheritHint = new QLabel(UiLanguage::text("Параметры наследуются из коллекции. Отметьте поле, чтобы задать своё значение."), settingsPage);
        inheritHint->setObjectName(QStringLiteral("subtitleLabel"));
        inheritHint->setWordWrap(true);
        settingsLayout->addWidget(inheritHint);
        settingsLayout->addStretch();

        m_bodyLabel = fieldLabel(UiLanguage::text("ТЕЛО POST · application/json · UTF-8"), editorCard);
        auto *bodyToolbar = new QHBoxLayout;
        bodyToolbar->addWidget(m_bodyLabel);
        bodyToolbar->addStretch();
        m_formatBody = new QPushButton(UiLanguage::text("Форматировать JSON"), bodyPage);
        m_formatBody->setObjectName(QStringLiteral("compactButton"));
        buttonIcon(m_formatBody, QStringLiteral("code"));
        bodyToolbar->addWidget(m_formatBody);
        bodyLayout->addLayout(bodyToolbar);
        m_body = new RequestBodyEditor(editorCard);
        m_bodyHighlighter = new PayloadHighlighter(m_body->document());
        m_body->setPlaceholderText(QStringLiteral("{\n    \"message\": \"value\"\n}"));
        m_body->setMinimumHeight(74);
        m_body->setObjectName(QStringLiteral("codeEditor"));
        bodyLayout->addWidget(m_body, 1);
        m_bodyEmpty = new QLabel(UiLanguage::text("У GET-запроса нет тела\nПараметры запроса можно указать в адресной строке."), bodyPage);
        m_bodyEmpty->setObjectName(QStringLiteral("emptyHint"));
        m_bodyEmpty->setAlignment(Qt::AlignCenter);
        m_bodyEmpty->setMinimumHeight(72);
        bodyLayout->addWidget(m_bodyEmpty, 1);
        connect(m_editorTabs, &QTabBar::currentChanged, this, [this](int index) {
            if (m_editorPages->currentIndex() == 0 && m_postMode)
                m_postSplitterState = m_editorSplitter->saveState();
            m_editorPages->setCurrentIndex(index);
            m_editorPages->updateGeometry();
            refreshEffective();
            if (index == 0 && m_postMode && !m_postSplitterState.isEmpty())
                restoreSplitterState(m_editorSplitter, m_postSplitterState);
        });
        connect(m_formatBody, &QPushButton::clicked, this, [this] {
            QJsonParseError error;
            const QJsonDocument json = QJsonDocument::fromJson(m_body->toPlainText().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || json.isNull()) {
                m_validation->setText(QStringLiteral("JSON: %1").arg(error.errorString()));
                m_validation->setProperty("state", QStringLiteral("invalid"));
                m_validation->style()->unpolish(m_validation);
                m_validation->style()->polish(m_validation);
                return;
            }
            QTextCursor cursor = m_body->textCursor();
            cursor.beginEditBlock();
            cursor.select(QTextCursor::Document);
            cursor.insertText(QString::fromUtf8(json.toJson(QJsonDocument::Indented)).trimmed());
            cursor.endEditBlock();
        });
        m_editorScroll->setWidget(editorCard);
        m_editorSplitter->addWidget(m_editorScroll);

        m_logPane = new LogPane(model, settings.logDetailsVerticalSplitterState,
            settings.autoScroll, settings.prettyJson,
            [this](const QByteArray &state) {
                if (m_actions.splitterChanged) {
                    m_actions.splitterChanged(state);
                }
            }, m_actions.autoScrollChanged, m_actions.prettyJsonChanged, m_editorSplitter);
        m_logPane->setClearAction(m_actions.clearLog);
        m_logPane->setReplayAction(m_actions.replay);
        m_editorSplitter->addWidget(m_logPane);
        m_editorSplitter->setStretchFactor(0, 0);
        m_editorSplitter->setStretchFactor(1, 1);
        m_editorSplitter->setSizes({350, 500});
        layout->addWidget(m_editorSplitter, 1);
        connect(m_editorSplitter, &QSplitter::splitterMoved, this, [this] {
            if (m_editorPages->currentIndex() != 0) return;
            if (m_request.protocol != RequestProtocol::Http) {
                m_editorCard->layout()->activate();
                m_transportBodyHeight = m_body->height();
                if (m_actions.transportBodyHeightChanged) {
                    m_actions.transportBodyHeightChanged(m_transportBodyHeight);
                }
                return;
            }
            if (!m_postMode) return;
            m_postSplitterState = m_editorSplitter->saveState();
            if (m_actions.editorSplitterChanged) {
                m_actions.editorSplitterChanged(m_postSplitterState);
            }
        });
        connect(m_send, &QPushButton::clicked, this, [this] {
            m_actions.sendOnce();
            m_logPane->revealLatestEntry();
        });
        connect(m_start, &QPushButton::clicked, this, [this] {
            if (m_running || m_activeOperations > 0) m_actions.stop();
            else { m_actions.start(); m_logPane->revealLatestEntry(); }
        });
        connect(m_stop, &QPushButton::clicked, this, [this] { m_actions.stop(); });
        connectEditorSignals();
        setDefinition(request, collection);
    }

    void setDefinition(const RequestDefinition &request, const CollectionDefinition &collection)
    {
        m_updating = true;
        m_request = request;
        m_collection = collection;
        m_title->setText(request.name);
        m_collectionLink->setText(collection.name);
        m_protocol->setCurrentIndex(m_protocol->findData(static_cast<int>(request.protocol)));
        if (m_serialPort->currentText().trimmed() != request.serial.portName) {
            m_serialPort->setCurrentText(request.serial.portName);
        }
        // Preserve a partially entered custom rate while local changes echo back.
        if (m_serialBaudRate->currentText().toInt() != request.serial.baudRate) {
            m_serialBaudRate->setCurrentText(QString::number(request.serial.baudRate));
        }
        m_serialDataBits->setCurrentIndex(m_serialDataBits->findData(
            static_cast<int>(request.serial.dataBits)));
        m_serialParity->setCurrentIndex(m_serialParity->findData(
            static_cast<int>(request.serial.parity)));
        m_serialStopBits->setCurrentIndex(m_serialStopBits->findData(
            static_cast<int>(request.serial.stopBits)));
        m_serialFlowControl->setCurrentIndex(m_serialFlowControl->findData(
            static_cast<int>(request.serial.flowControl)));
        m_waitForResponse->setChecked(request.waitForResponse);
        m_timeout->setValue(request.timeoutMs);
        m_target->setText(request.target);
        m_method->setCurrentIndex(m_method->findData(static_cast<int>(request.method)));
        m_httpScheme->setCurrentIndex(m_httpScheme->findData(request.httpScheme));
        m_httpTimeout->setValue(request.httpTimeoutMs);
        m_headers->setHeaders(request.headers);
        // Updating the definition also echoes local edits back to this tab.
        // Replacing an unchanged document resets its cursor and undo history.
        if (m_body->toPlainText() != request.body) {
            m_body->setPlainText(request.body);
        }
        m_host->setText(request.host.overridden ? request.host.value : collection.host);
        m_port->setValue(request.port.overridden ? request.port.value : collection.port);
        m_interval->setValue(request.intervalMs.overridden
                                 ? request.intervalMs.value : collection.intervalMs);
        m_parallel->setValue(request.maxInFlight.overridden
                                 ? request.maxInFlight.value : collection.maxInFlight);
        m_hostOwn->setChecked(request.host.overridden);
        m_portOwn->setChecked(request.port.overridden);
        m_intervalOwn->setChecked(request.intervalMs.overridden);
        m_parallelOwn->setChecked(request.maxInFlight.overridden);
        m_updating = false;
        refreshEffective();
    }

    void updateRuntime(bool running, const RequestCounters &counters)
    {
        m_running = running;
        m_activeOperations = counters.active;
        const bool busy = running || counters.active > 0;
        m_start->setEnabled(busy || validateRequestConfig(resolveRequestConfig(m_collection, m_request)).isEmpty());
        m_start->setText(busy ? UiLanguage::text("Стоп") : UiLanguage::text("Цикл"));
        m_start->setToolTip(busy ? UiLanguage::text("Остановить все отправки этого запроса · F5") : UiLanguage::text("Запустить периодическую отправку · F5"));
        if (m_start->property("running").toBool() != busy || m_start->property("iconName").toString().isEmpty()) {
            m_start->setProperty("running", busy);
            buttonIcon(m_start, busy ? QStringLiteral("stop") : QStringLiteral("play"), busy ? QStringLiteral("red") : QStringLiteral("secondary"));
            m_start->style()->unpolish(m_start);
            m_start->style()->polish(m_start);
        }
        m_stop->setEnabled(running || counters.active > 0);
        m_runtime->setText(busy ? UiLanguage::text("● %1 · %2 активно").arg(running ? UiLanguage::text("В цикле") : UiLanguage::text("Отправка")).arg(counters.active) : QString{});
        m_runtime->setProperty("mode", running ? QStringLiteral("running")
                                                : QStringLiteral("idle"));
        m_runtime->style()->unpolish(m_runtime);
        m_runtime->style()->polish(m_runtime);
        m_logPane->setClearEnabled(counters.active == 0);
    }

    void focusTarget()
    {
        if (m_request.protocol == RequestProtocol::Http) {
            m_target->setFocus();
            m_target->selectAll();
        } else {
            m_editorTabs->setCurrentIndex(0);
            m_body->setFocus();
        }
    }
    void logEntryChanged(const QUuid &entryId) { m_logPane->entryChanged(entryId); }
    void logReplaced() { m_logPane->modelReplaced(); }
    void setPrettyJson(bool enabled) { m_logPane->setPrettyJson(enabled); }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        QTimer::singleShot(0, this, &RequestTab::resizeTransportEditor);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        QTimer::singleShot(0, this, &RequestTab::resizeTransportEditor);
    }

private:
    void resizeTransportEditor()
    {
        if (!m_body || m_request.protocol == RequestProtocol::Http || m_editorPages->currentIndex() != 0) return;
        // Include wrapped hints and COM options, while sizing the payload itself
        // independently of the HTTP POST splitter's previous allocation.
        QLayout *layout = m_editorCard->layout();
        layout->activate();
        const int preferredHeight = layout->hasHeightForWidth()
            ? layout->totalHeightForWidth(m_editorCard->width())
            : layout->totalSizeHint().height();
        const int height = std::max(preferredHeight, layout->totalMinimumSize().height())
            + std::max(0, m_transportBodyHeight - m_body->minimumHeight());
        const int available = m_editorSplitter->height() - m_editorSplitter->handleWidth();
        m_editorSplitter->setSizes({height, std::max(0, available - height)});
    }

    QWidget *createSerialOptions(QWidget *parent)
    {
        m_serialOptions = new QWidget(parent);
        m_serialOptions->setObjectName(QStringLiteral("serialOptions"));
        auto *grid = new QGridLayout(m_serialOptions);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(5);
        grid->setVerticalSizeConstraint(QLayout::SetMinimumSize);
        for (int column = 0; column < 3; ++column) {
            grid->setColumnStretch(column, 1);
        }

        grid->addWidget(fieldLabel(UiLanguage::text("COM-ПОРТ"), m_serialOptions), 0, 0);
        auto *portRow = new QWidget(m_serialOptions);
        auto *portLayout = new QHBoxLayout(portRow);
        portLayout->setContentsMargins(0, 0, 0, 0);
        portLayout->setSpacing(5);
        m_serialPort = new QComboBox(portRow);
        m_serialPort->setLayoutDirection(Qt::LeftToRight);
        m_serialPort->setObjectName(QStringLiteral("serialPort"));
        m_serialPort->setAccessibleName(UiLanguage::text("COM-порт"));
        m_serialPort->setEditable(true);
        m_serialPort->setInsertPolicy(QComboBox::NoInsert);
        m_serialPort->setCompleter(nullptr);
        m_serialPort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_serialPort->setMinimumContentsLength(6);
        m_serialPort->setToolTip(UiLanguage::text("Выберите доступный порт или введите имя вручную, например COM3."));
        portLayout->addWidget(m_serialPort, 1);
        auto *refreshPorts = new QPushButton(UiLanguage::text("Обновить"), portRow);
        refreshPorts->setObjectName(QStringLiteral("refreshSerialPorts"));
        refreshPorts->setAccessibleName(UiLanguage::text("Обновить список COM-портов"));
        refreshPorts->setToolTip(UiLanguage::text("Обновить доступные COM-порты, сохранив выбранное имя."));
        portLayout->addWidget(refreshPorts);
        grid->addWidget(portRow, 1, 0);
        connect(refreshPorts, &QPushButton::clicked, this, [this] { refreshSerialPorts(); });

        const auto addCombo = [this, grid](const QString &label, const QString &name,
                                          int row, int column) {
            grid->addWidget(fieldLabel(label, m_serialOptions), row, column);
            auto *combo = new QComboBox(m_serialOptions);
            combo->setObjectName(name);
            combo->setAccessibleName(label);
            grid->addWidget(combo, row + 1, column);
            return combo;
        };
        m_serialBaudRate = addCombo(UiLanguage::text("СКОРОСТЬ, БОД"),
                                    QStringLiteral("serialBaudRate"), 0, 1);
        m_serialBaudRate->setEditable(true);
        m_serialBaudRate->setInsertPolicy(QComboBox::NoInsert);
        m_serialBaudRate->setCompleter(nullptr);
        m_serialBaudRate->setValidator(new QIntValidator(
            1, std::numeric_limits<qint32>::max(), m_serialBaudRate));
        m_serialBaudRate->setToolTip(UiLanguage::text("Выберите стандартную скорость или введите свою."));
        for (const qint32 rate : QSerialPortInfo::standardBaudRates()) {
            m_serialBaudRate->addItem(QString::number(rate));
        }
        m_serialDataBits = addCombo(UiLanguage::text("БИТЫ ДАННЫХ"),
                                    QStringLiteral("serialDataBits"), 0, 2);
        for (const auto bits : {QSerialPort::Data5, QSerialPort::Data6,
                                QSerialPort::Data7, QSerialPort::Data8}) {
            m_serialDataBits->addItem(QString::number(static_cast<int>(bits)),
                                      static_cast<int>(bits));
        }
        m_serialParity = addCombo(UiLanguage::text("ЧЁТНОСТЬ"),
                                  QStringLiteral("serialParity"), 2, 0);
        m_serialParity->addItem(UiLanguage::text("Нет"), static_cast<int>(QSerialPort::NoParity));
        m_serialParity->addItem(UiLanguage::text("Чётная (Even)"), static_cast<int>(QSerialPort::EvenParity));
        m_serialParity->addItem(UiLanguage::text("Нечётная (Odd)"), static_cast<int>(QSerialPort::OddParity));
        m_serialParity->addItem(UiLanguage::text("Всегда 1 (Mark)"), static_cast<int>(QSerialPort::MarkParity));
        m_serialParity->addItem(UiLanguage::text("Всегда 0 (Space)"), static_cast<int>(QSerialPort::SpaceParity));
        m_serialStopBits = addCombo(UiLanguage::text("СТОП-БИТЫ"),
                                    QStringLiteral("serialStopBits"), 2, 1);
        m_serialStopBits->addItem(QStringLiteral("1"), static_cast<int>(QSerialPort::OneStop));
        m_serialStopBits->addItem(QStringLiteral("1.5"), static_cast<int>(QSerialPort::OneAndHalfStop));
        m_serialStopBits->addItem(QStringLiteral("2"), static_cast<int>(QSerialPort::TwoStop));
        m_serialFlowControl = addCombo(UiLanguage::text("УПРАВЛЕНИЕ ПОТОКОМ"),
                                       QStringLiteral("serialFlowControl"), 2, 2);
        m_serialFlowControl->addItem(UiLanguage::text("Нет"), static_cast<int>(QSerialPort::NoFlowControl));
        m_serialFlowControl->addItem(UiLanguage::text("Аппаратное (RTS/CTS)"),
                                     static_cast<int>(QSerialPort::HardwareControl));
        m_serialFlowControl->addItem(UiLanguage::text("Программное (XON/XOFF)"),
                                     static_cast<int>(QSerialPort::SoftwareControl));
        refreshSerialPorts();
        return m_serialOptions;
    }

    void refreshSerialPorts()
    {
        const QString selected = m_serialPort->currentText();
        const QSignalBlocker blocker(m_serialPort);
        m_serialPort->clear();
        for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
            m_serialPort->addItem(port.portName());
            m_serialPort->setItemData(m_serialPort->count() - 1,
                                      port.description(), Qt::ToolTipRole);
        }
        if (!selected.isEmpty() && m_serialPort->findText(selected) < 0) {
            m_serialPort->addItem(selected);
        }
        m_serialPort->setCurrentText(selected);
    }

    void connectEditorSignals()
    {
        const auto notifyChanged = [this] {
            refreshEffective();
            if (m_actions.changed) m_actions.changed(m_request);
        };
        m_headers->setChangeAction([this, notifyChanged] {
            if (m_updating) return;
            m_request.headers = m_headers->headers();
            notifyChanged();
        });
        connect(m_httpScheme, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.httpScheme = m_httpScheme->currentData().toString();
            notifyChanged();
        });
        connect(m_httpTimeout, &QSpinBox::valueChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.httpTimeoutMs = m_httpTimeout->value();
            notifyChanged();
        });
        connect(m_protocol, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.protocol = static_cast<RequestProtocol>(m_protocol->currentData().toInt());
            notifyChanged();
        });
        connect(m_serialPort, &QComboBox::currentTextChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.serial.portName = m_serialPort->currentText().trimmed();
            notifyChanged();
        });
        connect(m_serialBaudRate, &QComboBox::currentTextChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.serial.baudRate = m_serialBaudRate->currentText().toInt();
            notifyChanged();
        });
        connect(m_serialDataBits, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.serial.dataBits = static_cast<QSerialPort::DataBits>(
                m_serialDataBits->currentData().toInt());
            notifyChanged();
        });
        connect(m_serialParity, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.serial.parity = static_cast<QSerialPort::Parity>(
                m_serialParity->currentData().toInt());
            notifyChanged();
        });
        connect(m_serialStopBits, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.serial.stopBits = static_cast<QSerialPort::StopBits>(
                m_serialStopBits->currentData().toInt());
            notifyChanged();
        });
        connect(m_serialFlowControl, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.serial.flowControl = static_cast<QSerialPort::FlowControl>(
                m_serialFlowControl->currentData().toInt());
            notifyChanged();
        });
        connect(m_waitForResponse, &QCheckBox::toggled, this, [this, notifyChanged](bool enabled) {
            if (m_updating) return;
            m_request.waitForResponse = enabled;
            notifyChanged();
        });
        connect(m_timeout, &QSpinBox::valueChanged, this, [this, notifyChanged](int value) {
            if (m_updating) return;
            m_request.timeoutMs = value;
            notifyChanged();
        });
        connect(m_target, &QLineEdit::textChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.target = m_target->text().trimmed();
            notifyChanged();
        });
        connect(m_method, &QComboBox::currentIndexChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.method = static_cast<HttpMethod>(m_method->currentData().toInt());
            notifyChanged();
        });
        connect(m_body, &QPlainTextEdit::textChanged, this, [this, notifyChanged] {
            if (m_updating) return;
            m_request.body = m_body->toPlainText();
            notifyChanged();
        });
        connect(m_host, &QLineEdit::textChanged, this, [this, notifyChanged] {
            if (m_updating || !m_hostOwn->isChecked()) return;
            m_request.host.value = m_host->text().trimmed();
            notifyChanged();
        });
        connect(m_port, &QSpinBox::valueChanged, this, [this, notifyChanged] {
            if (m_updating || !m_portOwn->isChecked()) return;
            m_request.port.value = static_cast<quint16>(m_port->value());
            notifyChanged();
        });
        connect(m_interval, &QSpinBox::valueChanged, this, [this, notifyChanged] {
            if (m_updating || !m_intervalOwn->isChecked()) return;
            m_request.intervalMs.value = m_interval->value();
            notifyChanged();
        });
        connect(m_parallel, &QSpinBox::valueChanged, this, [this, notifyChanged] {
            if (m_updating || !m_parallelOwn->isChecked()) return;
            m_request.maxInFlight.value = m_parallel->value();
            notifyChanged();
        });
        connect(m_hostOwn, &QCheckBox::toggled, this, [this, notifyChanged](bool own) {
            if (m_updating) return;
            m_request.host.overridden = own;
            if (own) {
                m_request.host.value = m_host->text().trimmed();
            } else {
                m_updating = true;
                m_host->setText(m_collection.host);
                m_updating = false;
            }
            notifyChanged();
        });
        connect(m_portOwn, &QCheckBox::toggled, this, [this, notifyChanged](bool own) {
            if (m_updating) return;
            m_request.port.overridden = own;
            if (own) {
                m_request.port.value = static_cast<quint16>(m_port->value());
            } else {
                m_updating = true;
                m_port->setValue(m_collection.port);
                m_updating = false;
            }
            notifyChanged();
        });
        connect(m_intervalOwn, &QCheckBox::toggled, this, [this, notifyChanged](bool own) {
            if (m_updating) return;
            m_request.intervalMs.overridden = own;
            if (own) {
                m_request.intervalMs.value = m_interval->value();
            } else {
                m_updating = true;
                m_interval->setValue(m_collection.intervalMs);
                m_updating = false;
            }
            notifyChanged();
        });
        connect(m_parallelOwn, &QCheckBox::toggled, this, [this, notifyChanged](bool own) {
            if (m_updating) return;
            m_request.maxInFlight.overridden = own;
            if (own) {
                m_request.maxInFlight.value = m_parallel->value();
            } else {
                m_updating = true;
                m_parallel->setValue(m_collection.maxInFlight);
                m_updating = false;
            }
            notifyChanged();
        });
    }

    void refreshEffective()
    {
        m_host->setEnabled(m_hostOwn->isChecked());
        m_port->setEnabled(m_portOwn->isChecked());
        m_interval->setEnabled(m_intervalOwn->isChecked());
        m_parallel->setEnabled(m_parallelOwn->isChecked());
        m_hostOwn->setToolTip(UiLanguage::text("Из коллекции: %1").arg(m_collection.host));
        m_portOwn->setToolTip(UiLanguage::text("Из коллекции: %1").arg(m_collection.port));
        m_intervalOwn->setToolTip(UiLanguage::text("Из коллекции: %1 мс").arg(m_collection.intervalMs));
        m_parallelOwn->setToolTip(UiLanguage::text("Из коллекции: %1").arg(m_collection.maxInFlight));
        const RequestConfig config = resolveRequestConfig(m_collection, m_request);
        const QString endpoint = buildRequestUrl(config).toString(QUrl::FullyEncoded);
        m_effectiveUrl->setText(UiLanguage::text("%1  ·  %2 мс  ·  %3").arg(
            config.protocol == RequestProtocol::Serial ? config.serial.portName : QStringLiteral("%1:%2").arg(config.host).arg(config.port))
            .arg(config.intervalMs).arg(config.protocol == RequestProtocol::Serial
                ? UiLanguage::text("%1 бод").arg(config.serial.baudRate) : UiLanguage::text("до %1 параллельно").arg(config.maxInFlight)));
        if (m_transportAddress->text() != endpoint) {
            m_transportAddress->setText(endpoint);
            m_transportAddress->setCursorPosition(0);
        }
        m_effectiveUrl->setToolTip(endpoint);
        const QString error = validateRequestConfig(config);
        m_validation->setText(error.isEmpty() ? UiLanguage::text("● Готово к отправке") : UiLanguage::text("● Проверьте параметры"));
        m_validation->setToolTip(error);
        m_validation->setProperty("state", error.isEmpty() ? QStringLiteral("valid")
                                                            : QStringLiteral("invalid"));
        m_validation->style()->unpolish(m_validation);
        m_validation->style()->polish(m_validation);
        m_send->setEnabled(error.isEmpty());
        m_start->setEnabled(m_running || m_activeOperations > 0 || error.isEmpty());
        const bool http = config.protocol == RequestProtocol::Http;
        const bool serial = config.protocol == RequestProtocol::Serial;
        m_serialOptions->setVisible(serial);
        m_hostHeader->setVisible(!serial);
        m_host->setVisible(!serial);
        m_portHeader->setVisible(!serial);
        m_port->setVisible(!serial);
        m_parallelHeader->setVisible(!serial);
        m_parallel->setVisible(!serial);
        m_commonGrid->setColumnStretch(0, serial ? 0 : 3);
        m_commonGrid->setColumnStretch(1, serial ? 0 : 1);
        m_commonGrid->setColumnStretch(2, serial ? 1 : 2);
        m_commonGrid->setColumnStretch(3, serial ? 0 : 2);
        m_bodyHighlighter->setJsonEnabled(http);
        m_bodyHighlighter->setEscapesEnabled(!http);
        m_method->setVisible(http);
        const QString methodName = httpMethodName(config.method);
        if (m_method->property("method").toString() != methodName) {
            m_method->setProperty("method", methodName);
            m_method->style()->unpolish(m_method);
            m_method->style()->polish(m_method);
        }
        m_httpOptions->setVisible(http);
        m_editorTabs->setTabVisible(1, http);
        const auto headerCount = std::ranges::count_if(config.headers, [](const HttpHeader &header) { return header.enabled && !header.name.isEmpty(); });
        m_editorTabs->setTabText(1, headerCount ? UiLanguage::text("Заголовки  %1").arg(headerCount) : UiLanguage::text("Заголовки"));
        if (!http && m_editorTabs->currentIndex() == 1) m_editorTabs->setCurrentIndex(0);
        m_target->setVisible(http);
        m_transportAddress->setVisible(!http);
        m_socketOptions->setVisible(!http);
        m_transportHint->setVisible(!http);
        m_timeout->setToolTip(serial
            ? UiLanguage::text("Лимит отправки; при ожидании ответа — длительность приёма после отправки.")
            : UiLanguage::text("Лимит подключения и отправки; при ожидании ответа — длительность окна приёма после отправки"));
        if (serial) {
            m_transportHint->setText(config.waitForResponse
                ? UiLanguage::text("Порт открывается для каждой отправки. Ответ собирается до конца тайм-аута; "
                                 "без данных — ошибка ожидания. На одном COM-порту выполняется только одна операция.")
                : UiLanguage::text("Порт открывается для каждой отправки. Успех означает передачу байтов драйверу. "
                                 "На одном COM-порту выполняется только одна операция."));
        } else if (!config.waitForResponse) {
            m_transportHint->setText(UiLanguage::text("Успех означает отправку данных; получение сервером не подтверждается."));
        } else if (config.protocol == RequestProtocol::Tcp) {
            m_transportHint->setText(UiLanguage::text("Ответ TCP собирается до закрытия соединения или конца тайм-аута. Без данных — ошибка ожидания."));
        } else {
            m_transportHint->setText(UiLanguage::text("Ожидание одной ответной UDP-датаграммы до тайм-аута."));
        }
        m_bodyLabel->setText(http ? UiLanguage::text("ТЕЛО %1 · UTF-8").arg(httpMethodName(config.method))
                                 : UiLanguage::text("ДАННЫЕ %1 · UTF-8 / \\xHH").arg(requestProtocolName(config.protocol)));
        m_body->setPlaceholderText(http ? QStringLiteral("{\n    \"message\": \"value\"\n}")
                                       : UiLanguage::text("Текст и байты: ABC\\x1D123\n\\\\ — один обратный слеш."));
        m_body->setToolTip(http ? QString{}
            : UiLanguage::text("Текст кодируется в UTF-8. \\xHH — один байт в шестнадцатеричном виде "
                             "(например, \\x1D — байт 0x1D). \\\\ — один обратный слеш. "
                             "Корректные последовательности выделены синим; неполные или ошибочные "
                             "\\xHH подчёркнуты красным. Переводы строк не добавляются автоматически."));
        m_bodyLabel->setToolTip(m_body->toolTip());
        const bool post = http && httpMethodHasBody(config.method);
        m_bodyEmpty->setText(UiLanguage::text("Для %1 тело не отправляется\nПараметры можно указать в адресной строке, заголовки — на соседней вкладке.").arg(httpMethodName(config.method)));
        const bool showBody = !http || post;
        const bool modeChanged = post != m_postMode;
        if (modeChanged && m_postMode) {
            m_postSplitterState = m_editorSplitter->saveState();
        }
        m_postMode = post;
        m_body->ensurePolished();
        const QMargins margins = m_body->contentsMargins();
        const int threeLines = 3 * m_body->fontMetrics().lineSpacing()
            + qCeil(2 * m_body->document()->documentMargin()) + margins.top() + margins.bottom();
        m_body->setMinimumHeight(http ? 74 : threeLines);
        // Keep the scroll area's height-for-width minimum at three lines even
        // after a manual enlargement, so the splitter can shrink the body again.
        m_body->setPreferredHeight(http ? 0 : threeLines);
        m_bodyEmpty->setVisible(!showBody);
        m_formatBody->setVisible(http && post);
        m_bodyLabel->setVisible(showBody);
        m_body->setVisible(showBody);
        const bool bodyPage = m_editorPages->currentIndex() == 0;
        const bool expandable = !bodyPage || showBody;
        m_editorCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_editorPages->updateGeometry();
        m_editorCard->layout()->activate();
        m_editorCard->setMaximumHeight(QWIDGETSIZE_MAX);
        m_editorScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        m_editorScroll->setMaximumHeight(QWIDGETSIZE_MAX);
        m_editorScroll->setMinimumHeight(190);
        m_editorSplitter->handle(1)->setEnabled(true);
        if (!http && bodyPage) {
            resizeTransportEditor();
        } else if (modeChanged && post && bodyPage) {
            restoreSplitterState(m_editorSplitter, m_postSplitterState);
            if (m_postSplitterState.isEmpty()) m_editorSplitter->setSizes({330, 500});
        } else if (!expandable) {
            m_editorSplitter->setSizes({230, m_editorSplitter->height()});
        }

    }

    RequestDefinition m_request;
    CollectionDefinition m_collection;
    Actions m_actions;
    QByteArray m_postSplitterState;
    int m_transportBodyHeight = 0;
    QSplitter *m_editorSplitter = nullptr;
    QScrollArea *m_editorScroll = nullptr;
    QFrame *m_editorCard = nullptr;
    EditableTitle *m_title = nullptr;
    QPushButton *m_collectionLink = nullptr;
    LogPane *m_logPane = nullptr;
    QComboBox *m_protocol = nullptr;
    QWidget *m_serialOptions = nullptr;
    QComboBox *m_serialPort = nullptr;
    QComboBox *m_serialBaudRate = nullptr;
    QComboBox *m_serialDataBits = nullptr;
    QComboBox *m_serialParity = nullptr;
    QComboBox *m_serialStopBits = nullptr;
    QComboBox *m_serialFlowControl = nullptr;
    QGridLayout *m_commonGrid = nullptr;
    QWidget *m_hostHeader = nullptr;
    QWidget *m_portHeader = nullptr;
    QWidget *m_parallelHeader = nullptr;
    QWidget *m_socketOptions = nullptr;
    QCheckBox *m_waitForResponse = nullptr;
    QSpinBox *m_timeout = nullptr;
    QLabel *m_transportHint = nullptr;
    QComboBox *m_method = nullptr;
    HttpHeadersEditor *m_headers = nullptr;
    QWidget *m_httpOptions = nullptr;
    QComboBox *m_httpScheme = nullptr;
    QSpinBox *m_httpTimeout = nullptr;
    QLineEdit *m_target = nullptr;
    QLineEdit *m_transportAddress = nullptr;
    RequestBodyEditor *m_body = nullptr;
    QTabBar *m_editorTabs = nullptr;
    ActivePageStack *m_editorPages = nullptr;
    QLabel *m_bodyEmpty = nullptr;
    QPushButton *m_formatBody = nullptr;
    PayloadHighlighter *m_bodyHighlighter = nullptr;
    QLabel *m_bodyLabel = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QSpinBox *m_interval = nullptr;
    QSpinBox *m_parallel = nullptr;
    QCheckBox *m_hostOwn = nullptr;
    QCheckBox *m_portOwn = nullptr;
    QCheckBox *m_intervalOwn = nullptr;
    QCheckBox *m_parallelOwn = nullptr;
    QLabel *m_effectiveUrl = nullptr;
    QLabel *m_validation = nullptr;
    QLabel *m_runtime = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    bool m_updating = false;
    bool m_postMode = false;
    bool m_running = false;
    int m_activeOperations = 0;
};

class CollectionTab final : public QWidget
{
public:
    struct Actions {
        std::function<void(const CollectionDefinition &)> changed;
        std::function<void()> start;
        std::function<void()> stop;
        std::function<void()> clearLog;
        std::function<void(const QByteArray &)> splitterChanged;
        std::function<void(const QByteArray &)> editorSplitterChanged;
        std::function<void(bool)> autoScrollChanged;
        std::function<void(bool)> prettyJsonChanged;
        std::function<void(const RequestLogEntry &)> replay;
        std::function<void(const QUuid &)> openRequest;
        std::function<void(const QUuid &)> toggleRequest;
        std::function<bool(const QUuid &)> requestBusy;
        std::function<void()> newRequest;
    };

    CollectionTab(const CollectionDefinition &collection,
                  const QVector<RequestLogEntry> &entries,
                  const ApplicationSettings &settings, Actions actions,
                  QWidget *parent = nullptr)
        : QWidget(parent)
        , m_collection(collection)
        , m_actions(std::move(actions))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *settingsCard = card(this);
        settingsCard->setObjectName(QStringLiteral("collectionEditorCard"));
        auto *settingsLayout = new QVBoxLayout(settingsCard);
        settingsLayout->setContentsMargins(20, 14, 20, 12);
        settingsLayout->setSpacing(10);
        auto *header = new QHBoxLayout;
        m_title = new EditableTitle(collection.name, [this](const QString &name) {
            m_collection.name = name;
            if (m_actions.changed) m_actions.changed(m_collection);
        }, settingsCard);
        header->addWidget(m_title, 0, Qt::AlignLeft | Qt::AlignVCenter);
        header->addStretch();
        m_summary = new QLabel(settingsCard);
        m_summary->setObjectName(QStringLiteral("subtitleLabel"));
        settingsLayout->addLayout(header);
        auto *editorTabs = new QTabBar(settingsCard);
        editorTabs->setObjectName(QStringLiteral("editorTabs"));
        editorTabs->setExpanding(false);
        editorTabs->setDrawBase(false);
        editorTabs->setIconSize(QSize(14, 14));
        editorTabs->setProperty("tabIconNames", QStringList{QStringLiteral("activity"), QStringLiteral("settings")});
        editorTabs->addTab(AppTheme::icon(u"activity"), UiLanguage::text("Запросы"));
        editorTabs->addTab(AppTheme::icon(u"settings"), UiLanguage::text("Настройки коллекции"));
        auto *pages = new ActivePageStack(settingsCard);
        auto *requestsPage = new QWidget(pages);
        auto *requestsLayout = new QVBoxLayout(requestsPage);
        requestsLayout->setContentsMargins(0, 0, 0, 0);
        requestsLayout->setSpacing(6);
        pages->addWidget(requestsPage);
        auto *parametersPage = new QWidget(pages);
        auto *parametersLayout = new QVBoxLayout(parametersPage);
        parametersLayout->setContentsMargins(0, 6, 0, 0);
        parametersLayout->setSpacing(14);
        pages->addWidget(parametersPage);
        parametersLayout->addWidget(m_summary);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->addWidget(fieldLabel(UiLanguage::text("АДРЕС"), settingsCard), 0, 0);
        grid->addWidget(fieldLabel(UiLanguage::text("ПОРТ"), settingsCard), 0, 1);
        grid->addWidget(fieldLabel(UiLanguage::text("ИНТЕРВАЛ, МС"), settingsCard), 0, 2);
        grid->addWidget(fieldLabel(UiLanguage::text("ПАРАЛЛЕЛЬНО НА ЗАПРОС"), settingsCard), 0, 3);
        m_host = new QLineEdit(settingsCard);
        m_host->setLayoutDirection(Qt::LeftToRight);
        m_port = new QSpinBox(settingsCard);
        m_port->setRange(1, 65535);
        m_interval = new QSpinBox(settingsCard);
        m_interval->setRange(1, 86'400'000);
        m_interval->setGroupSeparatorShown(true);
        m_parallel = new QSpinBox(settingsCard);
        m_parallel->setRange(1, 100'000);
        m_parallel->setGroupSeparatorShown(true);
        grid->addWidget(m_host, 1, 0);
        grid->addWidget(m_port, 1, 1);
        grid->addWidget(m_interval, 1, 2);
        grid->addWidget(m_parallel, 1, 3);
        grid->setColumnStretch(0, 3);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(2, 2);
        grid->setColumnStretch(3, 2);
        parametersLayout->addLayout(grid);
        auto *inheritanceHelp = new QLabel(UiLanguage::text("Общие параметры для запросов коллекции. Каждый запрос может переопределить их в своих настройках."), parametersPage);
        inheritanceHelp->setObjectName(QStringLiteral("helperText"));
        inheritanceHelp->setWordWrap(true);
        parametersLayout->addWidget(inheritanceHelp);

        auto *actionLayout = new QHBoxLayout;
        m_start = new QPushButton(UiLanguage::text("Запустить всё"), settingsCard);
        m_start->setObjectName(QStringLiteral("primaryButton"));
        actionLayout->addWidget(m_start);
        buttonIcon(m_start, QStringLiteral("play"), QStringLiteral("accent"));
        m_stop = new QPushButton(UiLanguage::text("Стоп"), settingsCard);
        m_stop->setObjectName(QStringLiteral("dangerButton"));
        actionLayout->addWidget(m_stop);
        buttonIcon(m_stop, QStringLiteral("stop"), QStringLiteral("red"));
        m_status = new QLabel(UiLanguage::text("● Остановлено"), settingsCard);
        m_status->setObjectName(QStringLiteral("statusPill"));
        actionLayout->addWidget(m_status);
        header->addLayout(actionLayout);
        settingsLayout->addWidget(editorTabs);

        auto *stats = new QHBoxLayout;
        stats->setSpacing(28);
        counterLabel(UiLanguage::text("ОТПРАВЛЕНО"), &m_sent, settingsCard, stats);
        counterLabel(UiLanguage::text("ЗАВЕРШЕНО"), &m_completed, settingsCard, stats);
        counterLabel(UiLanguage::text("ОШИБКИ"), &m_errors, settingsCard, stats);
        counterLabel(UiLanguage::text("ПРОПУЩЕНО"), &m_skipped, settingsCard, stats);
        counterLabel(UiLanguage::text("АКТИВНО"), &m_active, settingsCard, stats);
        stats->addStretch();
        parametersLayout->addLayout(stats);
        parametersLayout->addStretch();
        auto *requestRow = new QHBoxLayout;
        requestRow->addWidget(fieldLabel(UiLanguage::text("ЗАПРОСЫ КОЛЛЕКЦИИ"), settingsCard));
        requestRow->addStretch();
        auto *newRequest = new QPushButton(UiLanguage::text("Добавить"), settingsCard);
        newRequest->setObjectName(QStringLiteral("compactButton"));
        requestRow->addWidget(newRequest);
        auto *openRequest = new QPushButton(UiLanguage::text("Открыть"), settingsCard);
        openRequest->setObjectName(QStringLiteral("compactButton"));
        requestRow->addWidget(openRequest);
        m_toggleRequest = new QPushButton(UiLanguage::text("Запустить"), settingsCard);
        m_toggleRequest->setObjectName(QStringLiteral("compactButton"));
        requestRow->addWidget(m_toggleRequest);
        requestsLayout->addLayout(requestRow);
        m_requests = new QListWidget(settingsCard);
        m_requests->setObjectName(QStringLiteral("collectionRequests"));
        m_requests->setAccessibleName(UiLanguage::text("Запросы коллекции"));
        m_requests->setItemDelegate(new CollectionRequestDelegate(m_requests));
        m_requests->setMinimumHeight(100);
        m_requests->setMaximumHeight(200);
        m_requests->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        requestsLayout->addWidget(m_requests, 1);
        settingsLayout->addWidget(pages, 1);
        connect(editorTabs, &QTabBar::currentChanged, this, [pages](int index) {
            pages->setCurrentIndex(index);
            pages->updateGeometry();
        });
        connect(newRequest, &QPushButton::clicked, this, [this] { if (m_actions.newRequest) m_actions.newRequest(); });
        const auto openSelected = [this] {
            if (const auto *item = m_requests->currentItem(); item && m_actions.openRequest)
                m_actions.openRequest(item->data(Qt::UserRole).toUuid());
        };
        connect(openRequest, &QPushButton::clicked, this, openSelected);
        connect(m_requests, &QListWidget::itemDoubleClicked, this, openSelected);
        connect(m_requests, &QListWidget::currentRowChanged, this, [this, openRequest] {
            openRequest->setEnabled(m_requests->currentItem() != nullptr);
            updateRequestButton();
        });
        openRequest->setEnabled(false);
        connect(m_toggleRequest, &QPushButton::clicked, this, [this] {
            if (const auto *item = m_requests->currentItem(); item && m_actions.toggleRequest)
                m_actions.toggleRequest(item->data(Qt::UserRole).toUuid());
        });
        auto *settingsScroll = new QScrollArea(this);
        settingsScroll->setWidgetResizable(true);
        settingsScroll->setFrameShape(QFrame::NoFrame);
        settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        settingsScroll->setMinimumHeight(200);
        settingsScroll->setWidget(settingsCard);
        auto *splitter = new ThinSplitter(Qt::Vertical, this);
        splitter->addWidget(settingsScroll);
        layout->addWidget(splitter, 1);

        m_logModel = new RequestLogModel(this);
        m_logModel->setMaximumEntries(0);
        m_logModel->replaceEntries(entries);
        m_logPane = new LogPane(m_logModel, settings.logDetailsVerticalSplitterState,
            settings.autoScroll, settings.prettyJson,
            [this](const QByteArray &state) {
                if (m_actions.splitterChanged) {
                    m_actions.splitterChanged(state);
                }
            }, m_actions.autoScrollChanged, m_actions.prettyJsonChanged, this);
        m_logPane->setClearAction(m_actions.clearLog);
        m_logPane->setReplayAction(m_actions.replay);
        splitter->addWidget(m_logPane);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({330, 480});
        if (!settings.collectionVerticalSplitterState.isEmpty()) restoreSplitterState(splitter, settings.collectionVerticalSplitterState);
        connect(splitter, &QSplitter::splitterMoved, this, [this, splitter] {
            if (m_actions.editorSplitterChanged) m_actions.editorSplitterChanged(splitter->saveState());
        });
        connect(m_start, &QPushButton::clicked, this, [this] { m_actions.start(); });
        connect(m_stop, &QPushButton::clicked, this, [this] { m_actions.stop(); });
        const auto changed = [this] {
            if (m_updating) return;
            m_collection.host = m_host->text().trimmed();
            m_collection.port = static_cast<quint16>(m_port->value());
            m_collection.intervalMs = m_interval->value();
            m_collection.maxInFlight = m_parallel->value();
            updateSummary();
            if (m_actions.changed) m_actions.changed(m_collection);
        };
        connect(m_host, &QLineEdit::textChanged, this, changed);
        connect(m_port, &QSpinBox::valueChanged, this, changed);
        connect(m_interval, &QSpinBox::valueChanged, this, changed);
        connect(m_parallel, &QSpinBox::valueChanged, this, changed);
        setDefinition(collection);
        m_logPane->modelReplaced();
    }

    void setDefinition(const CollectionDefinition &collection)
    {
        m_updating = true;
        m_collection = collection;
        m_title->setText(collection.name);
        m_host->setText(collection.host);
        m_port->setValue(collection.port);
        m_interval->setValue(collection.intervalMs);
        m_parallel->setValue(collection.maxInFlight);
        m_updating = false;
        updateSummary();
        refreshRequests();
    }

    void setEntries(const QVector<RequestLogEntry> &entries)
    {
        m_logModel->replaceEntries(entries);
        m_logPane->modelReplaced();
    }

    void entryChanged(const QUuid &entryId) { m_logPane->entryChanged(entryId); }
    void setPrettyJson(bool enabled) { m_logPane->setPrettyJson(enabled); }

    void updateRuntime(const RequestCounters &counters, int runningCount, int requestCount)
    {
        m_sent->setText(QString::number(counters.sent));
        m_completed->setText(QString::number(counters.completed));
        m_errors->setText(QString::number(counters.errors));
        m_skipped->setText(QString::number(counters.skipped));
        m_active->setText(QString::number(counters.active));
        m_stop->setEnabled(runningCount > 0 || counters.active > 0);
        m_stop->setVisible(runningCount > 0 || counters.active > 0);
        m_start->setEnabled(requestCount > 0 && runningCount < requestCount);
        m_status->setText(runningCount > 0
            ? UiLanguage::text("● Работает %1 из %2").arg(runningCount).arg(requestCount)
            : UiLanguage::text("● Остановлено"));
        m_status->setProperty("mode", runningCount > 0 ? QStringLiteral("running")
                                                        : QStringLiteral("idle"));
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
        m_logPane->setClearEnabled(counters.active == 0);
        refreshRequests();
    }

private:
    void updateRequestButton()
    {
        const auto *item = m_requests->currentItem();
        m_toggleRequest->setEnabled(item != nullptr);
        const bool busy = item && m_actions.requestBusy && m_actions.requestBusy(item->data(Qt::UserRole).toUuid());
        m_toggleRequest->setText(busy ? UiLanguage::text("Остановить") : UiLanguage::text("Запустить"));
    }

    void refreshRequests()
    {
        const QUuid selected = m_requests->currentItem() ? m_requests->currentItem()->data(Qt::UserRole).toUuid() : QUuid{};
        bool rebuild = m_requests->count() != m_collection.requests.size();
        for (int i = 0; !rebuild && i < m_requests->count(); ++i)
            rebuild = m_requests->item(i)->data(Qt::UserRole).toUuid() != m_collection.requests.at(i).id;
        if (rebuild) m_requests->clear();
        for (int i = 0; i < m_collection.requests.size(); ++i) {
            const auto &request = m_collection.requests.at(i);
            auto *item = rebuild ? new QListWidgetItem(m_requests) : m_requests->item(i);
            item->setData(Qt::UserRole, request.id);
            const bool busy = m_actions.requestBusy && m_actions.requestBusy(request.id);
            const QString address = buildRequestUrl(resolveRequestConfig(m_collection, request)).toString();
            item->setText(request.name);
            item->setData(Qt::UserRole + 1, requestOperationName(request.protocol, request.method));
            item->setData(Qt::UserRole + 2, address);
            item->setData(Qt::UserRole + 3, busy);
            item->setToolTip(QStringLiteral("%1\n%2").arg(request.name, address));
            item->setForeground(AppTheme::color(busy ? u"accent" : u"text"));
            if (request.id == selected) m_requests->setCurrentItem(item);
        }
        if (!m_requests->currentItem() && m_requests->count()) m_requests->setCurrentRow(0);
        updateRequestButton();
    }

    void updateSummary()
    {
        m_summary->setText(UiLanguage::text("%1:%2 · %3 запросов")
                               .arg(m_collection.host)
                               .arg(m_collection.port)
                               .arg(m_collection.requests.size()));
    }

    CollectionDefinition m_collection;
    Actions m_actions;
    RequestLogModel *m_logModel = nullptr;
    LogPane *m_logPane = nullptr;
    EditableTitle *m_title = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QSpinBox *m_interval = nullptr;
    QSpinBox *m_parallel = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_sent = nullptr;
    QLabel *m_completed = nullptr;
    QLabel *m_errors = nullptr;
    QLabel *m_skipped = nullptr;
    QLabel *m_active = nullptr;
    QListWidget *m_requests = nullptr;
    QPushButton *m_toggleRequest = nullptr;
    bool m_updating = false;
};

struct MainWindow::RequestSession
{
    RequestController *controller = nullptr;
    RequestLogModel *log = nullptr;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_uiSettings = m_settingsStore.load();
    setupUi();
    setupConnections();
    setupShortcuts();

    m_workspaceSaveTimer = new QTimer(this);
    m_workspaceSaveTimer->setSingleShot(true);
    m_workspaceSaveTimer->setInterval(350);
    connect(m_workspaceSaveTimer, &QTimer::timeout, this, [this] {
        saveWorkspace();
        saveUiSettings();
    });
    m_historySaveTimer = new QTimer(this);
    m_historySaveTimer->setSingleShot(true);
    m_historySaveTimer->setInterval(700);
    connect(m_historySaveTimer, &QTimer::timeout, this, &MainWindow::saveHistory);

    loadState();
}

MainWindow::~MainWindow()
{
    for (RequestSession *value : std::as_const(m_sessions)) {
        value->controller->stop();
        delete value;
    }
    m_sessions.clear();
}

QString MainWindow::applicationStyleSheet()
{
    return AppTheme::styleSheet();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                               QCoreApplication::applicationVersion()));
    setMinimumSize(1100, 720);
    resize(1420, 900);
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *appHeader = new QFrame(central);
    appHeader->setObjectName(QStringLiteral("appHeader"));
    auto *header = new QHBoxLayout(appHeader);
    header->setContentsMargins(16, 7, 16, 7);
    header->setSpacing(12);
    auto *mark = new QLabel(QStringLiteral("M"), appHeader);
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setFixedSize(32, 32);
    mark->setAlignment(Qt::AlignCenter);
    header->addWidget(mark);
    auto *brand = new QLabel(QStringLiteral("MultiSender"), appHeader);
    brand->setObjectName(QStringLiteral("appTitle"));
    header->addWidget(brand);
    auto *edition = new QLabel(QStringLiteral("QT EDITION"), appHeader);
    edition->setObjectName(QStringLiteral("appEdition"));
    header->addWidget(edition);
    header->addStretch();
    auto *workspaceCaption = new QLabel(UiLanguage::text("●  Рабочее пространство  /  Локально"), appHeader);
    workspaceCaption->setObjectName(QStringLiteral("workspaceCaption"));
    header->addWidget(workspaceCaption);
    header->addStretch();
    auto *workspace = new QPushButton(UiLanguage::text("Действия"), appHeader);
    workspace->setObjectName(QStringLiteral("compactButton"));
    auto *workspaceMenu = new QMenu(workspace);
    workspaceMenu->addAction(UiLanguage::text("Импорт коллекций…"), this, &MainWindow::importWorkspace);
    workspaceMenu->addAction(UiLanguage::text("Экспорт всех коллекций…"), this, [this] { exportWorkspace(); });
    workspaceMenu->addSeparator();
    workspaceMenu->addAction(UiLanguage::text("Сохранить всё · Ctrl+S"), this, &MainWindow::saveAll);
    workspaceMenu->addAction(UiLanguage::text("Открыть папку данных"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_workspaceStore.workspacePath()).absolutePath()));
    });
    workspace->setMenu(workspaceMenu);
    header->addWidget(workspace);
    auto *languageButton = new QPushButton(UiLanguage::current().toUpper(), appHeader);
    languageButton->setObjectName(QStringLiteral("languageButton"));
    languageButton->setToolTip(UiLanguage::text("Язык интерфейса"));
    languageButton->setAccessibleName(UiLanguage::text("Язык интерфейса"));
    auto *languageMenu = new QMenu(languageButton);
    auto *languageGroup = new QActionGroup(languageMenu);
    for (const auto &code : UiLanguage::available()) {
        auto *action = languageMenu->addAction(UiLanguage::nativeName(code));
        action->setData(code);
        action->setCheckable(true);
        action->setChecked(code == UiLanguage::current());
        languageGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, code, languageGroup] {
            if (!changeLanguage(code))
                for (auto *item : languageGroup->actions()) item->setChecked(item->data().toString() == UiLanguage::current());
        });
    }
    languageButton->setMenu(languageMenu);
    header->addWidget(languageButton);
    m_stopAllButton = new QPushButton(UiLanguage::text("Стоп всё"), appHeader);
    m_stopAllButton->setObjectName(QStringLiteral("dangerButton"));
    buttonIcon(m_stopAllButton, QStringLiteral("stop"), QStringLiteral("red"));
    connect(m_stopAllButton, &QPushButton::clicked, this, &MainWindow::stopAll);
    header->addWidget(m_stopAllButton);
    header->addSpacing(12);
    m_themeButton = new QPushButton(appHeader);
    m_themeButton->setObjectName(QStringLiteral("themeButton"));
    m_themeButton->setAccessibleName(UiLanguage::text("Переключить тему"));
    header->addWidget(m_themeButton);
    updateThemeButton();
    layout->addWidget(appHeader);

    m_workspaceSplitter = new ThinSplitter(Qt::Horizontal, central);
    auto *sidebar = new QWidget(m_workspaceSplitter);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setMinimumWidth(235);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 18, 10, 12);
    sidebarLayout->setSpacing(12);
    m_workspaceHome = new QPushButton(UiLanguage::text("Моё пространство"), sidebar);
    m_workspaceHome->setObjectName(QStringLiteral("workspaceHomeButton"));
    m_workspaceHome->setCheckable(true);
    buttonIcon(m_workspaceHome, QStringLiteral("home"));
    m_workspaceHome->setToolTip(UiLanguage::text("Обзор коллекций и общий журнал"));
    connect(m_workspaceHome, &QPushButton::clicked, this, &MainWindow::showOverview);
    sidebarLayout->addWidget(m_workspaceHome);
    auto *workspaceHint = new QLabel(UiLanguage::text("Коллекции и подключения"), sidebar);
    workspaceHint->setObjectName(QStringLiteral("workspaceHomeCaption"));
    sidebarLayout->addWidget(workspaceHint);
    auto *captionRow = new QHBoxLayout;
    auto *caption = new QLabel(UiLanguage::text("КОЛЛЕКЦИИ"), sidebar);
    caption->setObjectName(QStringLiteral("sidebarSection"));
    captionRow->addWidget(caption);
    m_collectionCount = new QLabel(sidebar);
    m_collectionCount->setObjectName(QStringLiteral("countBadge"));
    captionRow->addWidget(m_collectionCount);
    captionRow->addStretch();
    m_addCollectionButton = new QPushButton(sidebar);
    m_addCollectionButton->setObjectName(QStringLiteral("toolbarButton"));
    m_addCollectionButton->setToolTip(UiLanguage::text("Создать коллекцию"));
    m_addCollectionButton->setAccessibleName(UiLanguage::text("Создать коллекцию"));
    buttonIcon(m_addCollectionButton, QStringLiteral("folder-plus"));
    captionRow->addWidget(m_addCollectionButton);
    m_search = new QLineEdit(sidebar);
    m_search->setObjectName(QStringLiteral("sidebarSearch"));
    m_search->setPlaceholderText(UiLanguage::text("Найти запрос…"));
    m_search->setAccessibleName(UiLanguage::text("Поиск по коллекциям и запросам"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(AppTheme::icon(u"search"), QLineEdit::LeadingPosition);
    m_search->setToolTip(UiLanguage::text("Поиск по имени, протоколу или адресу · Ctrl+K"));
    sidebarLayout->addWidget(m_search);
    sidebarLayout->addSpacing(6);
    sidebarLayout->addLayout(captionRow);
    m_addRequestButton = new QPushButton(UiLanguage::text("Новый запрос"), sidebar);
    m_addRequestButton->setObjectName(QStringLiteral("breadcrumbButton"));
    buttonIcon(m_addRequestButton, QStringLiteral("plus"));
    m_tree = new CollectionTreeWidget(sidebar);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({UiLanguage::text("Запрос"), UiLanguage::text("Тип")});
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Keep protocol data available to filtering while painting its badge beside the name.
    m_tree->setColumnHidden(1, true);
    m_tree->setItemDelegateForColumn(0, new RequestNameDelegate(m_tree));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    sidebarLayout->addWidget(m_tree, 1);
    sidebarLayout->addWidget(m_addRequestButton);
    m_tree->setIndentation(16);
    m_tree->setAnimated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_searchEmpty = new QLabel(UiLanguage::text("Ничего не найдено\nПопробуйте другой запрос"), sidebar);
    m_searchEmpty->setObjectName(QStringLiteral("emptyHint"));
    m_searchEmpty->setAlignment(Qt::AlignCenter);
    m_searchEmpty->setWordWrap(true);
    m_searchEmpty->hide();
    sidebarLayout->addWidget(m_searchEmpty);
    auto *transfers = new QHBoxLayout;
    auto *import = new QPushButton(UiLanguage::text("Импорт"), sidebar);
    import->setObjectName(QStringLiteral("transferButton"));
    buttonIcon(import, QStringLiteral("download"));
    connect(import, &QPushButton::clicked, this, &MainWindow::importWorkspace);
    transfers->addWidget(import);
    auto *exportAll = new QPushButton(UiLanguage::text("Экспорт"), sidebar);
    exportAll->setObjectName(QStringLiteral("transferButton"));
    buttonIcon(exportAll, QStringLiteral("upload"));
    connect(exportAll, &QPushButton::clicked, this, [this] { exportWorkspace(); });
    transfers->addWidget(exportAll);
    sidebarLayout->addLayout(transfers);
    auto *dataDirectory = new QPushButton(sidebar);
    dataDirectory->setObjectName(QStringLiteral("dataDirectoryButton"));
    dataDirectory->setMinimumHeight(68);
    dataDirectory->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    dataDirectory->setAccessibleName(UiLanguage::text("Данные на этом компьютере"));
    dataDirectory->setCursor(Qt::PointingHandCursor);
    auto *directoryLayout = new QHBoxLayout(dataDirectory);
    directoryLayout->setContentsMargins(7, 10, 7, 10);
    directoryLayout->setSpacing(10);
    directoryLayout->addWidget(new ThemeIcon(QStringLiteral("database"), dataDirectory));
    auto *directoryText = new QVBoxLayout;
    directoryText->setSpacing(4);
    auto *directoryTitle = new QLabel(UiLanguage::text("Данные на этом компьютере"), dataDirectory);
    directoryTitle->setObjectName(QStringLiteral("dataDirectoryTitle"));
    auto *directoryHint = new QLabel(UiLanguage::text("Открыть папку коллекций и истории"), dataDirectory);
    directoryHint->setObjectName(QStringLiteral("dataDirectoryHint"));
    for (auto *label : {directoryTitle, directoryHint}) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setWordWrap(true);
        directoryText->addWidget(label);
    }
    directoryLayout->addLayout(directoryText, 1);
    directoryLayout->addWidget(new ThemeIcon(QStringLiteral("external"), dataDirectory));
    dataDirectory->setToolTip(UiLanguage::text("Открыть папку коллекций и истории"));
    connect(dataDirectory, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_workspaceStore.workspacePath()).absolutePath()));
    });
    sidebarLayout->addWidget(dataDirectory);
    m_workspaceSplitter->addWidget(sidebar);

    m_workspacePages = new QStackedWidget(m_workspaceSplitter);
    m_tabs = new QTabWidget(m_workspacePages);
    m_tabs->setObjectName(QStringLiteral("workspaceTabs"));
    m_tabs->tabBar()->setObjectName(QStringLiteral("workspaceTabBar"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setMinimumWidth(700);
    m_workspacePages->addWidget(m_tabs);
    auto *empty = new QWidget(m_workspacePages);
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(0);
    auto *overviewCard = card(empty);
    overviewCard->setObjectName(QStringLiteral("overviewHero"));
    auto *overviewLayout = new QVBoxLayout(overviewCard);
    overviewLayout->setContentsMargins(24, 22, 24, 18);
    overviewLayout->setSpacing(12);
    auto *eyebrow = new QLabel(QStringLiteral("HTTP  /  TCP  /  UDP  /  COM"), overviewCard);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    overviewLayout->addWidget(eyebrow);
    auto *overviewRow = new QHBoxLayout;
    auto *emptyTitle = new QLabel(UiLanguage::text("Все подключения под рукой."), overviewCard);
    emptyTitle->setObjectName(QStringLiteral("overviewTitle"));
    overviewRow->addWidget(emptyTitle);
    overviewRow->addStretch();
    auto *create = new QPushButton(UiLanguage::text("Новый запрос"), overviewCard);
    create->setObjectName(QStringLiteral("sendButton"));
    buttonIcon(create, QStringLiteral("plus"), QStringLiteral("onAccent"));
    overviewRow->addWidget(create);
    overviewLayout->addLayout(overviewRow);
    m_overviewSummary = new QLabel(overviewCard);
    m_overviewSummary->setObjectName(QStringLiteral("subtitleLabel"));
    m_overviewSummary->setWordWrap(true);
    overviewLayout->addWidget(m_overviewSummary);
    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(24);
    for (const auto &caption : {UiLanguage::text("Коллекции"), UiLanguage::text("Запросы"), UiLanguage::text("В цикле"), UiLanguage::text("Записи истории")}) {
        auto *container = new QWidget(overviewCard);
        container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *metric = new QVBoxLayout(container);
        metric->setContentsMargins(0, 0, 0, 0);
        metric->setSpacing(3);
        auto *value = new QLabel(QStringLiteral("0"), overviewCard);
        value->setObjectName(QStringLiteral("overviewMetricValue"));
        m_overviewMetrics.append(value);
        metric->addWidget(value);
        auto *label = new QLabel(caption, overviewCard);
        label->setObjectName(QStringLiteral("overviewMetricLabel"));
        metric->addWidget(label);
        metrics->addWidget(container, 1);
    }
    overviewLayout->addLayout(metrics);
    emptyLayout->addWidget(overviewCard);
    connect(create, &QPushButton::clicked, this, [this] { addRequest(); });
    m_overviewModel = new RequestLogModel(this);
    m_overviewModel->setMaximumEntries(0);
    m_overviewLog = new LogPane(m_overviewModel, m_uiSettings.logDetailsVerticalSplitterState,
        m_uiSettings.autoScroll, m_uiSettings.prettyJson,
        [this](const QByteArray &state) {
            m_uiSettings.logDetailsVerticalSplitterState = state;
            scheduleWorkspaceSave();
        }, [this](bool checked) { m_uiSettings.autoScroll = checked; scheduleWorkspaceSave(); },
        [this](bool checked) {
            m_uiSettings.prettyJson = checked;
            for (auto *tab : std::as_const(m_requestTabs)) tab->setPrettyJson(checked);
            for (auto *tab : std::as_const(m_collectionTabs)) tab->setPrettyJson(checked);
            scheduleWorkspaceSave();
        }, empty);
    m_overviewLog->setClearAction([this] { clearAllLogs(); });
    m_overviewLog->setReplayAction([this](const RequestLogEntry &entry) { replayEntry(entry); });
    emptyLayout->addWidget(m_overviewLog, 1);
    m_workspacePages->addWidget(empty);
    m_workspacePages->setCurrentWidget(empty);
    m_workspaceHome->setChecked(true);
    connect(m_workspacePages, &QStackedWidget::currentChanged, this, [this](int index) {
        m_workspaceHome->setChecked(index == 1);
    });
    m_workspaceSplitter->addWidget(m_workspacePages);
    m_workspaceSplitter->setStretchFactor(0, 0);
    m_workspaceSplitter->setStretchFactor(1, 1);
    m_workspaceSplitter->setSizes({260, 1160});
    layout->addWidget(m_workspaceSplitter, 1);
    statusBar()->setSizeGripEnabled(false);
    m_saveStatus = new QLabel(UiLanguage::text("●  Всё сохранено"), this);
    m_saveStatus->setObjectName(QStringLiteral("savedStatus"));
    statusBar()->addWidget(m_saveStatus);
    auto *keys = new QLabel(UiLanguage::text("Ctrl+Enter  отправить     F5  цикл     Ctrl+K  поиск"), this);
    keys->setObjectName(QStringLiteral("statusText"));
    statusBar()->addPermanentWidget(keys);
    auto *version = new QLabel(QStringLiteral("   v%1").arg(QCoreApplication::applicationVersion()), this);
    version->setObjectName(QStringLiteral("statusText"));
    statusBar()->addPermanentWidget(version);
}

void MainWindow::updateThemeButton()
{
    const bool dark = AppTheme::isDark();
    m_themeButton->setText({});
    m_themeButton->setFixedSize(32, 32);
    m_themeButton->setToolTip((dark ? UiLanguage::text("Светлая тема") : UiLanguage::text("Тёмная тема")) + QStringLiteral(" · Ctrl+Shift+T"));
    buttonIcon(m_themeButton, dark ? QStringLiteral("sun") : QStringLiteral("moon"));
}

void MainWindow::toggleTheme()
{
    const auto theme = AppTheme::isDark() ? AppTheme::Theme::Light : AppTheme::Theme::Dark;
    AppTheme::apply(theme);
    m_uiSettings.theme = AppTheme::themeName(theme);
    updateThemeButton();
    refreshTreeStatuses();
    saveUiSettings();
}

void MainWindow::filterTree()
{
    const QString query = m_search->text().trimmed();
    m_filtering = true;
    int visible = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *parent = m_tree->topLevelItem(i);
        const bool parentMatches = parent->text(0).contains(query, Qt::CaseInsensitive);
        bool childMatches = false;
        for (int j = 0; j < parent->childCount(); ++j) {
            auto *child = parent->child(j);
            const CollectionDefinition *owner = nullptr;
            const auto *definition = request(idFromItem(child), &owner);
            const QString address = definition && owner ? buildRequestUrl(resolveRequestConfig(*owner, *definition)).toString() : QString{};
            const bool matches = query.isEmpty() || parentMatches
                || child->text(0).contains(query, Qt::CaseInsensitive)
                || child->text(1).contains(query, Qt::CaseInsensitive)
                || address.contains(query, Qt::CaseInsensitive);
            child->setHidden(!matches);
            childMatches |= matches;
        }
        const bool matches = query.isEmpty() || parentMatches || childMatches;
        parent->setHidden(!matches);
        if (matches) ++visible;
        if (!query.isEmpty() && childMatches) parent->setExpanded(true);
        else if (query.isEmpty() && m_searchActive)
            parent->setExpanded(m_searchExpanded.contains(idFromItem(parent).toString(QUuid::WithoutBraces)));
    }
    m_searchEmpty->setVisible(visible == 0 && !query.isEmpty());
    m_tree->setDragDropMode(query.isEmpty() ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    m_filtering = false;
}

void MainWindow::setupConnections()
{
    connect(m_addCollectionButton, &QPushButton::clicked, this, &MainWindow::addCollection);
    connect(m_addRequestButton, &QPushButton::clicked, this, [this] { addRequest(); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_themeButton, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty() && !m_searchActive) {
            m_searchExpanded.clear();
            for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
                if (m_tree->topLevelItem(i)->isExpanded())
                    m_searchExpanded.append(idFromItem(m_tree->topLevelItem(i)).toString(QUuid::WithoutBraces));
            m_searchActive = true;
        }
        filterTree();
        if (text.trimmed().isEmpty()) { m_searchActive = false; m_searchExpanded.clear(); }
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        m_workspacePages->setCurrentIndex(m_tabs->count() ? 0 : 1);
        scheduleWorkspaceSave();
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (typeFromItem(item) == ItemType::Collection) openCollection(idFromItem(item));
        else openRequest(idFromItem(item));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::showTreeContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_rebuildingTree || column != 0) return;
        const QString name = item->text(0).trimmed();
        if (typeFromItem(item) == ItemType::Collection) {
            if (CollectionDefinition *value = collection(idFromItem(item))) {
                value->name = name.isEmpty() ? UiLanguage::text("Без имени") : name;
                refreshCollectionTab(value->id);
                for (const RequestDefinition &child : value->requests) refreshRequestTab(child.id);
            }
        } else if (RequestDefinition *value = request(idFromItem(item))) {
            value->name = name.isEmpty() ? UiLanguage::text("Без имени") : name;
            refreshRequestTab(value->id);
        }
        if (m_searchActive) filterTree();
        scheduleWorkspaceSave();
    });
    connect(m_tree->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (!m_rebuildingTree) applyTreeOrder();
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this] { if (!m_filtering) scheduleWorkspaceSave(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this] { if (!m_filtering) scheduleWorkspaceSave(); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (QTreeWidgetItem *current = m_tree->currentItem())
            m_uiSettings.selectedItem = objectKey(typeFromItem(current), idFromItem(current));
        scheduleWorkspaceSave();
    });
    connect(m_workspaceSplitter, &QSplitter::splitterMoved, this, [this] {
        m_uiSettings.workspaceSplitterState = m_workspaceSplitter->saveState();
        scheduleWorkspaceSave();
    });
}

void MainWindow::setupShortcuts()
{
    auto *save = new QShortcut(QKeySequence::Save, this);
    connect(save, &QShortcut::activated, this, &MainWindow::saveAll);
    auto *search = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(search, &QShortcut::activated, this, [this] { m_search->setFocus(); m_search->selectAll(); });
    auto *theme = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")), this);
    connect(theme, &QShortcut::activated, this, &MainWindow::toggleTheme);
    auto *send = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), this);
    connect(send, &QShortcut::activated, this, [this] {
        if (m_workspacePages->currentIndex() != 0) return;
        QWidget *widget = m_tabs->currentWidget();
        if (widget && widget->property("kind").toString() == QStringLiteral("request"))
            sendOnce(QUuid(widget->property("id").toString()));
    });
    auto *sendKeypad = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Enter")), this);
    connect(sendKeypad, &QShortcut::activated, send, &QShortcut::activated);
    auto *run = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(run, &QShortcut::activated, this, [this] {
        if (m_workspacePages->currentIndex() != 0) return;
        QWidget *widget = m_tabs->currentWidget();
        if (!widget) return;
        const QUuid id(widget->property("id").toString());
        if (widget->property("kind").toString() == QStringLiteral("request")) {
            RequestSession *value = session(id);
            if (value && (value->controller->isRunning() || value->controller->counters().active > 0)) stopRequest(id); else startRequest(id);
        } else {
            const CollectionDefinition *value = collection(id);
            bool anyRunning = false;
            if (value) for (const RequestDefinition &request : value->requests)
                anyRunning = anyRunning || session(request.id)->controller->isRunning()
                    || session(request.id)->controller->counters().active > 0;
            if (anyRunning) stopCollection(id); else startCollection(id);
        }
    });
    auto *focusTarget = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(focusTarget, &QShortcut::activated, this, [this] {
        if (m_workspacePages->currentIndex() != 0) return;
        QWidget *widget = m_tabs->currentWidget();
        if (widget && widget->property("kind").toString() == QStringLiteral("request")) {
            if (RequestTab *tab = m_requestTabs.value(QUuid(widget->property("id").toString())))
                tab->focusTarget();
        }
    });
}

void MainWindow::loadState()
{
    m_uiSettings = m_settingsStore.load();
    QString workspaceError;
    m_workspace = m_workspaceStore.loadWorkspace(m_uiSettings.request, &workspaceError);
    if (!workspaceError.isEmpty()) showStorageError(workspaceError);
    QString historyError;
    m_loadedHistory = m_workspaceStore.loadHistory(&historyError);
    if (!historyError.isEmpty()) showStorageError(historyError);
    for (auto &entries : m_loadedHistory) {
        for (auto &entry : entries) {
            if (entry.result == RequestResult::Pending) {
                entry.result = RequestResult::Cancelled;
                entry.completedAt = QDateTime::currentDateTime();
                entry.errorText = UiLanguage::text("Выполнение прервано при закрытии приложения");
            }
        }
    }
    rebuildSessions();
    rebuildTree();
    if (!m_uiSettings.windowGeometry.isEmpty()) restoreGeometry(m_uiSettings.windowGeometry);
    if (!m_uiSettings.workspaceSplitterState.isEmpty())
        restoreSplitterState(m_workspaceSplitter, m_uiSettings.workspaceSplitterState);
    restoreTabs();
    refreshTreeStatuses();
    refreshOverview(true);
}

CollectionDefinition *MainWindow::collection(const QUuid &id)
{
    for (CollectionDefinition &value : m_workspace.collections)
        if (value.id == id) return &value;
    return nullptr;
}

const CollectionDefinition *MainWindow::collection(const QUuid &id) const
{
    for (const CollectionDefinition &value : m_workspace.collections)
        if (value.id == id) return &value;
    return nullptr;
}

RequestDefinition *MainWindow::request(const QUuid &id, CollectionDefinition **owner)
{
    for (CollectionDefinition &collection : m_workspace.collections) {
        for (RequestDefinition &value : collection.requests) {
            if (value.id == id) {
                if (owner) *owner = &collection;
                return &value;
            }
        }
    }
    return nullptr;
}

const RequestDefinition *MainWindow::request(const QUuid &id,
                                             const CollectionDefinition **owner) const
{
    for (const CollectionDefinition &collection : m_workspace.collections) {
        for (const RequestDefinition &value : collection.requests) {
            if (value.id == id) {
                if (owner) *owner = &collection;
                return &value;
            }
        }
    }
    return nullptr;
}

MainWindow::RequestSession *MainWindow::session(const QUuid &requestId) const
{
    return m_sessions.value(requestId, nullptr);
}

void MainWindow::createSession(const RequestDefinition &request)
{
    if (m_sessions.contains(request.id)) return;
    auto *value = new RequestSession;
    value->controller = new RequestController(this);
    value->log = new RequestLogModel(this);
    QVector<RequestLogEntry> history = m_loadedHistory.take(request.id);
    quint64 nextId = 1;
    for (RequestLogEntry &entry : history) {
        nextId = qMax(nextId, entry.id + 1);
        if (entry.result == RequestResult::Pending) {
            entry.result = RequestResult::Cancelled;
            entry.completedAt = QDateTime::currentDateTime();
            entry.errorText = UiLanguage::text("Выполнение прервано при закрытии приложения");
        }
    }
    value->log->replaceEntries(history);
    value->controller->setNextRequestId(nextId);
    m_sessions.insert(request.id, value);
    connect(value->controller, &RequestController::requestStarted, this,
            [this, id = request.id](const RequestLogEntry &entry) { handleLogEntry(id, entry); });
    connect(value->controller, &RequestController::requestCompleted, this,
            [this, id = request.id](const RequestLogEntry &entry) { handleLogEntry(id, entry); });
    connect(value->controller, &RequestController::requestSkipped, this,
            [this, id = request.id](const RequestLogEntry &entry) { handleLogEntry(id, entry); });
    const auto runtimeChanged = [this, id = request.id] {
        refreshRequestTab(id);
        const CollectionDefinition *owner = nullptr;
        this->request(id, &owner);
        if (owner) refreshCollectionTab(owner->id);
        refreshTreeStatuses();
    };
    connect(value->controller, &RequestController::countersChanged, this,
            [runtimeChanged](const RequestCounters &) { runtimeChanged(); });
    connect(value->controller, &RequestController::runningChanged, this,
            [runtimeChanged](bool) { runtimeChanged(); });
}

void MainWindow::rebuildSessions()
{
    for (const CollectionDefinition &collection : std::as_const(m_workspace.collections))
        for (const RequestDefinition &request : collection.requests) createSession(request);
}

QString MainWindow::objectKey(ItemType type, const QUuid &id)
{
    return QStringLiteral("%1:%2")
        .arg(type == ItemType::Collection ? QStringLiteral("C") : QStringLiteral("R"),
             id.toString(QUuid::WithoutBraces));
}

QUuid MainWindow::idFromItem(const QTreeWidgetItem *item)
{
    return item ? QUuid(item->data(0, IdRole).toString()) : QUuid{};
}

MainWindow::ItemType MainWindow::typeFromItem(const QTreeWidgetItem *item)
{
    return static_cast<ItemType>(item ? item->data(0, TypeRole).toInt()
                                      : static_cast<int>(ItemType::Collection));
}

void MainWindow::rebuildTree()
{
    const QString selected = m_uiSettings.selectedItem;
    QStringList expanded = m_uiSettings.expandedCollections;
    if (m_tree->topLevelItemCount() > 0) {
        expanded.clear();
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            if (m_tree->topLevelItem(i)->isExpanded())
                expanded.append(idFromItem(m_tree->topLevelItem(i)).toString(QUuid::WithoutBraces));
    }

    m_rebuildingTree = true;
    m_tree->clear();
    QTreeWidgetItem *selectedItem = nullptr;
    for (const CollectionDefinition &collection : std::as_const(m_workspace.collections)) {
        auto *collectionItem = new QTreeWidgetItem(m_tree);
        collectionItem->setText(0, collection.name);
        collectionItem->setFirstColumnSpanned(true);
        collectionItem->setData(0, TypeRole, static_cast<int>(ItemType::Collection));
        collectionItem->setData(0, IdRole, collection.id.toString(QUuid::WithoutBraces));
        collectionItem->setIcon(0, AppTheme::icon(u"folder"));
        collectionItem->setFlags(collectionItem->flags() | Qt::ItemIsEditable
                                 | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        const bool expand = expanded.isEmpty()
            || expanded.contains(collection.id.toString(QUuid::WithoutBraces));
        collectionItem->setExpanded(expand);
        if (selected == objectKey(ItemType::Collection, collection.id)) selectedItem = collectionItem;
        for (const RequestDefinition &request : collection.requests) {
            auto *requestItem = new QTreeWidgetItem(collectionItem);
            requestItem->setText(0, request.name);
            requestItem->setText(1, requestOperationName(request.protocol, request.method));
            requestItem->setData(0, TypeRole, static_cast<int>(ItemType::Request));
            requestItem->setData(0, IdRole, request.id.toString(QUuid::WithoutBraces));

            requestItem->setFlags((requestItem->flags() | Qt::ItemIsEditable
                                  | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
            if (selected == objectKey(ItemType::Request, request.id)) selectedItem = requestItem;
        }
    }
    if (selectedItem) m_tree->setCurrentItem(selectedItem);
    else if (m_tree->topLevelItemCount() > 0) m_tree->setCurrentItem(m_tree->topLevelItem(0));
    m_rebuildingTree = false;
    m_collectionCount->setText(QString::number(m_workspace.collections.size()));
    filterTree();
    refreshTreeStatuses();
}

void MainWindow::restoreTabs()
{
    for (const QString &key : std::as_const(m_uiSettings.openTabs)) {
        const int separator = key.indexOf(u':');
        if (separator <= 0) continue;
        const QUuid id(key.mid(separator + 1));
        if (key.startsWith(QStringLiteral("C:"))) openCollection(id);
        else if (key.startsWith(QStringLiteral("R:"))) openRequest(id);
    }
    if (m_tabs->count() == 0) {
        for (const CollectionDefinition &collection : std::as_const(m_workspace.collections)) {
            if (!collection.requests.isEmpty()) {
                openRequest(collection.requests.first().id);
                break;
            }
        }
        if (m_tabs->count() == 0 && !m_workspace.collections.isEmpty())
            openCollection(m_workspace.collections.first().id);
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        if (tabKey(i) == m_uiSettings.activeTab) m_tabs->setCurrentIndex(i);
    if (m_uiSettings.activeTab == QStringLiteral("overview")) m_workspacePages->setCurrentIndex(1);
}

int MainWindow::tabIndex(const QString &kind, const QUuid &id) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget *widget = m_tabs->widget(i);
        if (widget->property("kind").toString() == kind
            && QUuid(widget->property("id").toString()) == id) return i;
    }
    return -1;
}

QString MainWindow::tabKey(int index) const
{
    QWidget *widget = m_tabs->widget(index);
    if (!widget) return {};
    return QStringLiteral("%1:%2")
        .arg(widget->property("kind").toString() == QStringLiteral("collection")
                 ? QStringLiteral("C") : QStringLiteral("R"),
             widget->property("id").toString());
}

void MainWindow::openRequest(const QUuid &requestId)
{
    m_workspacePages->setCurrentIndex(0);
    const int existing = tabIndex(QStringLiteral("request"), requestId);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }
    const CollectionDefinition *owner = nullptr;
    const RequestDefinition *definition = request(requestId, &owner);
    RequestSession *runtime = session(requestId);
    if (!definition || !owner || !runtime) return;

    RequestTab::Actions actions;
    actions.changed = [this](const RequestDefinition &value) { updateRequestDefinition(value); };
    actions.openCollection = [this, collectionId = owner->id] { openCollection(collectionId); };
    actions.sendOnce = [this, requestId] { sendOnce(requestId); };
    actions.start = [this, requestId] { startRequest(requestId); };
    actions.stop = [this, requestId] { stopRequest(requestId); };
    actions.clearLog = [this, requestId] { clearRequestLog(requestId); };
    actions.replay = [this](const RequestLogEntry &entry) { replayEntry(entry); };
    actions.editorSplitterChanged = [this](const QByteArray &state) {
        m_uiSettings.requestEditorSplitterState = state;
        scheduleWorkspaceSave();
    };
    actions.transportBodyHeightChanged = [this](int height) {
        m_uiSettings.transportBodyHeight = height;
        scheduleWorkspaceSave();
    };
    actions.splitterChanged = [this](const QByteArray &state) {
        if (!state.isEmpty()) m_uiSettings.logDetailsVerticalSplitterState = state;
        scheduleWorkspaceSave();
    };
    actions.autoScrollChanged = [this](bool checked) {
        m_uiSettings.autoScroll = checked;
        scheduleWorkspaceSave();
    };
    actions.prettyJsonChanged = [this](bool checked) {
        m_uiSettings.prettyJson = checked;
        for (RequestTab *tab : std::as_const(m_requestTabs)) tab->setPrettyJson(checked);
        for (CollectionTab *tab : std::as_const(m_collectionTabs)) tab->setPrettyJson(checked);
        m_overviewLog->setPrettyJson(checked);
        scheduleWorkspaceSave();
    };
    auto *tab = new RequestTab(*definition, *owner, runtime->log, m_uiSettings,
                               std::move(actions), m_tabs);
    tab->setProperty("kind", QStringLiteral("request"));
    tab->setProperty("id", requestId.toString(QUuid::WithoutBraces));
    m_requestTabs.insert(requestId, tab);
    const int index = m_tabs->addTab(tab,
        QStringLiteral("%1  %2").arg(requestOperationName(definition->protocol, definition->method), definition->name));
    requestTabLabel(m_tabs, index, requestOperationName(definition->protocol, definition->method), definition->name);
    addTabCloseButton(tab, index);
    m_tabs->setCurrentIndex(index);
    tab->updateRuntime(runtime->controller->isRunning(), runtime->controller->counters());
    tab->logReplaced();
    scheduleWorkspaceSave();
}

void MainWindow::openCollection(const QUuid &collectionId)
{
    m_workspacePages->setCurrentIndex(0);
    const int existing = tabIndex(QStringLiteral("collection"), collectionId);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }
    const CollectionDefinition *definition = collection(collectionId);
    if (!definition) return;

    CollectionTab::Actions actions;
    actions.changed = [this](const CollectionDefinition &value) {
        updateCollectionDefinition(value);
    };
    actions.start = [this, collectionId] { startCollection(collectionId); };
    actions.stop = [this, collectionId] { stopCollection(collectionId); };
    actions.clearLog = [this, collectionId] { clearCollectionLog(collectionId); };
    actions.editorSplitterChanged = [this](const QByteArray &state) {
        m_uiSettings.collectionVerticalSplitterState = state;
        scheduleWorkspaceSave();
    };
    actions.replay = [this](const RequestLogEntry &entry) { replayEntry(entry); };
    actions.openRequest = [this](const QUuid &id) { openRequest(id); };
    actions.newRequest = [this, collectionId] { addRequest(collectionId); };
    actions.requestBusy = [this](const QUuid &id) {
        const auto *runtime = session(id);
        return runtime && (runtime->controller->isRunning() || runtime->controller->counters().active > 0);
    };
    actions.toggleRequest = [this](const QUuid &id) {
        const auto *runtime = session(id);
        if (runtime && (runtime->controller->isRunning() || runtime->controller->counters().active > 0)) stopRequest(id);
        else startRequest(id);
    };
    actions.splitterChanged = [this](const QByteArray &state) {
        if (!state.isEmpty()) m_uiSettings.logDetailsVerticalSplitterState = state;
        scheduleWorkspaceSave();
    };
    actions.autoScrollChanged = [this](bool checked) {
        m_uiSettings.autoScroll = checked;
        scheduleWorkspaceSave();
    };
    actions.prettyJsonChanged = [this](bool checked) {
        m_uiSettings.prettyJson = checked;
        for (RequestTab *tab : std::as_const(m_requestTabs)) tab->setPrettyJson(checked);
        for (CollectionTab *tab : std::as_const(m_collectionTabs)) tab->setPrettyJson(checked);
        m_overviewLog->setPrettyJson(checked);
        scheduleWorkspaceSave();
    };
    auto *tab = new CollectionTab(*definition, aggregateEntries(collectionId), m_uiSettings,
                                  std::move(actions), m_tabs);
    tab->setProperty("kind", QStringLiteral("collection"));
    tab->setProperty("id", collectionId.toString(QUuid::WithoutBraces));
    m_collectionTabs.insert(collectionId, tab);
    const int index = m_tabs->addTab(tab, UiLanguage::text("Коллекция · %1").arg(definition->name));
    auto *folder = new ThemeIcon(QStringLiteral("folder"), m_tabs->tabBar());
    folder->setFixedSize(24, 16);
    folder->setContentsMargins(4, 0, 4, 0);
    m_tabs->tabBar()->setTabButton(index, QTabBar::LeftSide, folder);
    addTabCloseButton(tab, index);
    m_tabs->setCurrentIndex(index);
    refreshCollectionTab(collectionId);
    scheduleWorkspaceSave();
}

void MainWindow::addTabCloseButton(QWidget *tab, int index)
{
    auto *button = new QToolButton(m_tabs->tabBar());
    button->setObjectName(QStringLiteral("tabCloseButton"));
    button->setAccessibleName(UiLanguage::text("Закрыть вкладку"));
    button->setToolTip(UiLanguage::text("Закрыть вкладку"));
    button->setFixedSize(20, 20);
    buttonIcon(button, QStringLiteral("close"));
    m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, button);
    connect(button, &QToolButton::clicked, this, [this, tab] { closeTab(m_tabs->indexOf(tab)); });
}

void MainWindow::closeTab(int index)
{
    QWidget *widget = m_tabs->widget(index);
    if (!widget) return;
    const QUuid id(widget->property("id").toString());
    if (widget->property("kind").toString() == QStringLiteral("request"))
        m_requestTabs.remove(id);
    else
        m_collectionTabs.remove(id);
    m_tabs->removeTab(index);
    widget->deleteLater();
    scheduleWorkspaceSave();
}

void MainWindow::refreshRequestTab(const QUuid &requestId)
{
    RequestTab *tab = m_requestTabs.value(requestId, nullptr);
    const CollectionDefinition *owner = nullptr;
    const RequestDefinition *definition = request(requestId, &owner);
    RequestSession *runtime = session(requestId);
    if (!tab || !definition || !owner || !runtime) return;
    tab->setDefinition(*definition, *owner);
    tab->updateRuntime(runtime->controller->isRunning(), runtime->controller->counters());
    const int index = tabIndex(QStringLiteral("request"), requestId);
    if (index >= 0)
        requestTabLabel(m_tabs, index, requestOperationName(definition->protocol, definition->method), definition->name);
}

void MainWindow::refreshCollectionTab(const QUuid &collectionId)
{
    CollectionTab *tab = m_collectionTabs.value(collectionId, nullptr);
    const CollectionDefinition *definition = collection(collectionId);
    if (!tab || !definition) return;
    tab->setDefinition(*definition);
    const RequestCounters counters = aggregateCounters(collectionId);
    int running = 0;
    for (const RequestDefinition &request : definition->requests) {
        if (RequestSession *value = session(request.id))
            running += value->controller->isRunning() ? 1 : 0;
    }
    tab->updateRuntime(counters, running, definition->requests.size());
    const int index = tabIndex(QStringLiteral("collection"), collectionId);
    if (index >= 0) m_tabs->setTabText(index,
        UiLanguage::text("Коллекция · %1").arg(definition->name));
}

void MainWindow::refreshOpenTabs()
{
    for (auto it = m_requestTabs.cbegin(); it != m_requestTabs.cend(); ++it)
        refreshRequestTab(it.key());
    for (auto it = m_collectionTabs.cbegin(); it != m_collectionTabs.cend(); ++it) {
        it.value()->setEntries(aggregateEntries(it.key()));
        refreshCollectionTab(it.key());
    }
}

void MainWindow::refreshTreeStatuses()
{
    refreshOverview();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *collectionItem = m_tree->topLevelItem(i);
        collectionItem->setIcon(0, AppTheme::icon(u"folder"));
        bool collectionRunning = false;
        for (int j = 0; j < collectionItem->childCount(); ++j) {
            QTreeWidgetItem *requestItem = collectionItem->child(j);
            RequestSession *value = session(idFromItem(requestItem));
            const bool running = value && value->controller->isRunning();
            QFont font = requestItem->font(0);
            font.setBold(running);
            requestItem->setFont(0, font);
            requestItem->setForeground(0, running ? AppTheme::color(u"accent")
                                                  : AppTheme::color(u"text"));
            requestItem->setToolTip(0, running ? UiLanguage::text("Периодический запрос выполняется")
                                               : UiLanguage::text("Остановлено"));
            collectionRunning = collectionRunning || running;
        }
        QFont font = collectionItem->font(0);
        font.setWeight(QFont::DemiBold);
        collectionItem->setFont(0, font);
        collectionItem->setForeground(0, collectionRunning
            ? AppTheme::color(u"accent") : AppTheme::color(u"text"));
    }
}

void MainWindow::addCollection()
{
    CollectionDefinition value;
    value.name = UiLanguage::text("Новая коллекция %1").arg(m_workspace.collections.size() + 1);
    m_workspace.collections.append(value);
    m_uiSettings.selectedItem = objectKey(ItemType::Collection, value.id);
    rebuildTree();
    openCollection(value.id);
    scheduleWorkspaceSave();
}

void MainWindow::addRequest(const QUuid &requestedCollectionId)
{
    QUuid collectionId = requestedCollectionId;
    if (collectionId.isNull()) {
        QTreeWidgetItem *item = m_tree->currentItem();
        if (item) {
            if (typeFromItem(item) == ItemType::Collection) collectionId = idFromItem(item);
            else if (item->parent()) collectionId = idFromItem(item->parent());
        }
    }
    if (collectionId.isNull() && !m_workspace.collections.isEmpty())
        collectionId = m_workspace.collections.first().id;
    if (collectionId.isNull()) {
        CollectionDefinition newCollection;
        newCollection.name = UiLanguage::text("Моя коллекция");
        collectionId = newCollection.id;
        m_workspace.collections.append(newCollection);
    }
    CollectionDefinition *owner = collection(collectionId);
    if (!owner) return;
    RequestDefinition value;
    value.name = UiLanguage::text("Запрос %1").arg(owner->requests.size() + 1);
    owner->requests.append(value);
    createSession(value);
    m_uiSettings.selectedItem = objectKey(ItemType::Request, value.id);
    rebuildTree();
    openRequest(value.id);
    refreshCollectionTab(collectionId);
    scheduleWorkspaceSave();
}

void MainWindow::renameItem(QTreeWidgetItem *item)
{
    if (!item) return;
    m_tree->setCurrentItem(item);
    m_tree->editItem(item, 0);
}

void MainWindow::duplicateItem(QTreeWidgetItem *item)
{
    if (!item) return;
    if (typeFromItem(item) == ItemType::Collection) {
        const CollectionDefinition *source = collection(idFromItem(item));
        if (!source) return;
        CollectionDefinition copy = *source;
        copy.id = QUuid::createUuid();
        copy.name += UiLanguage::text(" — копия");
        for (RequestDefinition &request : copy.requests) request.id = QUuid::createUuid();
        m_workspace.collections.append(copy);
        for (const RequestDefinition &request : copy.requests) createSession(request);
        m_uiSettings.selectedItem = objectKey(ItemType::Collection, copy.id);
        rebuildTree();
        openCollection(copy.id);
    } else {
        CollectionDefinition *owner = nullptr;
        const RequestDefinition *source = request(idFromItem(item), &owner);
        if (!source || !owner) return;
        RequestDefinition copy = *source;
        copy.id = QUuid::createUuid();
        copy.name += UiLanguage::text(" — копия");
        owner->requests.append(copy);
        createSession(copy);
        m_uiSettings.selectedItem = objectKey(ItemType::Request, copy.id);
        const QUuid ownerId = owner->id;
        rebuildTree();
        openRequest(copy.id);
        refreshCollectionTab(ownerId);
    }
    scheduleWorkspaceSave();
}

void MainWindow::deleteItem(QTreeWidgetItem *item)
{
    if (!item) return;
    const ItemType type = typeFromItem(item);
    const QUuid id = idFromItem(item);
    QString name;
    QVector<QUuid> requestIds;
    if (type == ItemType::Collection) {
        const CollectionDefinition *value = collection(id);
        if (!value) return;
        name = value->name;
        for (const RequestDefinition &request : value->requests) requestIds.append(request.id);
    } else {
        const RequestDefinition *value = request(id);
        if (!value) return;
        name = value->name;
        requestIds.append(id);
    }
    bool active = false;
    for (const QUuid &requestId : std::as_const(requestIds)) {
        RequestSession *value = session(requestId);
        active = active || (value && (value->controller->isRunning()
                                     || value->controller->counters().active > 0));
    }
    const QString warning = active
        ? UiLanguage::text("«%1» сейчас выполняется. Запросы будут остановлены и удалены вместе с историей.").arg(name)
        : UiLanguage::text("Удалить «%1» вместе с сохранённой историей?").arg(name);
    if (QMessageBox::question(this, UiLanguage::text("Подтверждение удаления"), warning,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) return;

    for (const QUuid &requestId : std::as_const(requestIds)) {
        const int requestIndex = tabIndex(QStringLiteral("request"), requestId);
        if (requestIndex >= 0) closeTab(requestIndex);
        RequestSession *value = m_sessions.take(requestId);
        if (value) {
            value->controller->stop();
            delete value->controller;
            delete value->log;
            delete value;
        }
    }
    if (type == ItemType::Collection) {
        const int collectionIndex = tabIndex(QStringLiteral("collection"), id);
        if (collectionIndex >= 0) closeTab(collectionIndex);
        m_workspace.collections.removeIf([id](const CollectionDefinition &value) {
            return value.id == id;
        });
    } else {
        for (CollectionDefinition &collection : m_workspace.collections)
            collection.requests.removeIf([id](const RequestDefinition &value) {
                return value.id == id;
            });
    }
    rebuildTree();
    refreshOpenTabs();
    scheduleWorkspaceSave();
    scheduleHistorySave();
}

void MainWindow::showTreeContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = m_tree->itemAt(position);
    QMenu menu(this);
    QAction *newCollection = menu.addAction(UiLanguage::text("Новая коллекция"));
    QAction *newRequest = menu.addAction(UiLanguage::text("Новый запрос"));
    menu.addSeparator();
    QAction *open = item ? menu.addAction(UiLanguage::text("Открыть")) : nullptr;
    QAction *rename = item ? menu.addAction(UiLanguage::text("Переименовать")) : nullptr;
    QAction *duplicate = item ? menu.addAction(UiLanguage::text("Дублировать")) : nullptr;
    QAction *remove = item ? menu.addAction(UiLanguage::text("Удалить")) : nullptr;
    QAction *exportCollection = item ? menu.addAction(UiLanguage::text("Экспорт коллекции…")) : nullptr;
    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if (!chosen) return;
    if (chosen == newCollection) addCollection();
    else if (chosen == newRequest) {
        QUuid ownerId;
        if (item) ownerId = typeFromItem(item) == ItemType::Collection
            ? idFromItem(item) : idFromItem(item->parent());
        addRequest(ownerId);
    } else if (chosen == open) {
        if (typeFromItem(item) == ItemType::Collection) openCollection(idFromItem(item));
        else openRequest(idFromItem(item));
    } else if (chosen == rename) renameItem(item);
    else if (chosen == duplicate) duplicateItem(item);
    else if (chosen == remove) deleteItem(item);
    else if (chosen == exportCollection) exportWorkspace(typeFromItem(item) == ItemType::Collection
        ? idFromItem(item) : idFromItem(item->parent()));
}

bool MainWindow::treeStructureValid() const
{
    int collectionCount = 0;
    int requestCount = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *collectionItem = m_tree->topLevelItem(i);
        if (typeFromItem(collectionItem) != ItemType::Collection) return false;
        ++collectionCount;
        for (int j = 0; j < collectionItem->childCount(); ++j) {
            const QTreeWidgetItem *requestItem = collectionItem->child(j);
            if (typeFromItem(requestItem) != ItemType::Request || requestItem->childCount() > 0)
                return false;
            ++requestCount;
        }
    }
    int expectedRequests = 0;
    for (const CollectionDefinition &value : m_workspace.collections)
        expectedRequests += value.requests.size();
    return collectionCount == m_workspace.collections.size() && requestCount == expectedRequests;
}

void MainWindow::applyTreeOrder()
{
    if (!treeStructureValid()) {
        rebuildTree();
        statusBar()->showMessage(UiLanguage::text("Коллекции могут находиться только на верхнем уровне, запросы — внутри коллекций"), 5000);
        return;
    }
    QHash<QUuid, CollectionDefinition> collections;
    QHash<QUuid, RequestDefinition> requests;
    for (const CollectionDefinition &collection : std::as_const(m_workspace.collections)) {
        collections.insert(collection.id, collection);
        for (const RequestDefinition &request : collection.requests) requests.insert(request.id, request);
    }
    Workspace reordered;
    reordered.schemaVersion = m_workspace.schemaVersion;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *collectionItem = m_tree->topLevelItem(i);
        CollectionDefinition collection = collections.value(idFromItem(collectionItem));
        collection.requests.clear();
        for (int j = 0; j < collectionItem->childCount(); ++j)
            collection.requests.append(requests.value(idFromItem(collectionItem->child(j))));
        reordered.collections.append(collection);
    }
    m_workspace = std::move(reordered);
    refreshOpenTabs();
    scheduleWorkspaceSave();
}

void MainWindow::updateCollectionDefinition(const CollectionDefinition &definition)
{
    CollectionDefinition *stored = collection(definition.id);
    if (!stored) return;
    const bool nameChanged = stored->name != definition.name;
    stored->name = definition.name;
    stored->host = definition.host;
    stored->port = definition.port;
    stored->intervalMs = definition.intervalMs;
    stored->maxInFlight = definition.maxInFlight;
    refreshCollectionTab(stored->id);
    for (const RequestDefinition &request : std::as_const(stored->requests))
        refreshRequestTab(request.id);
    if (nameChanged) rebuildTree();
    else if (m_searchActive) filterTree();
    scheduleWorkspaceSave();
}

void MainWindow::updateRequestDefinition(const RequestDefinition &definition)
{
    RequestDefinition *stored = request(definition.id);
    if (!stored) return;
    const bool nameChanged = stored->name != definition.name;
    const bool operationChanged = stored->protocol != definition.protocol
        || stored->method != definition.method;
    *stored = definition;
    refreshRequestTab(stored->id);
    if (nameChanged) rebuildTree();
    else if (operationChanged) {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *collectionItem = m_tree->topLevelItem(i);
            for (int j = 0; j < collectionItem->childCount(); ++j) {
                QTreeWidgetItem *item = collectionItem->child(j);
                if (idFromItem(item) == definition.id)
                    item->setText(1, requestOperationName(definition.protocol, definition.method));
            }
        }
    }
    if (operationChanged) m_tree->viewport()->update();
    if (m_searchActive && !nameChanged) filterTree();
    scheduleWorkspaceSave();
}

void MainWindow::sendOnce(const QUuid &requestId)
{
    const CollectionDefinition *owner = nullptr;
    const RequestDefinition *definition = request(requestId, &owner);
    RequestSession *runtime = session(requestId);
    if (!definition || !owner || !runtime) return;
    const RequestConfig config = resolveRequestConfig(*owner, *definition);
    const QString error = validateRequestConfig(config);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, UiLanguage::text("Некорректный запрос"), error);
        return;
    }
    runtime->controller->sendOnce(config);
    scheduleWorkspaceSave();
}

void MainWindow::startRequest(const QUuid &requestId)
{
    const CollectionDefinition *owner = nullptr;
    const RequestDefinition *definition = request(requestId, &owner);
    RequestSession *runtime = session(requestId);
    if (!definition || !owner || !runtime || runtime->controller->isRunning()) return;
    const RequestConfig config = resolveRequestConfig(*owner, *definition);
    const QString error = validateRequestConfig(config);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, UiLanguage::text("Некорректный запрос"), error);
        return;
    }
    runtime->controller->start(config);
    scheduleWorkspaceSave();
}

void MainWindow::stopRequest(const QUuid &requestId)
{
    if (RequestSession *runtime = session(requestId)) runtime->controller->stop();
}

void MainWindow::startCollection(const QUuid &collectionId)
{
    const CollectionDefinition *definition = collection(collectionId);
    if (!definition || definition->requests.isEmpty()) {
        QMessageBox::information(this, UiLanguage::text("Пустая коллекция"),
                                 UiLanguage::text("Добавьте хотя бы один запрос."));
        return;
    }
    QStringList errors;
    for (const RequestDefinition &request : definition->requests) {
        const QString error = validateRequestConfig(resolveRequestConfig(*definition, request));
        if (!error.isEmpty()) errors.append(QStringLiteral("%1: %2").arg(request.name, error));
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, UiLanguage::text("Коллекция не запущена"),
            UiLanguage::text("Исправьте настройки всех запросов:\n\n%1").arg(errors.join(u'\n')));
        return;
    }
    for (const RequestDefinition &request : definition->requests) {
        RequestSession *runtime = session(request.id);
        if (runtime && !runtime->controller->isRunning())
            runtime->controller->start(resolveRequestConfig(*definition, request));
    }
    scheduleWorkspaceSave();
}

void MainWindow::stopCollection(const QUuid &collectionId)
{
    const CollectionDefinition *definition = collection(collectionId);
    if (!definition) return;
    for (const RequestDefinition &request : definition->requests)
        if (RequestSession *runtime = session(request.id)) runtime->controller->stop();
}

void MainWindow::clearRequestLog(const QUuid &requestId)
{
    RequestSession *runtime = session(requestId);
    if (!runtime || runtime->controller->counters().active > 0) return;
    runtime->log->clear();
    runtime->controller->resetCounters();
    if (RequestTab *tab = m_requestTabs.value(requestId)) tab->logReplaced();
    const CollectionDefinition *owner = nullptr;
    request(requestId, &owner);
    if (owner && m_collectionTabs.contains(owner->id)) {
        m_collectionTabs.value(owner->id)->setEntries(aggregateEntries(owner->id));
        refreshCollectionTab(owner->id);
    }
    refreshOverview(true);
    scheduleHistorySave();
}

void MainWindow::clearCollectionLog(const QUuid &collectionId)
{
    const CollectionDefinition *definition = collection(collectionId);
    if (!definition) return;
    for (const RequestDefinition &request : definition->requests) {
        RequestSession *runtime = session(request.id);
        if (runtime && runtime->controller->counters().active > 0) return;
    }
    for (const RequestDefinition &request : definition->requests) {
        RequestSession *runtime = session(request.id);
        if (!runtime) continue;
        runtime->log->clear();
        runtime->controller->resetCounters();
        if (RequestTab *tab = m_requestTabs.value(request.id)) tab->logReplaced();
    }
    if (CollectionTab *tab = m_collectionTabs.value(collectionId))
        tab->setEntries({});
    refreshOverview(true);
    scheduleHistorySave();
}

void MainWindow::handleLogEntry(const QUuid &requestId, const RequestLogEntry &entry)
{
    RequestSession *runtime = session(requestId);
    if (!runtime) return;
    QUuid removedEntry;
    if (runtime->log->rowForId(entry.entryId) < 0 && runtime->log->rowCount() >= 1000) {
        if (const auto *oldest = runtime->log->entryAt(0)) removedEntry = oldest->entryId;
    }
    runtime->log->upsertEntry(entry);
    if (!removedEntry.isNull()) m_overviewModel->removeEntry(removedEntry);
    m_overviewModel->upsertEntry(entry);
    m_overviewLog->entryChanged(entry.entryId);
    if (RequestTab *tab = m_requestTabs.value(requestId)) tab->logEntryChanged(entry.entryId);
    const CollectionDefinition *owner = nullptr;
    request(requestId, &owner);
    if (owner) {
        if (CollectionTab *tab = m_collectionTabs.value(owner->id)) {
            tab->setEntries(aggregateEntries(owner->id));
            tab->entryChanged(entry.entryId);
        }
        refreshCollectionTab(owner->id);
    }
    refreshRequestTab(requestId);
    scheduleHistorySave();
}

QVector<RequestLogEntry> MainWindow::aggregateEntries(const QUuid &collectionId) const
{
    QVector<RequestLogEntry> result;
    const CollectionDefinition *definition = collection(collectionId);
    if (!definition) return result;
    for (const RequestDefinition &request : definition->requests) {
        if (RequestSession *runtime = session(request.id))
            result += runtime->log->entries();
    }
    std::ranges::sort(result, [](const RequestLogEntry &left, const RequestLogEntry &right) {
        return left.sentAt < right.sentAt;
    });
    return result;
}

RequestCounters MainWindow::aggregateCounters(const QUuid &collectionId) const
{
    RequestCounters result;
    const CollectionDefinition *definition = collection(collectionId);
    if (!definition) return result;
    for (const RequestDefinition &request : definition->requests) {
        if (RequestSession *runtime = session(request.id)) {
            const RequestCounters counters = runtime->controller->counters();
            result.sent += counters.sent;
            result.completed += counters.completed;
            result.errors += counters.errors;
            result.skipped += counters.skipped;
            result.active += counters.active;
        }
    }
    return result;
}

void MainWindow::scheduleWorkspaceSave()
{
    if (!m_closing && m_workspaceSaveTimer) {
        m_saveStatus->setText(UiLanguage::text("●  Сохранение…"));
        m_workspaceSaveTimer->start();
    }
}

void MainWindow::scheduleHistorySave()
{
    if (!m_closing && m_historySaveTimer) m_historySaveTimer->start();
}

bool MainWindow::saveWorkspace()
{
    QString error;
    if (!m_workspaceStore.saveWorkspace(m_workspace, &error)) {
        showStorageError(UiLanguage::text("Не удалось сохранить коллекции: %1").arg(error));
        return false;
    }
    m_saveStatus->setText(UiLanguage::text("●  Всё сохранено"));
    return true;
}

bool MainWindow::saveHistory()
{
    QHash<QUuid, QVector<RequestLogEntry>> history = m_loadedHistory;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it)
        history.insert(it.key(), it.value()->log->entries());
    QString error;
    if (!m_workspaceStore.saveHistory(history, &error)) {
        showStorageError(UiLanguage::text("Не удалось сохранить историю: %1").arg(error));
        return false;
    }
    return true;
}

bool MainWindow::saveUiSettings()
{
    m_uiSettings.windowGeometry = saveGeometry();
    m_uiSettings.workspaceSplitterState = m_workspaceSplitter->saveState();
    m_uiSettings.openTabs.clear();
    for (int i = 0; i < m_tabs->count(); ++i) m_uiSettings.openTabs.append(tabKey(i));
    m_uiSettings.activeTab = m_workspacePages->currentIndex() == 1
        ? QStringLiteral("overview") : tabKey(m_tabs->currentIndex());
    if (QTreeWidgetItem *current = m_tree->currentItem())
        m_uiSettings.selectedItem = objectKey(typeFromItem(current), idFromItem(current));
    m_uiSettings.expandedCollections = m_searchActive ? m_searchExpanded : QStringList{};
    for (int i = 0; !m_searchActive && i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->isExpanded())
            m_uiSettings.expandedCollections.append(idFromItem(item).toString(QUuid::WithoutBraces));
    }
    if (!m_workspace.collections.isEmpty() && !m_workspace.collections.first().requests.isEmpty())
        m_uiSettings.request = resolveRequestConfig(m_workspace.collections.first(),
                                                    m_workspace.collections.first().requests.first());
    if (!m_settingsStore.save(m_uiSettings)) {
        showStorageError(UiLanguage::text("Не удалось сохранить MultiSender.ini"));
        return false;
    }
    return true;
}

bool MainWindow::saveAll()
{
    if (m_workspaceSaveTimer) m_workspaceSaveTimer->stop();
    if (m_historySaveTimer) m_historySaveTimer->stop();
    const bool workspaceSaved = saveWorkspace();
    const bool historySaved = saveHistory();
    const bool settingsSaved = saveUiSettings();
    return workspaceSaved && historySaved && settingsSaved;
}

void MainWindow::showStorageError(const QString &message)
{
    statusBar()->showMessage(message, 12000);
}

void MainWindow::showOverview()
{
    refreshOverview(true);
    m_workspacePages->setCurrentIndex(1);
    m_workspaceHome->setChecked(true);
    scheduleWorkspaceSave();
}

void MainWindow::refreshOverview(bool replaceLog)
{
    if (!m_overviewModel) return;
    int requests = 0;
    int running = 0;
    int active = 0;
    for (const auto &definition : std::as_const(m_workspace.collections)) requests += definition.requests.size();
    for (const auto *value : std::as_const(m_sessions)) {
        running += value->controller->isRunning() ? 1 : 0;
        active += value->controller->counters().active;
    }
    m_overviewSummary->setText(active > 0 ? UiLanguage::text("Активных операций: %1. Все ответы появляются в общем журнале.").arg(active)
        : UiLanguage::text("Проверяйте API, отправляйте байты и управляйте устройствами."));
    m_overviewMetrics[0]->setText(QString::number(m_workspace.collections.size()));
    m_overviewMetrics[1]->setText(QString::number(requests));
    m_overviewMetrics[2]->setText(QString::number(running));
    m_stopAllButton->setEnabled(running > 0 || active > 0);
    m_stopAllButton->setVisible(running > 0 || active > 0);
    m_overviewLog->setClearEnabled(active == 0);
    if (replaceLog) {
        QVector<RequestLogEntry> entries;
        for (const auto *value : std::as_const(m_sessions)) entries += value->log->entries();
        for (const auto &orphaned : std::as_const(m_loadedHistory)) entries += orphaned;
        std::ranges::stable_sort(entries, {}, &RequestLogEntry::sentAt);
        m_overviewModel->replaceEntries(entries);
        m_overviewLog->modelReplaced();
    }
    m_overviewMetrics[3]->setText(QString::number(m_overviewModel->rowCount()));
}

void MainWindow::stopAll()
{
    for (auto *value : std::as_const(m_sessions)) value->controller->stop();
    refreshTreeStatuses();
}

bool MainWindow::changeLanguage(const QString &language)
{
    const auto code = UiLanguage::normalized(language);
    if (code == UiLanguage::current()) return true;
    const bool busy = std::ranges::any_of(m_sessions, [](const RequestSession *value) {
        return value->controller->isRunning() || value->controller->counters().active > 0;
    });
    if (busy) {
        QMessageBox dialog(QMessageBox::Question, UiLanguage::text("Смена языка"),
            UiLanguage::text("Для смены языка нужно остановить текущие отправки. Остановить их и продолжить?"),
            QMessageBox::NoButton, this);
        auto *proceed = dialog.addButton(UiLanguage::text("Остановить и продолжить"), QMessageBox::AcceptRole);
        dialog.addButton(QMessageBox::Cancel);
        dialog.setDefaultButton(QMessageBox::Cancel);
        dialog.exec();
        if (dialog.clickedButton() != proceed) return false;
    }
    const auto oldLanguage = m_uiSettings.language;
    m_uiSettings.language = code;
    m_restartForLanguage = true;
    if (!close()) {
        const QString error = statusBar()->currentMessage();
        m_uiSettings.language = oldLanguage;
        saveUiSettings();
        QMessageBox::warning(this, UiLanguage::text("Смена языка"), error);
        return false;
    }
    return true;
}

void MainWindow::clearAllLogs()
{
    for (const auto *value : std::as_const(m_sessions))
        if (value->controller->counters().active > 0) return;
    if (QMessageBox::question(this, UiLanguage::text("Очистка истории"),
        UiLanguage::text("Удалить историю всех запросов? Коллекции и настройки останутся."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    m_loadedHistory.clear();
    for (auto *value : std::as_const(m_sessions)) {
        value->log->clear();
        value->controller->resetCounters();
    }
    for (auto *tab : std::as_const(m_requestTabs)) tab->logReplaced();
    refreshOpenTabs();
    refreshOverview(true);
    scheduleHistorySave();
}

void MainWindow::exportWorkspace(const QUuid &collectionId)
{
    Workspace exported = m_workspace;
    if (!collectionId.isNull()) {
        const auto *definition = collection(collectionId);
        if (!definition) return;
        exported.collections = {*definition};
    }
    const QString path = QFileDialog::getSaveFileName(this, UiLanguage::text("Экспорт коллекций"),
        QStringLiteral("MultiSender.collections.json"), UiLanguage::text("Коллекции JSON (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!WorkspaceExchange::write(path, exported, &error))
        QMessageBox::warning(this, UiLanguage::text("Ошибка экспорта"), error);
    else statusBar()->showMessage(UiLanguage::text("Коллекции экспортированы: %1").arg(path), 8000);
}

void MainWindow::importWorkspace()
{
    const QString path = QFileDialog::getOpenFileName(this, UiLanguage::text("Импорт коллекций Qt / Electron"),
        {}, UiLanguage::text("Коллекции JSON (*.json)"));
    if (path.isEmpty()) return;
    Workspace incoming;
    QString error;
    if (!WorkspaceExchange::read(path, &incoming, &error)) {
        QMessageBox::warning(this, UiLanguage::text("Ошибка импорта"), error);
        return;
    }
    int count = 0;
    for (const auto &definition : incoming.collections) count += definition.requests.size();
    QMessageBox choice(QMessageBox::Question, UiLanguage::text("Импорт коллекций"),
        UiLanguage::text("Найдено коллекций: %1, запросов: %2.\nДобавить их к текущим или заменить пространство? При замене будет создана резервная копия; история сохранится.")
            .arg(incoming.collections.size()).arg(count), QMessageBox::NoButton, this);
    auto *mergeButton = choice.addButton(UiLanguage::text("Добавить"), QMessageBox::AcceptRole);
    auto *replaceButton = choice.addButton(UiLanguage::text("Заменить"), QMessageBox::DestructiveRole);
    choice.addButton(QMessageBox::Cancel);
    choice.setDefaultButton(mergeButton);
    choice.exec();
    const bool replace = choice.clickedButton() == replaceButton;
    if (!replace && choice.clickedButton() != mergeButton) return;
    Workspace candidate = replace ? incoming : WorkspaceExchange::merged(m_workspace, incoming);
    QString backupPath;
    if (replace) {
        backupPath = QFileInfo(m_workspaceStore.workspacePath()).absolutePath()
            + QStringLiteral("/backups/import-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        if (!QDir().mkpath(backupPath)
            || !WorkspaceExchange::write(backupPath + QStringLiteral("/MultiSender.workspace.json"), m_workspace, &error)) {
            QMessageBox::warning(this, UiLanguage::text("Импорт отменён"), UiLanguage::text("Не удалось сохранить резервную копию: %1").arg(error));
            return;
        }
        // Preserve every existing user file, including the legacy names. The current
        // in-memory workspace above additionally includes edits awaiting autosave.
        const QDir directory(QFileInfo(m_workspaceStore.workspacePath()).absolutePath());
        for (const QString &name : {QStringLiteral("MultiSender"), QStringLiteral("HttpRequester"), QStringLiteral("MultiSenderQml")}) {
            for (const QString &suffix : {QStringLiteral(".ini"), QStringLiteral(".workspace.json"), QStringLiteral(".history.json")}) {
                const QString source = directory.filePath(name + suffix);
                if (!QFile::exists(source)) continue;
                const QString destination = backupPath + u'/' + name + suffix
                    + (name == QStringLiteral("MultiSender") && suffix == QStringLiteral(".workspace.json") ? QStringLiteral(".saved") : QString{});
                if (!QFile::copy(source, destination)) {
                    QMessageBox::warning(this, UiLanguage::text("Импорт отменён"),
                        UiLanguage::text("Не удалось скопировать %1. Резервные файлы: %2").arg(source, backupPath));
                    return;
                }
            }
        }
    }
    // Validate and write atomically before changing the visible workspace.
    if (!m_workspaceStore.saveWorkspace(candidate, &error)) {
        QMessageBox::warning(this, UiLanguage::text("Импорт отменён"), error
            + (backupPath.isEmpty() ? QString{} : UiLanguage::text("\nРезервная копия: %1").arg(backupPath)));
        return;
    }
    if (replace) {
        stopAll();
        while (m_tabs->count()) closeTab(0);
        for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
            m_loadedHistory.insert(it.key(), it.value()->log->entries());
            delete it.value()->controller;
            it.value()->log->deleteLater();
            delete it.value();
        }
        m_sessions.clear();
    }
    m_workspace = std::move(candidate);
    rebuildSessions();
    rebuildTree();
    refreshOpenTabs();
    showOverview();
    saveAll();
    statusBar()->showMessage(replace ? UiLanguage::text("Импорт завершён. Резервная копия: %1").arg(backupPath)
                                    : UiLanguage::text("Импортировано коллекций: %1, запросов: %2").arg(incoming.collections.size()).arg(count), 15000);
}

void MainWindow::replayEntry(const RequestLogEntry &entry)
{
    RequestConfig config = entry.configAvailable ? entry.configSnapshot : RequestConfig{};
    if (!entry.configAvailable) {
        config.protocol = entry.protocol;
        config.method = entry.method;
        config.host = entry.url.host();
        config.httpScheme = entry.url.scheme() == QStringLiteral("https") ? QStringLiteral("https") : QStringLiteral("http");
        config.port = static_cast<quint16>(entry.url.port(config.httpScheme == QStringLiteral("https") ? 443 : 80));
        config.target = entry.url.path(QUrl::FullyEncoded);
        if (entry.url.hasQuery()) config.target += u'?' + entry.url.query(QUrl::FullyEncoded);
        config.body = entry.requestBody;
        if (entry.protocol == RequestProtocol::Serial) config.serial.portName = entry.url.host();
        for (const auto &header : entry.requestHeaders) {
            const auto lower = header.name.toLower();
            if (lower != "host" && lower != "content-length" && lower != "transfer-encoding") config.headers.append(header);
        }
    }
    auto *owner = collection(entry.collectionId);
    if (!owner) {
        CollectionDefinition recovered;
        recovered.name = UiLanguage::text("Из истории");
        m_workspace.collections.append(recovered);
        owner = &m_workspace.collections.last();
    }
    RequestDefinition restored;
    restored.name = UiLanguage::text("Повтор · %1").arg(entry.requestName.isEmpty() ? requestOperationName(entry.protocol, entry.method) : entry.requestName);
    restored.protocol = config.protocol;
    restored.method = config.method;
    restored.target = config.target;
    restored.body = config.body;
    restored.headers = config.headers;
    restored.httpScheme = config.httpScheme;
    restored.httpTimeoutMs = config.httpTimeoutMs;
    restored.waitForResponse = config.waitForResponse;
    restored.timeoutMs = config.timeoutMs;
    restored.serial = config.serial;
    restored.host = {config.host, true};
    restored.port = {config.port, true};
    restored.intervalMs = {config.intervalMs, true};
    restored.maxInFlight = {config.maxInFlight, true};
    owner->requests.append(restored);
    createSession(restored);
    rebuildTree();
    refreshOpenTabs();
    openRequest(restored.id);
    scheduleWorkspaceSave();
    statusBar()->showMessage(entry.configAvailable ? UiLanguage::text("Создан новый запрос из истории. Он готов к редактированию.")
        : UiLanguage::text("Создан запрос из старой истории. Проверьте настройки соединения: полный снимок конфигурации отсутствует."), 10000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    for (RequestSession *value : std::as_const(m_sessions)) value->controller->stop();
    if (!saveAll() && m_restartForLanguage) {
        m_restartForLanguage = false;
        m_closing = false;
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) QCoreApplication::exit(m_restartForLanguage ? UiLanguage::RestartExitCode : 0);
}
