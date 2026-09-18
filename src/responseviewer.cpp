#include "uilanguage.h"
#include "responseviewer.h"

#include "apptheme.h"
#include "codeeditor.h"
#include "payloadhighlighter.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabBar>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>

namespace {
constexpr qsizetype PreviewBytes = 256 * 1024;
constexpr int MaximumSearchMatches = 10000;

QString hexDump(const QByteArray &bytes)
{
    QString result;
    result.reserve(((bytes.size() + 15) / 16) * 79);
    for (qsizetype offset = 0; offset < bytes.size(); offset += 16) {
        result += QStringLiteral("%1  ").arg(offset, 8, 16, u'0').toUpper();
        QString ascii;
        ascii.reserve(16);
        for (qsizetype column = 0; column < 16; ++column) {
            if (offset + column < bytes.size()) {
                const auto byte = static_cast<unsigned char>(bytes.at(offset + column));
                result += QStringLiteral("%1 ").arg(byte, 2, 16, u'0').toUpper();
                ascii += byte >= 32 && byte <= 126 ? QChar(byte) : u'.';
            } else {
                result += QStringLiteral("   ");
            }
            if (column == 7) result += u' ';
        }
        result += QStringLiteral(" |%1|\n").arg(ascii.leftJustified(16, u' '));
    }
    if (result.endsWith(u'\n')) result.chop(1);
    return result;
}

QString jsonText(const QByteArray &bytes, QString *error = nullptr)
{
    QJsonParseError parseError;
    const auto value = QJsonValue::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || value.isUndefined()) {
        if (error) *error = UiLanguage::text("Не удалось разобрать JSON (позиция %1)").arg(parseError.offset);
        return QString::fromUtf8(bytes);
    }
    qsizetype estimate = bytes.size();
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (const char byte : bytes) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (byte == '\\') escaped = true;
            else if (byte == '"') quoted = false;
        } else if (byte == '"') quoted = true;
        else if (byte == '{' || byte == '[') { ++depth; estimate += depth * 4 + 1; }
        else if (byte == '}' || byte == ']') { depth = qMax(0, depth - 1); estimate += depth * 4 + 1; }
        else if (byte == ',') estimate += depth * 4 + 1;
        if (estimate > PreviewBytes * 2) {
            if (error) *error = UiLanguage::text("Большой JSON показан без отступов");
            return QString::fromUtf8(bytes);
        }
    }
    QString result = QString::fromUtf8(value.toJson(QJsonValue::JsonFormat::Indented));
    if (result.endsWith(u'\n')) result.chop(1);
    return result;
}
} // namespace

ResponseViewer::ResponseViewer(QWidget *parent) : QWidget(parent)
{
    setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto *toolbarWidget = new QWidget(this);
    toolbarWidget->setObjectName(QStringLiteral("responseToolbar"));
    auto *toolbar = new QHBoxLayout(toolbarWidget);
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(5);
    m_mode = new QTabBar(toolbarWidget);
    m_mode->setObjectName(QStringLiteral("bodyViewMode"));
    m_mode->setAccessibleName(UiLanguage::text("Режим просмотра тела"));
    m_mode->setExpanding(false);
    m_mode->setDrawBase(false);
    m_mode->setUsesScrollButtons(false);
    m_mode->addTab(UiLanguage::text("Текст"));
    m_mode->addTab(QStringLiteral("JSON"));
    m_mode->addTab(QStringLiteral("HEX"));
    m_mode->setToolTip(UiLanguage::text("Текст UTF-8, форматированный JSON или исходные байты"));
    toolbar->addWidget(m_mode);
    m_notice = new QLabel(this);
    m_notice->setObjectName(QStringLiteral("fieldLabel"));
    m_notice->setMinimumWidth(0);
    m_notice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toolbar->addWidget(m_notice, 1);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("bodySearch"));
    m_search->setAccessibleName(UiLanguage::text("Поиск в теле"));
    m_search->setPlaceholderText(UiLanguage::text("Найти · Ctrl+F"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(110);
    m_search->setMaximumWidth(180);
    toolbar->addWidget(m_search);
    m_matches = new QLabel(this);
    m_matches->setObjectName(QStringLiteral("fieldLabel"));
    m_matches->setMinimumWidth(44);
    m_matches->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(m_matches);
    auto makeNavigation = [this, toolbar](const QString &text, const QString &hint) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setObjectName(QStringLiteral("toolbarButton"));
        button->setToolTip(hint);
        button->setAccessibleName(hint);
        toolbar->addWidget(button);
        return button;
    };
    m_previous = makeNavigation(QStringLiteral("↑"), UiLanguage::text("Предыдущее совпадение · Shift+Enter"));
    m_next = makeNavigation(QStringLiteral("↓"), UiLanguage::text("Следующее совпадение · Enter"));
    layout->addWidget(toolbarWidget);

    m_editor = new CodeEditor(this);
    m_editor->setReadOnly(true);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->ensurePolished();
    // Reserve two complete lines even when a restored outer splitter gives the
    // journal its smallest permitted height. Include document insets and a
    // horizontal scrollbar; otherwise the toolbar can consume the entire body.
    m_editor->setMinimumHeight(2 * m_editor->fontMetrics().height()
        + qCeil(2 * m_editor->document()->documentMargin())
        + 2 * m_editor->frameWidth() + m_editor->horizontalScrollBar()->sizeHint().height());
    m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_editor->document()->setUndoRedoEnabled(false);
    m_highlighter = new PayloadHighlighter(m_editor->document());
    m_highlighter->setEscapesEnabled(false);
    m_highlighter->setJsonEnabled(false);
    layout->addWidget(m_editor, 1);

    connect(m_mode, &QTabBar::currentChanged, this, [this] {
        render();
        if (m_modeChanged) m_modeChanged(mode());
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] { updateSearch(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        selectMatch(QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
    });
    connect(m_previous, &QToolButton::clicked, this, [this] { selectMatch(-1); });
    connect(m_next, &QToolButton::clicked, this, [this] { selectMatch(1); });
    auto *find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, [this] { m_search->setFocus(); m_search->selectAll(); });
    updateSearch();
}

void ResponseViewer::setContent(const QByteArray &bytes, const QString &legacyText,
                               bool bytesAvailable, const QString &placeholder)
{
    m_editor->setPlaceholderText(placeholder);
    if (m_hasContent && m_bytes == bytes && m_legacyText == legacyText && m_bytesAvailable == bytesAvailable) return;
    m_bytes = bytes;
    m_legacyText = legacyText;
    m_bytesAvailable = bytesAvailable;
    m_hasContent = true;
    render();
}

void ResponseViewer::clear()
{
    m_hasContent = false;
    m_bytes.clear();
    m_legacyText.clear();
    m_bytesAvailable = false;
    m_editor->clear();
    m_notice->clear();
    updateSearch();
}

void ResponseViewer::setMode(BodyViewMode requested)
{
    const int index = static_cast<int>(requested);
    if (index < 0 || index >= m_mode->count() || index == m_mode->currentIndex()) return;
    const QSignalBlocker blocker(m_mode);
    m_mode->setCurrentIndex(index);
    render();
}

BodyViewMode ResponseViewer::mode() const
{
    return static_cast<BodyViewMode>(m_mode->currentIndex());
}

QByteArray ResponseViewer::fullBytes() const
{
    return m_bytesAvailable ? m_bytes : m_legacyText.toUtf8();
}

QString ResponseViewer::fullDisplayText() const
{
    const auto bytes = fullBytes();
    switch (mode()) {
    case BodyViewMode::Hex: return hexDump(bytes);
    case BodyViewMode::Json:
        // Avoid parsing arbitrarily large bodies just because the user copies them.
        return bytes.size() <= PreviewBytes ? jsonText(bytes) : QString::fromUtf8(bytes);
    case BodyViewMode::Text: return QString::fromUtf8(bytes);
    }
    return {};
}

bool ResponseViewer::hasExactBytes() const { return m_bytesAvailable; }

void ResponseViewer::setModeChanged(std::function<void(BodyViewMode)> callback)
{
    m_modeChanged = std::move(callback);
}

void ResponseViewer::resetScroll()
{
    m_editor->verticalScrollBar()->setValue(0);
    m_editor->horizontalScrollBar()->setValue(0);
}

void ResponseViewer::render()
{
    if (!m_hasContent) return;
    const auto bytes = fullBytes();
    const bool limited = bytes.size() > PreviewBytes;
    const auto preview = limited ? bytes.first(PreviewBytes) : bytes;
    QStringList notices;
    if (!m_bytesAvailable) notices.append(UiLanguage::text("Байты восстановлены из текста"));
    if (limited) notices.append(UiLanguage::text("Первые 256 КиБ из %1 КиБ").arg(bytes.size() / 1024.0, 0, 'f', 1));
    QString rendered;
    if (mode() == BodyViewMode::Hex) {
        rendered = hexDump(preview);
    } else if (mode() == BodyViewMode::Json && !limited && !preview.isEmpty()) {
        QString error;
        rendered = jsonText(preview, &error);
        if (!error.isEmpty()) notices.append(error);
    } else {
        rendered = QString::fromUtf8(preview);
        if (mode() == BodyViewMode::Json && limited)
            notices.append(UiLanguage::text("Большой JSON показан как текст"));
    }
    if (mode() != BodyViewMode::Hex && rendered.size() > PreviewBytes) {
        rendered.truncate(PreviewBytes);
        notices.append(UiLanguage::text("Предпросмотр сокращён после форматирования"));
    }
    // One huge line can make the syntax highlighter expensive. The raw bytes and
    // saved/copied body remain intact even when its preview uses plain text.
    m_highlighter->setJsonEnabled(mode() == BodyViewMode::Json && !limited);
    m_editor->setLineNumbersVisible(mode() != BodyViewMode::Hex);
    if (m_editor->toPlainText() != rendered) {
        const int scroll = m_editor->verticalScrollBar()->value();
        m_editor->setPlainText(rendered);
        m_editor->verticalScrollBar()->setValue(scroll);
    }
    const QString notice = notices.join(QStringLiteral(" · "));
    m_notice->setText(notice);
    m_notice->setToolTip(notice + (!notices.isEmpty()
        ? UiLanguage::text("\nПоиск работает по предпросмотру. Копирование и сохранение используют полное тело.") : QString{}));
    updateSearch();
}

void ResponseViewer::updateSearch()
{
    m_matchPositions.clear();
    m_currentMatch = -1;
    m_moreMatches = false;
    const QString needle = m_search->text();
    if (!needle.isEmpty()) {
        const QString haystack = m_editor->toPlainText();
        qsizetype from = 0;
        while ((from = haystack.indexOf(needle, from, Qt::CaseInsensitive)) >= 0) {
            if (m_matchPositions.size() == MaximumSearchMatches) { m_moreMatches = true; break; }
            m_matchPositions.append(static_cast<int>(from));
            from += needle.size();
        }
    }
    m_previous->setEnabled(!m_matchPositions.isEmpty());
    m_next->setEnabled(!m_matchPositions.isEmpty());
    m_matches->setVisible(!needle.isEmpty());
    m_previous->setVisible(!needle.isEmpty());
    m_next->setVisible(!needle.isEmpty());
    if (!m_matchPositions.isEmpty()) showMatch(0);
    else {
        m_matches->setText(needle.isEmpty() ? QString{} : QStringLiteral("0"));
        auto cursor = m_editor->textCursor();
        cursor.clearSelection();
        m_editor->setTextCursor(cursor);
    }
}

void ResponseViewer::selectMatch(int direction)
{
    if (m_matchPositions.isEmpty()) return;
    const int count = static_cast<int>(m_matchPositions.size());
    showMatch((m_currentMatch + direction + count) % count);
}

void ResponseViewer::showMatch(int index)
{
    if (index < 0 || index >= m_matchPositions.size()) return;
    m_currentMatch = index;
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(m_matchPositions.at(index));
    cursor.setPosition(cursor.position() + static_cast<int>(m_search->text().size()), QTextCursor::KeepAnchor);
    m_editor->setTextCursor(cursor);
    m_editor->ensureCursorVisible();
    m_matches->setText(QStringLiteral("%1/%2%3").arg(index + 1).arg(m_matchPositions.size())
        .arg(m_moreMatches ? QStringLiteral("+") : QString{}));
}
