#include "uilanguage.h"
#include "logpane.h"

#include "apptheme.h"
#include "codeeditor.h"
#include "payloadhighlighter.h"
#include "requestfilterproxymodel.h"
#include "requestlogmodel.h"
#include "responseviewer.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QTabBar>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>

namespace {

class JournalView final : public QTableView
{
public:
    using QTableView::QTableView;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTableView::resizeEvent(event);
        setColumnWidth(RequestLogModel::RequestColumn, qBound(185, viewport()->width() * 28 / 100, 330));
    }
};

class JournalDelegate final : public QStyledItemDelegate
{
public:
    JournalDelegate(RequestLogModel *model, RequestFilterProxyModel *proxy, QObject *parent)
        : QStyledItemDelegate(parent), m_model(model), m_proxy(proxy) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const auto *entry = m_model->entryAt(m_proxy->mapToSource(index).row());
        if (!entry) return;
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.state &= ~QStyle::State_HasFocus;
        opt.font.setWeight(QFont::Normal);
        opt.font.setPixelSize(11);
        if (index.column() == RequestLogModel::TimeColumn
            || index.column() == RequestLogModel::DurationColumn
            || index.column() == RequestLogModel::UrlColumn) {
            opt.font.setFamily(QStringLiteral("Consolas"));
            opt.font.setPixelSize(10);
        }
        opt.fontMetrics = QFontMetrics(opt.font);
        if (index.column() == RequestLogModel::TimeColumn || index.column() == RequestLogModel::UrlColumn)
            opt.direction = Qt::LeftToRight;
        const bool custom = index.column() == RequestLogModel::RequestColumn
            || index.column() == RequestLogModel::StatusColumn;
        if (custom) opt.text.clear();
        const QWidget *widget = option.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(
            QStyle::CE_ItemViewItem, &opt, painter, widget);
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        if (index.column() == RequestLogModel::TimeColumn
            && option.state.testFlag(QStyle::State_Selected))
            painter->fillRect(QStyle::visualRect(option.direction, option.rect,
                QRect(option.rect.left(), option.rect.top(), 2, option.rect.height())), AppTheme::color(u"accent"));
        if (custom) {
            QString badge;
            QColor foreground;
            if (index.column() == RequestLogModel::RequestColumn) {
                badge = requestOperationName(entry->protocol, entry->method);
                foreground = AppTheme::operationColor(badge);
            } else {
                switch (entry->result) {
                case RequestResult::Success:
                    badge = entry->httpStatus > 0 ? UiLanguage::text("%1 · Успех").arg(entry->httpStatus) : UiLanguage::text("Успешно");
                    foreground = AppTheme::color(u"accent");
                    break;
                case RequestResult::Pending:
                    badge = UiLanguage::text("Ожидание"); foreground = AppTheme::color(u"blue"); break;
                case RequestResult::Skipped:
                    badge = UiLanguage::text("Пропущено"); foreground = AppTheme::color(u"muted"); break;
                case RequestResult::Cancelled:
                    badge = UiLanguage::text("Отменено"); foreground = AppTheme::color(u"muted"); break;
                default:
                    badge = entry->httpStatus > 0 ? UiLanguage::text("%1 · Ошибка").arg(entry->httpStatus) : UiLanguage::text("Ошибка");
                    foreground = AppTheme::color(u"red"); break;
                }
            }
            QFont badgeFont = option.font;
            badgeFont.setWeight(QFont::Medium);
            badgeFont.setPixelSize(9);
            const QFontMetrics fm(badgeFont);
            const int badgeWidth = index.column() == RequestLogModel::RequestColumn
                ? qMax(51, fm.horizontalAdvance(badge) + 14)
                : qMin(option.rect.width() - 14, fm.horizontalAdvance(badge) + 16);
            const int badgeHeight = qMin(20, option.rect.height() - 16);
            const QRect badgeRect = QStyle::visualRect(option.direction, option.rect,
                QRect(option.rect.left() + 7, option.rect.center().y() - badgeHeight / 2, badgeWidth, badgeHeight));
            QColor background = foreground;
            background.setAlpha(AppTheme::isDark() ? 26 : 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(background);
            painter->drawRoundedRect(badgeRect, 4, 4);
            painter->setPen(foreground);
            painter->setFont(badgeFont);
            painter->drawText(badgeRect, Qt::AlignCenter, fm.elidedText(badge, Qt::ElideRight, badgeWidth - 8));
            if (index.column() == RequestLogModel::RequestColumn) {
                const QRect textRect = QStyle::visualRect(option.direction, option.rect,
                    option.rect.adjusted(badgeWidth + 16, 0, -8, 0));
                painter->setFont(opt.font);
                painter->setPen(AppTheme::color(u"text"));
                painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignAbsolute
                    | (option.direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft),
                    QFontMetrics(opt.font).elidedText(entry->requestName, Qt::ElideRight, textRect.width()));
            }
        }
        painter->setPen(AppTheme::color(u"lineSoft"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->restore();
    }

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &, const QModelIndex &index) override
    {
        const auto *entry = m_model->entryAt(m_proxy->mapToSource(index).row());
        if (!entry) return false;
        QString text = QStringLiteral("%1 / %2\n%3 %4\n#%5 · %6\n%7")
            .arg(entry->collectionName, entry->requestName,
                 requestOperationName(entry->protocol, entry->method), entry->url.toString(QUrl::FullyEncoded),
                 QString::number(entry->id), entry->sentAt.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz")),
                 requestResultName(entry->result, entry->httpStatus));
        if (!entry->errorText.isEmpty()) text += u'\n' + entry->errorText;
        text = text.toHtmlEscaped().replace(u'\n', QStringLiteral("<br>"));
        QToolTip::showText(event->globalPos(), QStringLiteral("<qt>%1</qt>").arg(text), view);
        return true;
    }

private:
    RequestLogModel *m_model;
    RequestFilterProxyModel *m_proxy;
};

class ElidedLabel final : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent) : QLabel(parent)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void setContent(const QString &text)
    {
        m_content = text;
        setToolTip(text);
        setText(fontMetrics().elidedText(m_content, Qt::ElideMiddle, qMax(0, width() - 18)));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(m_content, Qt::ElideMiddle, qMax(0, width() - 18)));
    }

private:
    QString m_content;
};

QLabel *label(const QString &text, const QString &name, QWidget *parent)
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(name);
    return result;
}

QWidget *emptyState(const QString &title, const QString &hint, QWidget *parent,
                    QLabel **titleLabel = nullptr, QLabel **hintLabel = nullptr)
{
    auto *page = new QWidget(parent);
    page->setObjectName(QStringLiteral("logEmptyState"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(5);
    auto *heading = label(title, QStringLiteral("logEmptyTitle"), page);
    auto *description = label(hint, QStringLiteral("emptyHint"), page);
    heading->setAlignment(Qt::AlignCenter);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    layout->addStretch();
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addStretch();
    if (titleLabel) *titleLabel = heading;
    if (hintLabel) *hintLabel = description;
    return page;
}

QString headersText(const QVector<HttpHeader> &headers)
{
    QStringList lines;
    lines.reserve(headers.size());
    for (const auto &header : headers) {
        const QString utf8 = QString::fromUtf8(header.value);
        const QString value = utf8.toUtf8() == header.value ? utf8 : QString::fromLatin1(header.value);
        lines.append(QStringLiteral("%1: %2")
                         .arg(QString::fromLatin1(header.name), value));
    }
    return lines.join(u'\n');
}

void updateEditor(CodeEditor *editor, const QString &text)
{
    if (editor->toPlainText() == text) return;
    const int scroll = editor->verticalScrollBar()->value();
    editor->setPlainText(text);
    editor->verticalScrollBar()->setValue(scroll);
}

QString resultState(RequestResult result)
{
    switch (result) {
    case RequestResult::Success: return QStringLiteral("success");
    case RequestResult::Pending: return QStringLiteral("running");
    case RequestResult::Skipped:
    case RequestResult::Cancelled: return QStringLiteral("idle");
    default: return QStringLiteral("error");
    }
}

QString shellQuote(QString value)
{
    value.replace(u'\'', QStringLiteral("'\\''"));
    return u'\'' + value + u'\'';
}

QString shellHeader(const QByteArray &bytes)
{
    const QString utf8 = QString::fromUtf8(bytes);
    if (utf8.toUtf8() == bytes) return shellQuote(utf8);
    // Non-UTF-8 response/request history can still carry valid HTTP obs-text.
    // printf reconstructs those octets without the clipboard recoding them.
    QString octal;
    octal.reserve(bytes.size() * 4);
    for (const char byte : bytes)
        octal += QStringLiteral("\\%1").arg(static_cast<unsigned char>(byte), 3, 8, u'0');
    return QStringLiteral("\"$(printf '%1')\"").arg(octal);
}

QJsonArray jsonHeaders(const QVector<HttpHeader> &headers, bool config = false)
{
    QJsonArray array;
    for (const auto &header : headers)
        array.append(QJsonObject{{QStringLiteral("name"), config ? QString::fromUtf8(header.name) : QString::fromLatin1(header.name)},
                                 {QStringLiteral("value"), config ? QString::fromUtf8(header.value) : QString::fromLatin1(header.value)},
                                 {QStringLiteral("enabled"), header.enabled}});
    return array;
}

QJsonObject jsonEntry(const RequestLogEntry &entry)
{
    QJsonObject object{
        {QStringLiteral("entryId"), entry.entryId.toString(QUuid::WithoutBraces)},
        {QStringLiteral("id"), QString::number(entry.id)},
        {QStringLiteral("collectionId"), entry.collectionId.toString(QUuid::WithoutBraces)},
        {QStringLiteral("requestId"), entry.requestId.toString(QUuid::WithoutBraces)},
        {QStringLiteral("collectionName"), entry.collectionName},
        {QStringLiteral("requestName"), entry.requestName},
        {QStringLiteral("sentAt"), entry.sentAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), entry.completedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("protocol"), requestProtocolName(entry.protocol)},
        {QStringLiteral("method"), httpMethodName(entry.method)},
        {QStringLiteral("result"), requestResultName(entry.result, entry.httpStatus)},
        {QStringLiteral("httpStatus"), entry.httpStatus},
        {QStringLiteral("url"), entry.url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("elapsedMs"), entry.elapsedMs},
        {QStringLiteral("errorText"), entry.errorText},
        {QStringLiteral("requestBody"), entry.requestBody},
        {QStringLiteral("responseBody"), entry.responseBody},
        {QStringLiteral("requestHeaders"), jsonHeaders(entry.requestHeaders)},
        {QStringLiteral("responseHeaders"), jsonHeaders(entry.responseHeaders)},
        {QStringLiteral("requestBytesAvailable"), entry.requestBytesAvailable},
        {QStringLiteral("responseBytesAvailable"), entry.responseBytesAvailable},
        {QStringLiteral("responseTruncated"), entry.responseTruncated},
        {QStringLiteral("configAvailable"), entry.configAvailable}
    };
    if (entry.requestBytesAvailable)
        object.insert(QStringLiteral("requestBytesBase64"), QString::fromLatin1(entry.requestBytes.toBase64()));
    if (entry.responseBytesAvailable)
        object.insert(QStringLiteral("responseBytesBase64"), QString::fromLatin1(entry.responseBytes.toBase64()));
    if (entry.configAvailable) {
        const auto &config = entry.configSnapshot;
        object.insert(QStringLiteral("configSnapshot"), QJsonObject{
            {QStringLiteral("protocol"), requestProtocolName(config.protocol)},
            {QStringLiteral("host"), config.host}, {QStringLiteral("port"), config.port},
            {QStringLiteral("target"), config.target},
            {QStringLiteral("method"), httpMethodName(config.method)},
            {QStringLiteral("body"), config.body},
            {QStringLiteral("headers"), jsonHeaders(config.headers, true)},
            {QStringLiteral("httpScheme"), config.httpScheme},
            {QStringLiteral("httpTimeoutMs"), config.httpTimeoutMs},
            {QStringLiteral("waitForResponse"), config.waitForResponse},
            {QStringLiteral("timeoutMs"), config.timeoutMs},
            {QStringLiteral("intervalMs"), config.intervalMs},
            {QStringLiteral("maxInFlight"), config.maxInFlight},
            {QStringLiteral("serial"), QJsonObject{
                 {QStringLiteral("portName"), config.serial.portName},
                 {QStringLiteral("baudRate"), config.serial.baudRate},
                 {QStringLiteral("dataBits"), static_cast<int>(config.serial.dataBits)},
                 {QStringLiteral("parity"), static_cast<int>(config.serial.parity)},
                 {QStringLiteral("stopBits"), static_cast<int>(config.serial.stopBits)},
                 {QStringLiteral("flowControl"), static_cast<int>(config.serial.flowControl)}}}
        });
    }
    return object;
}

QString csvCell(QString text)
{
    // Quoting alone does not stop spreadsheet applications interpreting formulas.
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty() && (QStringLiteral("=+-@").contains(trimmed.front())
        || text.startsWith(u'\t') || text.startsWith(u'\r') || text.startsWith(u'\n')))
        text.prepend(u'\'');
    text.replace(u'"', QStringLiteral("\"\""));
    return u'"' + text + u'"';
}

bool saveBytes(QWidget *parent, const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(parent, UiLanguage::text("Не удалось сохранить файл"),
            QStringLiteral("%1\n\n%2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

} // namespace

LogPane::LogPane(RequestLogModel *model, const QByteArray &splitterState, bool autoScroll,
                 bool prettyJson, std::function<void(const QByteArray &)> splitterChanged,
                 std::function<void(bool)> autoScrollChanged,
                 std::function<void(bool)> prettyJsonChanged, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_splitterChanged(std::move(splitterChanged))
    , m_autoScrollChanged(std::move(autoScrollChanged))
    , m_prettyJsonChanged(std::move(prettyJsonChanged))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("journalToolbar"));
    auto *tools = new QHBoxLayout(toolbar);
    tools->setContentsMargins(10, 6, 10, 6);
    tools->setSpacing(8);
    tools->addWidget(label(UiLanguage::text("Журнал"), QStringLiteral("sectionTitle"), toolbar));
    m_count = label(QStringLiteral("0"), QStringLiteral("countBadge"), toolbar);
    tools->addWidget(m_count);
    m_live = label(QStringLiteral("LIVE"), QStringLiteral("statusPill"), toolbar);
    m_live->setProperty("state", QStringLiteral("running"));
    m_live->setToolTip(UiLanguage::text("Есть незавершённые запросы"));
    m_live->hide();
    tools->addWidget(m_live);
    m_statistics = new ElidedLabel(toolbar);
    m_statistics->setObjectName(QStringLiteral("fieldLabel"));
    tools->addWidget(m_statistics, 1);
    m_search = new QLineEdit(toolbar);
    m_search->setPlaceholderText(UiLanguage::text("Поиск в журнале"));
    m_search->setAccessibleName(UiLanguage::text("Поиск в журнале"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(100);
    m_search->setMaximumWidth(180);
    tools->addWidget(m_search);
    m_filter = new QComboBox(toolbar);
    m_filter->setAccessibleName(UiLanguage::text("Фильтр результатов"));
    m_filter->addItem(UiLanguage::text("Все события"), static_cast<int>(LogFilter::All));
    m_filter->addItem(UiLanguage::text("Успешные"), static_cast<int>(LogFilter::Successful));
    m_filter->addItem(UiLanguage::text("Ошибки"), static_cast<int>(LogFilter::Errors));
    m_filter->addItem(UiLanguage::text("Пропущенные"), static_cast<int>(LogFilter::Skipped));
    m_filter->addItem(UiLanguage::text("Прочие"), static_cast<int>(LogFilter::Other));
    tools->addWidget(m_filter);
    auto *journalMenuButton = new QToolButton(toolbar);
    journalMenuButton->setObjectName(QStringLiteral("toolbarButton"));
    journalMenuButton->setText(QStringLiteral("···"));
    journalMenuButton->setToolTip(UiLanguage::text("Настройки и экспорт журнала"));
    journalMenuButton->setAccessibleName(UiLanguage::text("Меню журнала"));
    journalMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto *journalMenu = new QMenu(journalMenuButton);
    m_autoScroll = journalMenu->addAction(UiLanguage::text("Автопрокрутка"));
    m_autoScroll->setCheckable(true);
    m_autoScroll->setToolTip(UiLanguage::text("Показывать последнюю запись и её ответ"));
    m_autoScroll->setChecked(autoScroll);
    journalMenu->addSeparator();
    m_exportJson = journalMenu->addAction(UiLanguage::text("Экспорт показанных записей в JSON…"));
    m_exportCsv = journalMenu->addAction(UiLanguage::text("Экспорт показанных записей в CSV…"));
    journalMenuButton->setMenu(journalMenu);
    m_clear = new QPushButton(AppTheme::icon(u"trash"), UiLanguage::text("Очистить"), toolbar);
    m_clear->setObjectName(QStringLiteral("compactButton"));
    m_clear->setProperty("iconName", QStringLiteral("trash"));
    tools->addWidget(m_clear);
    m_detailsToggle = new QToolButton(toolbar);
    m_detailsToggle->setObjectName(QStringLiteral("toolbarButton"));
    m_detailsToggle->setText(UiLanguage::text("Детали"));
    m_detailsToggle->setCheckable(true);
    m_detailsToggle->setToolTip(UiLanguage::text("Показать или скрыть ответ выбранной записи"));
    m_detailsToggle->setAccessibleName(UiLanguage::text("Показать детали записи"));
    tools->addWidget(m_detailsToggle);
    tools->addWidget(journalMenuButton);
    layout->addWidget(toolbar);

    // Capture the selected identity before the proxy/view discard their indexes.
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_entryBeforeReset = m_selectedEntry;
        m_verticalScrollBeforeReset = m_view->verticalScrollBar()->value();
        m_horizontalScrollBeforeReset = m_view->horizontalScrollBar()->value();
        m_resetting = true;
    });
    m_proxy = new RequestFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setHandleWidth(7);
    m_splitter->setChildrenCollapsible(false);

    auto *tableCard = new QFrame(m_splitter);
    tableCard->setObjectName(QStringLiteral("logTableCard"));
    auto *tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    m_tableStack = new QStackedWidget(tableCard);
    m_tableStack->setMinimumHeight(78);
    m_view = new JournalView(m_tableStack);
    m_view->setObjectName(QStringLiteral("logTable"));
    m_view->setModel(m_proxy);
    m_view->setItemDelegate(new JournalDelegate(m_model, m_proxy, m_view));
    m_view->setAlternatingRowColors(false);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(40);
    m_view->horizontalHeader()->setHighlightSections(false);
    m_view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Keep the outcome beside the timestamp; identifiers stay hidden.
    auto *journalHeader = m_view->horizontalHeader();
    journalHeader->moveSection(journalHeader->visualIndex(RequestLogModel::StatusColumn), 1);
    journalHeader->moveSection(journalHeader->visualIndex(RequestLogModel::DurationColumn),
                               RequestLogModel::ColumnCount - 1);
    journalHeader->setStretchLastSection(false);
    m_view->horizontalHeader()->setSectionResizeMode(RequestLogModel::UrlColumn, QHeaderView::Stretch);
    m_view->setColumnWidth(RequestLogModel::TimeColumn, 120);
    m_view->setColumnHidden(RequestLogModel::IdColumn, true);
    m_view->setColumnHidden(RequestLogModel::MethodColumn, true);
    m_view->setColumnWidth(RequestLogModel::RequestColumn, 210);
    m_view->setColumnWidth(RequestLogModel::StatusColumn, 125);
    m_view->setColumnWidth(RequestLogModel::DurationColumn, qMax(88,
        fontMetrics().horizontalAdvance(m_model->headerData(RequestLogModel::DurationColumn, Qt::Horizontal).toString()) + 24));
    m_tableStack->addWidget(m_view);
    m_tableStack->addWidget(emptyState(UiLanguage::text("История начнётся с первого запроса"),
        UiLanguage::text("Отправьте запрос — здесь появятся статус, время и ответ сервера."),
        m_tableStack, &m_emptyTitle, &m_emptyHint));
    tableLayout->addWidget(m_tableStack);
    m_splitter->addWidget(tableCard);

    auto *detailsCard = new QFrame(m_splitter);
    m_detailsCard = detailsCard;
    detailsCard->setObjectName(QStringLiteral("logDetailsCard"));
    auto *detailsLayout = new QVBoxLayout(detailsCard);
    detailsLayout->setContentsMargins(10, 0, 10, 6);
    detailsLayout->setSpacing(5);
    detailsLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto *detailsHeading = new QWidget(detailsCard);
    detailsHeading->setObjectName(QStringLiteral("detailsHeading"));
    auto *detailsTools = new QHBoxLayout(detailsHeading);
    detailsTools->setContentsMargins(0, 0, 0, 0);
    detailsTools->setSpacing(8);
    m_tabs = new QTabBar(detailsCard);
    m_tabs->setObjectName(QStringLiteral("detailsTabBar"));
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tabs->setMinimumWidth(0);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->addTab(UiLanguage::text("Ответ"));
    m_tabs->addTab(UiLanguage::text("Заголовки"));
    m_tabs->addTab(UiLanguage::text("Запрос"));
    m_tabs->addTab(UiLanguage::text("Заголовки запроса"));
    m_tabs->setTabToolTip(0, UiLanguage::text("Тело ответа"));
    m_tabs->setTabToolTip(1, UiLanguage::text("Заголовки ответа"));
    m_tabs->setTabToolTip(2, UiLanguage::text("Тело запроса"));
    detailsTools->addWidget(m_tabs, 1);
    auto *closeDetails = new QToolButton(detailsHeading);
    closeDetails->setObjectName(QStringLiteral("toolbarButton"));
    closeDetails->setProperty("iconName", QStringLiteral("close"));
    closeDetails->setIcon(AppTheme::icon(u"close"));
    closeDetails->setToolTip(UiLanguage::text("Свернуть детали · запись можно открыть снова щелчком"));
    closeDetails->setAccessibleName(UiLanguage::text("Свернуть детали"));
    detailsTools->addWidget(closeDetails);
    detailsLayout->addWidget(detailsHeading);
    m_copy = new QToolButton(detailsCard);
    m_copy->setObjectName(QStringLiteral("toolbarButton"));
    m_copy->setProperty("iconName", QStringLiteral("copy"));
    m_copy->setIcon(AppTheme::icon(u"copy"));
    m_copy->setIconSize(QSize(16, 16));
    m_copy->setText(UiLanguage::text("Копировать"));
    m_copy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_copy->setToolTip(UiLanguage::text("Копировать содержимое выбранной вкладки"));
    m_copy->setAccessibleName(UiLanguage::text("Копировать содержимое"));
    m_copy->setPopupMode(QToolButton::MenuButtonPopup);
    auto *copyMenu = new QMenu(m_copy);
    m_copyUrl = copyMenu->addAction(UiLanguage::text("Копировать адрес"));
    m_copyRequest = copyMenu->addAction(UiLanguage::text("Копировать тело запроса"));
    m_copyResponse = copyMenu->addAction(UiLanguage::text("Копировать тело ответа"));
    m_copyCurl = copyMenu->addAction(UiLanguage::text("Копировать как cURL (Bash / sh)"));
    copyMenu->addSeparator();
    m_saveBody = copyMenu->addAction(UiLanguage::text("Сохранить тело в файл…"));
    m_replay = copyMenu->addAction(UiLanguage::text("Создать запрос из записи…"));
    m_replay->setToolTip(UiLanguage::text("Открыть новый черновик с параметрами этой отправки"));
    m_copy->setMenu(copyMenu);

    m_detailStack = new QStackedWidget(detailsCard);
    m_detailStack->addWidget(emptyState(UiLanguage::text("Всё об ответе — здесь"),
        UiLanguage::text("Выберите запись в журнале, чтобы посмотреть данные и заголовки."), m_detailStack));
    auto *detailPage = new QWidget(m_detailStack);
    auto *detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(5);
    detailLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto *metadata = new QHBoxLayout;
    metadata->setSpacing(12);
    m_status = label({}, QStringLiteral("statusPill"), detailPage);
    m_timing = label({}, QStringLiteral("detailsMetadata"), detailPage);
    m_size = label({}, QStringLiteral("detailsMetadata"), detailPage);
    m_address = new ElidedLabel(detailPage);
    m_address->setObjectName(QStringLiteral("detailsMetadata"));
    metadata->addWidget(m_status);
    metadata->addWidget(m_timing);
    metadata->addWidget(m_size);
    metadata->addWidget(m_address, 1);
    metadata->addWidget(m_copy);
    m_saveBodyButton = new QToolButton(detailPage);
    m_saveBodyButton->setObjectName(QStringLiteral("toolbarButton"));
    m_saveBodyButton->setText(UiLanguage::text("Сохранить"));
    m_saveBodyButton->setToolTip(UiLanguage::text("Сохранить исходные байты тела в файл"));
    metadata->addWidget(m_saveBodyButton);
    m_replayButton = new QToolButton(detailPage);
    m_replayButton->setObjectName(QStringLiteral("toolbarButton"));
    m_replayButton->setText(UiLanguage::text("В запрос"));
    m_replayButton->setToolTip(UiLanguage::text("Создать новый редактируемый запрос из этой записи"));
    metadata->addWidget(m_replayButton);
    detailLayout->addLayout(metadata);
    m_error = new ElidedLabel(detailPage);
    m_error->setObjectName(QStringLiteral("validationLabel"));
    m_error->setProperty("state", QStringLiteral("invalid"));
    m_error->hide();
    detailLayout->addWidget(m_error);

    m_pages = new QStackedWidget(detailPage);
    auto makeEditor = [this](const QString &placeholder, bool json) {
        auto *editor = new CodeEditor(m_pages);
        editor->setReadOnly(true);
        editor->setObjectName(QStringLiteral("codeEditor"));
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor->setPlaceholderText(placeholder);
        editor->ensurePolished();
        editor->setMinimumHeight(2 * editor->fontMetrics().height()
            + qCeil(2 * editor->document()->documentMargin())
            + 2 * editor->frameWidth() + editor->horizontalScrollBar()->sizeHint().height());
        editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        auto *highlighter = new PayloadHighlighter(editor->document());
        highlighter->setEscapesEnabled(false);
        highlighter->setJsonEnabled(json);
        m_pages->addWidget(editor);
        return editor;
    };
    m_responseBody = new ResponseViewer(m_pages);
    m_responseBody->setMode(prettyJson ? BodyViewMode::Json : BodyViewMode::Text);
    m_pages->addWidget(m_responseBody);
    m_responseHeaders = makeEditor(UiLanguage::text("Заголовки ответа отсутствуют"), false);
    m_requestBody = new ResponseViewer(m_pages);
    m_requestBody->setMode(BodyViewMode::Text);
    m_pages->addWidget(m_requestBody);
    m_requestHeaders = makeEditor(UiLanguage::text("Заголовки запроса отсутствуют"), false);
    detailLayout->addWidget(m_pages, 1);
    m_detailStack->addWidget(detailPage);
    detailsLayout->addWidget(m_detailStack, 1);
    m_splitter->addWidget(detailsCard);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({300, 200});
    if (!splitterState.isEmpty()) m_splitter->restoreState(splitterState);
    m_splitter->setOrientation(Qt::Vertical);
    m_splitter->setHandleWidth(7);
    m_splitter->setChildrenCollapsible(false);
    m_expandedSplitterState = m_splitter->saveState();
    layout->addWidget(m_splitter, 1);

    auto *searchDelay = new QTimer(this);
    searchDelay->setSingleShot(true);
    searchDelay->setInterval(160);
    connect(m_search, &QLineEdit::textChanged, searchDelay, qOverload<>(&QTimer::start));
    connect(searchDelay, &QTimer::timeout, this, [this] {
        m_proxy->setSearchText(m_search->text());
        updateResultCount();
        updateDetails();
    });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_proxy->setResultFilter(static_cast<LogFilter>(m_filter->itemData(index).toInt()));
    });
    connect(m_autoScroll, &QAction::toggled, this, [this](bool checked) {
        if (m_autoScrollChanged) m_autoScrollChanged(checked);
        if (checked) modelReplaced();
    });
    m_responseBody->setModeChanged([this](BodyViewMode mode) {
        if (mode != BodyViewMode::Hex && m_prettyJsonChanged)
            m_prettyJsonChanged(mode == BodyViewMode::Json);
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        updateActions();
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this] { updateDetails(); });
    connect(m_view, &QTableView::clicked, this, [this] { setDetailsOpen(true); });
    connect(m_view, &QTableView::activated, this, [this] { setDetailsOpen(true); });
    connect(m_detailsToggle, &QToolButton::toggled, this, &LogPane::setDetailsOpen);
    connect(closeDetails, &QToolButton::clicked, this, [this] { setDetailsOpen(false); });
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this] { updateResultCount(); updateDetails(); });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        m_resetting = false;
        m_selectedEntry = std::exchange(m_entryBeforeReset, QUuid{});
        modelReplaced();
        if (!m_autoScroll->isChecked()) {
            m_view->verticalScrollBar()->setValue(m_verticalScrollBeforeReset);
            m_view->horizontalScrollBar()->setValue(m_horizontalScrollBeforeReset);
        }
    });
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(m_proxy, signal, this, [this] { updateResultCount(); });
    }
    connect(m_proxy, &QAbstractItemModel::modelReset, this, [this] { updateResultCount(); });
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, [this] { updateResultCount(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { updateResultCount(); updateDetails(); });
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_detailsCard->isVisible()) {
            m_expandedSplitterState = m_splitter->saveState();
            if (m_splitterChanged) m_splitterChanged(m_expandedSplitterState);
        }
    });
    connect(m_clear, &QPushButton::clicked, this, [this] {
        if (m_clearAction) m_clearAction();
    });
    connect(m_copy, &QToolButton::clicked, this, &LogPane::copyCurrentPage);
    connect(m_copyUrl, &QAction::triggered, this, [this] {
        if (const auto *entry = currentEntry())
            QApplication::clipboard()->setText(entry->url.toString(QUrl::FullyEncoded));
    });
    connect(m_copyRequest, &QAction::triggered, this, [this] {
        if (const auto *entry = currentEntry()) QApplication::clipboard()->setText(entry->requestBody);
    });
    connect(m_copyResponse, &QAction::triggered, this, [this] {
        if (const auto *entry = currentEntry()) QApplication::clipboard()->setText(entry->responseBody);
    });
    connect(m_copyCurl, &QAction::triggered, this, &LogPane::copyAsCurl);
    connect(m_saveBody, &QAction::triggered, this, &LogPane::saveCurrentBody);
    connect(m_saveBodyButton, &QToolButton::clicked, m_saveBody, &QAction::trigger);
    connect(m_replayButton, &QToolButton::clicked, m_replay, &QAction::trigger);
    connect(m_replay, &QAction::triggered, this, [this] {
        if (const auto *entry = currentEntry(); entry && m_replayAction) {
            const RequestLogEntry snapshot = *entry;
            m_replayAction(snapshot);
        }
    });
    connect(m_exportJson, &QAction::triggered, this, [this] { exportLog(false); });
    connect(m_exportCsv, &QAction::triggered, this, [this] { exportLog(true); });
    updateResultCount();
    updateDetails();
}

void LogPane::setClearAction(std::function<void()> action)
{
    m_clearAction = std::move(action);
}

void LogPane::setReplayAction(std::function<void(const RequestLogEntry &)> action)
{
    m_replayAction = std::move(action);
    updateActions();
}

void LogPane::entryChanged(const QUuid &entryId)
{
    if (m_autoScroll->isChecked() || m_selectedEntry == entryId)
        selectEntry(entryId, m_autoScroll->isChecked());
    updateResultCount();
    updateDetails();
}

void LogPane::modelReplaced()
{
    updateResultCount();
    if (m_autoScroll->isChecked() && m_proxy->rowCount() > 0) {
        const auto source = m_proxy->mapToSource(m_proxy->index(m_proxy->rowCount() - 1, 0));
        if (const auto *entry = m_model->entryAt(source.row())) selectEntry(entry->entryId);
    } else if (!m_selectedEntry.isNull()) {
        selectEntry(m_selectedEntry, false);
    }
    updateDetails();
}

void LogPane::setClearEnabled(bool enabled)
{
    m_clearAllowed = enabled;
    m_clear->setEnabled(enabled && m_model->rowCount() > 0);
    m_clear->setToolTip(enabled ? UiLanguage::text("Очистить записи этого журнала")
        : UiLanguage::text("Чтобы очистить журнал, сначала остановите активные запросы"));
}

void LogPane::setPrettyJson(bool enabled)
{
    // HEX is a temporary inspection mode and must survive another tab changing
    // the persisted JSON preference.
    if (m_responseBody->mode() != BodyViewMode::Hex)
        m_responseBody->setMode(enabled ? BodyViewMode::Json : BodyViewMode::Text);
}

void LogPane::revealLatestEntry()
{
    if (m_model->rowCount() == 0) return;
    const auto *latest = m_model->entryAt(m_model->rowCount() - 1);
    if (!latest) return;
    const QUuid identity = latest->entryId;
    const auto latestIndex = m_model->index(m_model->rowCount() - 1, RequestLogModel::TimeColumn);
    if (!m_proxy->mapFromSource(latestIndex).isValid()) {
        // An explicit send must reveal its own result, even when the previous
        // journal search/filter excludes this pending entry.
        m_search->clear();
        m_proxy->setSearchText({});
        m_filter->setCurrentIndex(m_filter->findData(static_cast<int>(LogFilter::All)));
    }
    selectEntry(identity);
    setDetailsOpen(true);
}

const RequestLogEntry *LogPane::currentEntry() const
{
    const auto index = m_view->currentIndex();
    return index.isValid() ? m_model->entryAt(m_proxy->mapToSource(index).row()) : nullptr;
}

void LogPane::selectEntry(const QUuid &entryId, bool scrollToEntry)
{
    const int sourceRow = m_model->rowForId(entryId);
    if (sourceRow < 0) return;
    const auto proxy = m_proxy->mapFromSource(m_model->index(sourceRow, RequestLogModel::TimeColumn));
    if (!proxy.isValid()) return;
    const int verticalScroll = m_view->verticalScrollBar()->value();
    const int horizontalScroll = m_view->horizontalScrollBar()->value();
    // QTableView::selectRow() can resolve an invalid leading column in RTL
    // layouts and clear currentIndex. Select the known visible index directly.
    m_view->selectionModel()->setCurrentIndex(proxy,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (scrollToEntry) {
        m_view->scrollTo(proxy, QAbstractItemView::PositionAtBottom);
    } else {
        m_view->verticalScrollBar()->setValue(verticalScroll);
        m_view->horizontalScrollBar()->setValue(horizontalScroll);
    }
}

void LogPane::updateDetails()
{
    if (m_resetting) return;
    const auto *entry = currentEntry();
    updateDetailsVisibility();
    m_detailStack->setCurrentIndex(entry ? 1 : 0);
    m_tabs->setEnabled(entry != nullptr);
    if (!entry) {
        m_selectedEntry = {};
        m_requestBody->clear();
        m_responseBody->clear();
        m_requestHeaders->clear();
        m_responseHeaders->clear();
        updateActions();
        return;
    }
    const bool changed = m_selectedEntry != entry->entryId;
    m_selectedEntry = entry->entryId;
    m_status->setText(requestResultName(entry->result, entry->httpStatus));
    const QString state = resultState(entry->result);
    if (m_status->property("state").toString() != state) {
        m_status->setProperty("state", state);
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
    }
    m_timing->setText(entry->result == RequestResult::Pending || entry->result == RequestResult::Skipped
        ? UiLanguage::text("— мс") : UiLanguage::text("%1 мс").arg(entry->elapsedMs));
    m_timing->setToolTip(UiLanguage::text("Отправлен: %1\nЗавершён: %2")
        .arg(entry->sentAt.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz")),
             entry->completedAt.isValid() ? entry->completedAt.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz"))
                                         : QStringLiteral("—")));
    const auto bytes = entry->responseBytesAvailable ? entry->responseBytes.size() : entry->responseBody.toUtf8().size();
    m_size->setText(bytes < 1024 ? UiLanguage::text("%1 Б").arg(bytes)
                               : UiLanguage::text("%1 КБ").arg(bytes / 1024.0, 0, 'f', 1));
    m_size->setToolTip(entry->responseBytesAvailable
        ? UiLanguage::text("Размер сохранённых байтов ответа")
        : UiLanguage::text("Старая запись: размер текста, повторно закодированного в UTF-8"));
    static_cast<ElidedLabel *>(m_address)->setContent(QStringLiteral("%1  %2")
        .arg(requestOperationName(entry->protocol, entry->method), entry->url.toString(QUrl::FullyEncoded)));
    m_address->setToolTip(UiLanguage::text("%1\nКоллекция: %2\nЗапрос: %3")
        .arg(entry->url.toString(QUrl::FullyEncoded), entry->collectionName, entry->requestName));
    QString errorText = entry->errorText;
    if (entry->responseTruncated)
        errorText = UiLanguage::text("Ответ неполный: сохранён только начальный фрагмент. ") + errorText;
    static_cast<ElidedLabel *>(m_error)->setContent(errorText);
    m_error->setVisible(!errorText.isEmpty());
    m_requestBody->setContent(entry->requestBytes, entry->requestBody, entry->requestBytesAvailable,
                             UiLanguage::text("Тело запроса отсутствует"));
    updateEditor(m_requestHeaders, headersText(entry->requestHeaders));
    m_responseBody->setContent(entry->responseBytes, entry->responseBody, entry->responseBytesAvailable,
        entry->result == RequestResult::Pending ? UiLanguage::text("Ожидаем ответ…") : UiLanguage::text("Тело ответа отсутствует"));
    updateEditor(m_responseHeaders, headersText(entry->responseHeaders));
    if (changed) {
        m_requestBody->resetScroll();
        m_responseBody->resetScroll();
        for (auto *editor : {m_requestHeaders, m_responseHeaders}) {
            editor->verticalScrollBar()->setValue(0);
            editor->horizontalScrollBar()->setValue(0);
        }
    }
    updateActions();
}

void LogPane::updateResultCount()
{
    const int total = m_model->rowCount();
    const int visible = m_proxy->rowCount();
    m_count->setText(total == visible ? QString::number(total) : QStringLiteral("%1 / %2").arg(visible).arg(total));
    m_count->setToolTip(UiLanguage::text("Показано %1 из %2 записей").arg(visible).arg(total));
    int successful = 0;
    int completed = 0;
    int pending = 0;
    qint64 duration = 0;
    for (const auto &entry : m_model->entries())
        if (entry.result == RequestResult::Pending) ++pending;
    for (int row = 0; row < visible; ++row) {
        const auto source = m_proxy->mapToSource(m_proxy->index(row, 0));
        const auto *entry = m_model->entryAt(source.row());
        if (!entry) continue;
        if (entry->result == RequestResult::Success) ++successful;
        if (entry->result != RequestResult::Pending && entry->result != RequestResult::Skipped
            && entry->result != RequestResult::Cancelled) {
            ++completed;
            duration += entry->elapsedMs;
        }
    }
    const QString summary = completed > 0
        ? UiLanguage::text("Успех %1% · %2 мс").arg(100.0 * successful / completed, 0, 'f', 0).arg(duration / completed)
        : UiLanguage::text("Нет завершённых");
    static_cast<ElidedLabel *>(m_statistics)->setContent(summary);
    m_statistics->setToolTip(UiLanguage::text("Среди показанных записей: успешных %1 из %2 завершённых.\n"
        "Среднее время не включает ожидание, отменённые и пропущенные записи.").arg(successful).arg(completed));
    m_live->setVisible(pending > 0);
    m_live->setToolTip(UiLanguage::text("Незавершённых запросов: %1").arg(pending));
    m_tableStack->setCurrentIndex(visible > 0 ? 0 : 1);
    m_emptyTitle->setText(total == 0 ? UiLanguage::text("История начнётся с первого запроса")
                                   : UiLanguage::text("Совпадений не найдено"));
    m_emptyHint->setText(total == 0 ? UiLanguage::text("Отправьте запрос — здесь появятся статус, время и ответ сервера.")
                                  : UiLanguage::text("Попробуйте другой текст поиска или выберите «Все события»."));
    m_clear->setEnabled(m_clearAllowed && total > 0);
    m_exportJson->setEnabled(visible > 0);
    m_exportCsv->setEnabled(visible > 0);
}

void LogPane::updateActions()
{
    const auto *entry = currentEntry();
    m_copy->setEnabled(entry != nullptr);
    m_copyUrl->setEnabled(entry != nullptr);
    m_copyRequest->setEnabled(entry != nullptr);
    m_copyResponse->setEnabled(entry != nullptr);
    m_copyCurl->setEnabled(entry && entry->protocol == RequestProtocol::Http);
    m_saveBody->setEnabled(entry && currentBodyViewer());
    m_replay->setEnabled(entry && entry->configAvailable && static_cast<bool>(m_replayAction));
    m_replay->setToolTip(entry && !entry->configAvailable
        ? UiLanguage::text("В старой записи нет полного снимка параметров. Откройте исходный запрос в коллекции.")
        : UiLanguage::text("Создать новый черновик из параметров этой отправки; отправки не будет"));
    m_saveBodyButton->setEnabled(m_saveBody->isEnabled());
    m_replayButton->setEnabled(m_replay->isEnabled());
    m_replayButton->setToolTip(m_replay->toolTip());
}

void LogPane::setDetailsOpen(bool open)
{
    m_detailsDismissed = !open;
    updateDetailsVisibility();
}

void LogPane::updateDetailsVisibility()
{
    const bool available = currentEntry() != nullptr;
    const bool visible = available && !m_detailsDismissed;
    const QSignalBlocker blocker(m_detailsToggle);
    m_detailsToggle->setEnabled(available);
    m_detailsToggle->setChecked(visible);
    m_detailsToggle->setToolTip(visible ? UiLanguage::text("Свернуть детали и расширить журнал")
                                      : UiLanguage::text("Открыть ответ выбранной записи"));
    if (visible == !m_detailsCard->isHidden()) return;
    if (!visible) {
        if (!m_detailsCard->isHidden()) m_expandedSplitterState = m_splitter->saveState();
        m_detailsCard->hide();
    } else {
        m_detailsCard->show();
        if (!m_expandedSplitterState.isEmpty()) m_splitter->restoreState(m_expandedSplitterState);
    }
}

void LogPane::copyCurrentPage()
{
    if (!currentEntry()) return;
    if (const auto *viewer = currentBodyViewer()) {
        QApplication::clipboard()->setText(viewer->fullDisplayText());
    } else {
        const auto *entry = currentEntry();
        QApplication::clipboard()->setText(headersText(m_tabs->currentIndex() == 1
            ? entry->responseHeaders : entry->requestHeaders));
    }
    m_copy->setText(UiLanguage::text("Скопировано"));
    QTimer::singleShot(1600, this, [this] { m_copy->setText(UiLanguage::text("Копировать")); });
}

ResponseViewer *LogPane::currentBodyViewer() const
{
    if (m_tabs->currentIndex() == 0) return m_responseBody;
    if (m_tabs->currentIndex() == 2) return m_requestBody;
    return nullptr;
}

void LogPane::copyAsCurl()
{
    const auto *entry = currentEntry();
    if (!entry || entry->protocol != RequestProtocol::Http) return;
    const QByteArray body = entry->requestBytesAvailable ? entry->requestBytes : entry->requestBody.toUtf8();
    if (body.contains('\0')) {
        QMessageBox::information(this, UiLanguage::text("Тело содержит нулевые байты"),
            UiLanguage::text("Сохраните тело в файл и используйте curl --data-binary @имя-файла. "
                           "Нулевые байты нельзя передать в аргументах командной строки."));
        return;
    }
    QString command = QStringLiteral("curl --request %1 %2")
        .arg(shellQuote(httpMethodName(entry->method)), shellQuote(entry->url.toString(QUrl::FullyEncoded)));
    if (entry->method == HttpMethod::Head) command += QStringLiteral(" --head");
    if (entry->configAvailable)
        command += QStringLiteral(" --max-time %1").arg(entry->configSnapshot.httpTimeoutMs / 1000.0, 0, 'f', 3);
    for (const auto &header : entry->requestHeaders) {
        const QByteArray value = header.name + ": " + header.value;
        if (value.contains('\0') || value.contains('\r') || value.contains('\n')) {
            QMessageBox::warning(this, UiLanguage::text("Нельзя создать команду cURL"),
                UiLanguage::text("В истории есть заголовок с недопустимыми управляющими байтами."));
            return;
        }
        command += QStringLiteral(" \\\n  --header %1").arg(shellHeader(value));
    }
    if (httpMethodHasBody(entry->method))
        command += QStringLiteral(" \\\n  --data-binary %1").arg(shellQuote(QString::fromUtf8(body)));
    QApplication::clipboard()->setText(command);
}

void LogPane::saveCurrentBody()
{
    const auto *viewer = currentBodyViewer();
    const auto *entry = currentEntry();
    if (!viewer || !entry) return;
    // Snapshot before the native dialog pumps events and new history arrives.
    const QByteArray bytes = viewer->fullBytes();
    const bool response = m_tabs->currentIndex() == 0;
    const QString filename = QStringLiteral("%1-%2.bin")
        .arg(response ? QStringLiteral("response") : QStringLiteral("request"), QString::number(entry->id));
    const QString title = viewer->hasExactBytes()
        ? (response && entry->responseTruncated ? UiLanguage::text("Сохранить полученный фрагмент ответа")
                                              : UiLanguage::text("Сохранить исходные байты тела"))
        : UiLanguage::text("Сохранить тело в UTF-8 (байты восстановлены из текста)");
    const QString path = QFileDialog::getSaveFileName(this, title, filename, UiLanguage::text("Все файлы (*)"));
    if (!path.isEmpty()) saveBytes(this, path, bytes);
}

void LogPane::exportLog(bool csv)
{
    QVector<RequestLogEntry> entries;
    entries.reserve(m_proxy->rowCount());
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const auto index = m_proxy->mapToSource(m_proxy->index(row, 0));
        if (const auto *entry = m_model->entryAt(index.row())) entries.append(*entry);
    }
    if (entries.isEmpty()) return;
    const QString suffix = csv ? QStringLiteral("csv") : QStringLiteral("json");
    const QString path = QFileDialog::getSaveFileName(this,
        UiLanguage::text("Экспорт %1 показанных записей").arg(entries.size()),
        QStringLiteral("multisender-log-%1.%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")), suffix),
        csv ? QStringLiteral("CSV (*.csv)") : QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, UiLanguage::text("Не удалось сохранить журнал"), file.errorString());
        return;
    }
    QProgressDialog progress(UiLanguage::text("Сохраняем журнал…"), UiLanguage::text("Отмена"),
                             0, static_cast<int>(entries.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    bool written = true;
    const auto write = [&file, &written](const QByteArray &bytes) {
        if (written && file.write(bytes) != bytes.size()) written = false;
    };
    if (csv) {
        write(QByteArray::fromHex("efbbbf"));
        write("id,sentAt,completedAt,collection,request,protocol,method,url,status,httpStatus,elapsedMs,error,requestBody,responseBody,responseTruncated\r\n");
        for (int index = 0; index < entries.size() && written; ++index) {
            progress.setValue(index);
            if (progress.wasCanceled()) { file.cancelWriting(); return; }
            const auto &entry = entries.at(index);
            QStringList cells{QString::number(entry.id), entry.sentAt.toString(Qt::ISODateWithMs),
                entry.completedAt.toString(Qt::ISODateWithMs), entry.collectionName, entry.requestName,
                requestProtocolName(entry.protocol), httpMethodName(entry.method), entry.url.toString(QUrl::FullyEncoded),
                requestResultName(entry.result, entry.httpStatus), QString::number(entry.httpStatus),
                QString::number(entry.elapsedMs), entry.errorText, entry.requestBody, entry.responseBody,
                entry.responseTruncated ? QStringLiteral("true") : QStringLiteral("false")};
            for (QString &cell : cells) cell = csvCell(cell);
            write(cells.join(u',').toUtf8() + "\r\n");
        }
    } else {
        write("{\n  \"format\": \"MultiSender.log\",\n  \"schemaVersion\": 1,\n  \"exportedAt\": \"");
        write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8());
        write("\",\n  \"entries\": [\n");
        for (int index = 0; index < entries.size() && written; ++index) {
            progress.setValue(index);
            if (progress.wasCanceled()) { file.cancelWriting(); return; }
            if (index > 0) write(",\n");
            write(QJsonDocument(jsonEntry(entries.at(index))).toJson(QJsonDocument::Compact));
        }
        write("\n  ]\n}\n");
    }
    if (!written || !file.commit()) {
        QMessageBox::warning(this, UiLanguage::text("Не удалось сохранить журнал"),
                             QStringLiteral("%1\n\n%2").arg(path, file.errorString()));
        return;
    }
    progress.setValue(static_cast<int>(entries.size()));
}

void LogPane::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_view) {
        m_view->viewport()->update();
    }
}
