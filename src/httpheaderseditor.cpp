#include "uilanguage.h"
#include "httpheaderseditor.h"

#include "apptheme.h"

#include <QBrush>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

bool sameHeaders(const QVector<HttpHeader> &left, const QVector<HttpHeader> &right)
{
    return left.size() == right.size()
        && std::equal(left.cbegin(), left.cend(), right.cbegin(), [](const HttpHeader &a, const HttpHeader &b) {
            return a.name == b.name && a.value == b.value && a.enabled == b.enabled;
        });
}

class HeaderDelegate final : public QStyledItemDelegate
{
public:
    HeaderDelegate(QObject *parent, std::function<void()> edited)
        : QStyledItemDelegate(parent), m_edited(std::move(edited))
    {
        connect(this, &QAbstractItemDelegate::closeEditor, this, [this](QWidget *editor) {
            if (m_editor.data() != editor) return;
            m_editor.clear();
            m_editorIndex = QPersistentModelIndex{};
            // Escape restores the original cell value. Publish that rollback as well.
            if (m_edited) m_edited();
        });
    }

    QString text(const QModelIndex &index) const
    {
        return m_editor && m_editorIndex == index ? m_editor->text() : index.data().toString();
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (index.column() == 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const QRect field = fieldRect(option, index);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hover = option.state.testFlag(QStyle::State_MouseOver);
        const bool enabled = index.siblingAtColumn(0).data(Qt::CheckStateRole).toInt() == Qt::Checked;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(AppTheme::color(selected ? u"accentLine" : hover ? u"muted" : u"line"));
        painter->setBrush(AppTheme::color(u"input"));
        painter->drawRoundedRect(QRectF(field).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        const QString value = index.data().toString();
        painter->setPen(AppTheme::color(value.isEmpty() || !enabled ? u"muted" : u"text"));
        painter->setFont(QFont(QStringLiteral("Consolas"), 9));
        const QRect textRect = field.adjusted(10, 0, -9, 0);
        const QString display = value.isEmpty()
            ? index.column() == 1 ? QStringLiteral("Content-Type") : QStringLiteral("application/json")
            : value;
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(display, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
    {
        editor->setGeometry(fieldRect(option, index));
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *lineEdit = qobject_cast<QLineEdit *>(editor)) {
            lineEdit->setLayoutDirection(Qt::LeftToRight);
            lineEdit->setFont(QFont(QStringLiteral("Consolas"), 9));
            lineEdit->setPlaceholderText(index.column() == 1 ? QStringLiteral("Content-Type")
                                                             : QStringLiteral("application/json"));
            lineEdit->setAccessibleName(index.column() == 1 ? UiLanguage::text("Имя HTTP-заголовка")
                                                           : UiLanguage::text("Значение HTTP-заголовка"));
            lineEdit->setClearButtonEnabled(true);
            // Avoid silently clipping long bearer tokens or other header values at QLineEdit's default.
            lineEdit->setMaxLength(std::numeric_limits<int>::max());
            m_editor = lineEdit;
            m_editorIndex = index;
            // Keep the request model current even when Ctrl+Enter or Ctrl+S is pressed while
            // editing. Do not force setModelData here: it can reset the live editor's undo stack.
            connect(lineEdit, &QLineEdit::textEdited, lineEdit, [this] {
                if (m_edited) m_edited();
            });
        }
        return editor;
    }

private:
    static QRect fieldRect(const QStyleOptionViewItem &option, const QModelIndex &index)
    {
        return option.rect.adjusted(0, 3, index.column() == 1 ? -8 : 0, -3);
    }

    std::function<void()> m_edited;
    mutable QPointer<QLineEdit> m_editor;
    mutable QPersistentModelIndex m_editorIndex;
};

} // namespace

HttpHeadersEditor::HttpHeadersEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("httpHeadersEditor"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 12, 0, 0);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(7);
    m_count = new QLabel(QStringLiteral("0"), this);
    m_count->setObjectName(QStringLiteral("fieldLabel"));
    m_count->setAccessibleName(UiLanguage::text("Число HTTP-заголовков"));

    m_add = new QPushButton(UiLanguage::text("Добавить заголовок"), this);
    m_add->setObjectName(QStringLiteral("toolbarButton"));
    m_add->setProperty("iconName", QStringLiteral("plus"));
    m_add->setIcon(AppTheme::icon(u"plus"));
    m_add->setIconSize(QSize(14, 14));
    m_add->setAccessibleName(UiLanguage::text("Добавить HTTP-заголовок"));
    m_add->setToolTip(UiLanguage::text("Добавить строку HTTP-заголовка"));
    toolbar->addWidget(m_add);

    m_remove = new QPushButton(UiLanguage::text("Удалить"), this);
    m_remove->setObjectName(QStringLiteral("toolbarButton"));
    m_remove->setProperty("iconName", QStringLiteral("trash"));
    m_remove->setIcon(AppTheme::icon(u"trash"));
    m_remove->setIconSize(QSize(14, 14));
    m_remove->setAccessibleName(UiLanguage::text("Удалить выбранный HTTP-заголовок"));
    m_remove->setToolTip(UiLanguage::text("Удалить выбранную строку"));
    m_remove->setEnabled(false);
    toolbar->addWidget(m_remove);
    toolbar->addStretch();
    toolbar->addWidget(m_count);

    m_table = new QTableWidget(0, 3, this);
    m_table->setLayoutDirection(Qt::LeftToRight);
    m_table->setObjectName(QStringLiteral("httpHeadersTable"));
    m_table->setAccessibleName(UiLanguage::text("HTTP-заголовки запроса"));
    m_table->setHorizontalHeaderLabels({QString(), UiLanguage::text("КЛЮЧ"), UiLanguage::text("ЗНАЧЕНИЕ")});
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget#httpHeadersTable { background: transparent; border: none; }"
        "QTableWidget#httpHeadersTable::item { border: none; padding: 0; }"
        "QHeaderView { background: transparent; }"
        "QHeaderView::section { background: transparent; border: none; padding: 0 10px; }"));
    m_table->horizontalHeaderItem(0)->setToolTip(UiLanguage::text("Отметьте заголовки, которые нужно отправить"));
    m_table->horizontalHeaderItem(1)->setToolTip(UiLanguage::text("Имя HTTP-заголовка: латинские буквы, цифры и разрешённые символы"));
    m_table->horizontalHeaderItem(2)->setToolTip(UiLanguage::text("Значение сохраняется и передаётся в UTF-8 без потери символов"));
    m_table->setItemDelegate(new HeaderDelegate(m_table, [this] { notifyChanged(); }));
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::SelectedClicked | QAbstractItemView::AnyKeyPressed);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setTabKeyNavigation(true);
    m_table->setSortingEnabled(false);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->verticalHeader()->setMinimumSectionSize(38);
    m_table->horizontalHeader()->setFixedHeight(24);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(30);
    m_table->setColumnWidth(0, 30);
    m_table->setColumnWidth(1, 220);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_table);

    m_empty = new QLabel(UiLanguage::text("Здесь можно добавить авторизацию, тип содержимого и другие заголовки запроса."), this);
    m_empty->setObjectName(QStringLiteral("fieldLabel"));
    m_empty->setWordWrap(true);
    m_empty->setContentsMargins(0, 5, 0, 8);
    layout->addWidget(m_empty);
    layout->addLayout(toolbar);
    layout->addStretch();
    setToolTip(UiLanguage::text("Неотмеченные заголовки сохраняются, но не отправляются. "
                              "Полностью пустые строки пропускаются."));
    setFocusProxy(m_table);
    QWidget::setTabOrder(m_add, m_remove);
    QWidget::setTabOrder(m_remove, m_table);

    connect(m_add, &QPushButton::clicked, this, [this] { addHeader(); });
    connect(m_remove, &QPushButton::clicked, this, [this] { removeHeader(); });
    connect(m_table, &QTableWidget::currentCellChanged, this, [this] {
        m_remove->setEnabled(m_table->currentRow() >= 0);
    });
    connect(m_table, &QTableWidget::itemClicked, this, [this](QTableWidgetItem *item) {
        if (item && item->column() > 0) m_table->editItem(item);
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this] {
        if (m_updating) {
            return;
        }
        updateAppearance();
        notifyChanged();
    });
    updateAppearance();
}

void HttpHeadersEditor::setHeaders(const QVector<HttpHeader> &newHeaders)
{
    // The owning request editor echoes model changes back into this widget. Keep the live editor,
    // cursor and selection when that echo contains exactly the values already displayed.
    if (sameHeaders(headers(), newHeaders)) {
        return;
    }
    const int currentRow = m_table->currentRow();
    const int currentColumn = m_table->currentColumn();
    const QScopedValueRollback guard(m_updating, true);
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    for (const HttpHeader &header : newHeaders) {
        appendRow(header);
    }
    if (currentRow >= 0 && m_table->rowCount() > 0) {
        m_table->setCurrentCell(qMin(currentRow, m_table->rowCount() - 1), qMax(0, currentColumn));
    }
    updateAppearance();
}

QVector<HttpHeader> HttpHeadersEditor::headers() const
{
    QVector<HttpHeader> result;
    result.reserve(m_table->rowCount());
    const auto *delegate = static_cast<const HeaderDelegate *>(m_table->itemDelegate());
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_table->item(row, 0);
        result.append({delegate->text(m_table->model()->index(row, 1)).toUtf8(),
                       delegate->text(m_table->model()->index(row, 2)).toUtf8(),
                       enabled && enabled->checkState() == Qt::Checked});
    }
    return result;
}

void HttpHeadersEditor::setChangeAction(std::function<void()> action)
{
    m_changeAction = std::move(action);
}

void HttpHeadersEditor::appendRow(const HttpHeader &header)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    enabled->setCheckState(header.enabled ? Qt::Checked : Qt::Unchecked);
    enabled->setToolTip(UiLanguage::text("Отправлять этот заголовок"));
    m_table->setItem(row, 0, enabled);
    auto *name = new QTableWidgetItem(QString::fromUtf8(header.name));
    name->setToolTip(UiLanguage::text("Имя HTTP-заголовка. Для редактирования нажмите F2 или начните ввод."));
    m_table->setItem(row, 1, name);
    auto *value = new QTableWidgetItem(QString::fromUtf8(header.value));
    value->setToolTip(UiLanguage::text("Значение HTTP-заголовка; текст сохраняется в UTF-8."));
    m_table->setItem(row, 2, value);
}

void HttpHeadersEditor::addHeader()
{
    {
        const QScopedValueRollback guard(m_updating, true);
        const QSignalBlocker blocker(m_table);
        appendRow({{}, {}, true});
        m_table->setCurrentCell(m_table->rowCount() - 1, 1);
    }
    updateAppearance();
    notifyChanged();
    m_table->setFocus(Qt::OtherFocusReason);
    m_table->editItem(m_table->currentItem());
}

void HttpHeadersEditor::removeHeader()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    {
        const QScopedValueRollback guard(m_updating, true);
        const QSignalBlocker blocker(m_table);
        m_table->removeRow(row);
        if (m_table->rowCount() > 0) {
            m_table->setCurrentCell(qMin(row, m_table->rowCount() - 1), 1);
        }
    }
    updateAppearance();
    notifyChanged();
    if (m_table->rowCount() > 0) {
        m_table->setFocus(Qt::OtherFocusReason);
    } else {
        m_add->setFocus(Qt::OtherFocusReason);
    }
}

void HttpHeadersEditor::updateAppearance()
{
    if (!m_table) {
        return;
    }
    const QSignalBlocker blocker(m_table);
    int enabledCount = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const bool enabled = m_table->item(row, 0)->checkState() == Qt::Checked;
        enabledCount += enabled;
        const QBrush foreground(AppTheme::color(enabled ? u"text" : u"muted"));
        m_table->item(row, 1)->setForeground(foreground);
        m_table->item(row, 2)->setForeground(foreground);
    }
    m_count->setText(UiLanguage::text("Включено %1 из %2").arg(enabledCount).arg(m_table->rowCount()));
    m_count->setVisible(m_table->rowCount() > 0);
    m_count->setToolTip(UiLanguage::text("Включено: %1 из %2").arg(enabledCount).arg(m_table->rowCount()));
    m_remove->setEnabled(m_table->currentRow() >= 0);
    m_remove->setVisible(m_table->rowCount() > 0);
    m_table->setVisible(m_table->rowCount() > 0);
    m_empty->setVisible(m_table->rowCount() == 0);
    setFocusProxy(m_table->rowCount() > 0 ? static_cast<QWidget *>(m_table)
                                       : static_cast<QWidget *>(m_add));
    m_table->setFixedHeight(m_table->horizontalHeader()->height()
                           + qMin(m_table->rowCount(), 5) * m_table->verticalHeader()->defaultSectionSize());
    m_table->viewport()->update();
}

void HttpHeadersEditor::notifyChanged()
{
    if (!m_updating && m_changeAction) {
        m_changeAction();
    }
}

void HttpHeadersEditor::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        updateAppearance();
    }
}

void HttpHeadersEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Match the 2:3 key/value balance without wasting narrow-window space on a fixed column.
    m_table->setColumnWidth(1, qMax(140, (m_table->viewport()->width() - 30) * 2 / 5));
}
