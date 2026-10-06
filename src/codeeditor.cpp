#include "uilanguage.h"
#include "codeeditor.h"

#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

class CodeEditorGutter final : public QWidget
{
public:
    explicit CodeEditorGutter(CodeEditor *editor)
        : QWidget(editor), m_editor(editor)
    {
        UiLanguage::set(this, "accessibleName", "Номера строк");
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    QSize sizeHint() const override
    {
        return {m_editor->gutterWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->paintGutter(event);
    }

private:
    CodeEditor *m_editor;
};

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent), m_gutter(new CodeEditorGutter(this))
{
    setObjectName(QStringLiteral("codeEditor"));
    setLayoutDirection(Qt::LeftToRight);
    QFont codeFont(QStringLiteral("Consolas"));
    codeFont.setPixelSize(11);
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setStyleStrategy(QFont::PreferAntialias);
    codeFont.setHintingPreference(QFont::PreferNoHinting);
    codeFont.setFixedPitch(true);
    setFont(codeFont);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 2);
    document()->setDocumentMargin(10);
    auto textOptions = document()->defaultTextOption();
    textOptions.setTextDirection(Qt::LeftToRight);
    textOptions.setAlignment(Qt::AlignLeft);
    document()->setDefaultTextOption(textOptions);
    setCenterOnScroll(false);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
        updateGutterGeometry();
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy != 0) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(viewport()->rect())) updateGutterGeometry();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        updateCurrentLine();
        m_gutter->update();
    });
    updateGutterGeometry();
    updateCurrentLine();
}

void CodeEditor::setPreferredHeight(int height)
{
    height = std::max(0, height);
    if (m_preferredHeight == height) return;
    m_preferredHeight = height;
    updateGeometry();
}

QSize CodeEditor::sizeHint() const
{
    QSize size = QPlainTextEdit::sizeHint();
    if (m_preferredHeight > 0) size.setHeight(m_preferredHeight);
    return size;
}

void CodeEditor::setLineNumbersVisible(bool visible)
{
    if (m_lineNumbersVisible == visible) return;
    m_lineNumbersVisible = visible;
    m_gutter->setVisible(visible);
    updateGutterGeometry();
}

bool CodeEditor::lineNumbersVisible() const
{
    return m_lineNumbersVisible;
}

int CodeEditor::gutterWidth() const
{
    if (!m_lineNumbersVisible) return 0;
    const int digits = std::max(2, static_cast<int>(QString::number(blockCount()).size()));
    return 18 + fontMetrics().horizontalAdvance(u'9') * digits;
}

void CodeEditor::updateGutterGeometry()
{
    const int width = gutterWidth();
    setViewportMargins(width, 0, 0, 0);
    // Follow the viewport so the native frame and horizontal scrollbar do not
    // shift line numbers relative to the text.
    const QRect view = viewport()->geometry();
    m_gutter->setGeometry(view.left() - width, view.top(), width, view.height());
    m_gutter->update();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateGutterGeometry();
}

void CodeEditor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    switch (event->type()) {
    case QEvent::LayoutDirectionChange: {
        auto textOptions = document()->defaultTextOption();
        textOptions.setTextDirection(Qt::LeftToRight);
        textOptions.setAlignment(Qt::AlignLeft);
        document()->setDefaultTextOption(textOptions);
        break;
    }
    case QEvent::FontChange:
        setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 2);
        updateGutterGeometry();
        break;
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::EnabledChange:
        updateCurrentLine();
        m_gutter->update();
        break;
    case QEvent::ReadOnlyChange:
        setTabChangesFocus(isReadOnly());
        updateCurrentLine();
        m_gutter->update();
        break;
    case QEvent::StyleChange:
        updateGutterGeometry();
        break;
    default:
        break;
    }
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Backtab || event->key() == Qt::Key_Tab)
        && event->modifiers() == Qt::ShiftModifier) {
        QWidget::focusNextPrevChild(false);
        event->accept();
        return;
    }
    if (!isReadOnly() && event->key() == Qt::Key_Tab
        && event->modifiers() == Qt::NoModifier) {
        insertPlainText(QStringLiteral("  "));
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::updateCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly() && isEnabled()) {
        QTextEdit::ExtraSelection selection;
        QColor tint = palette().color(QPalette::Highlight);
        tint.setAlpha(20);
        selection.format.setBackground(tint);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    setExtraSelections(selections);
}

void CodeEditor::paintGutter(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), palette().color(QPalette::Window));
    painter.setFont(font());
    QColor separator = palette().color(QPalette::PlaceholderText);
    separator.setAlpha(35);
    painter.setPen(separator);
    painter.drawLine(m_gutter->width() - 1, event->rect().top(),
                     m_gutter->width() - 1, event->rect().bottom());

    const int activeBlock = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal height = blockBoundingRect(block).height();
        if (block.isVisible() && top + height >= event->rect().top()) {
            painter.setPen(palette().color(number == activeBlock && !isReadOnly()
                                              ? QPalette::Text : QPalette::PlaceholderText));
            painter.drawText(QRectF(0, top, m_gutter->width() - 9, fontMetrics().height()),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(number + 1));
        }
        top += height;
        block = block.next();
        ++number;
    }
}
