#include "payloadhighlighter.h"

#include "payloadencoding.h"

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QSignalBlocker>
#include <QTimer>
#include <QWidget>

PayloadHighlighter::PayloadHighlighter(QTextDocument *document, QWidget *editor)
    : QSyntaxHighlighter(document), m_editor(editor)
{
    setThemeColors(QGuiApplication::palette().color(QPalette::Base).lightness() < 128);
    if (editor) editor->installEventFilter(this);
}

void PayloadHighlighter::setThemeColors(bool dark)
{
    if (m_themeInitialized && m_dark == dark) return;
    m_themeInitialized = true;
    m_dark = dark;
    m_escapeFormat.setForeground(QColor(dark ? QStringLiteral("#89bdf6") : QStringLiteral("#326fb0")));
    m_escapeFormat.setFontWeight(QFont::Normal);
    m_invalidEscapeFormat.setUnderlineColor(QColor(dark ? QStringLiteral("#ef9290") : QStringLiteral("#bd4a48")));
    m_invalidEscapeFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    m_keyFormat.setForeground(QColor(dark ? QStringLiteral("#89bdf6") : QStringLiteral("#326fb0")));
    m_stringFormat.setForeground(QColor(dark ? QStringLiteral("#79dfb5") : QStringLiteral("#16794e")));
    m_numberFormat.setForeground(QColor(dark ? QStringLiteral("#efbd82") : QStringLiteral("#a56821")));
    m_literalFormat.setForeground(QColor(dark ? QStringLiteral("#baa0e9") : QStringLiteral("#855abd")));
    m_punctuationFormat.setForeground(QColor(dark ? QStringLiteral("#a1aca9") : QStringLiteral("#536b60")));
    if (m_escapesEnabled || m_jsonEnabled) {
        // Updating syntax formats can emit the editor's textChanged signal.
        // A theme change must only repaint, without publishing a request edit.
        const QSignalBlocker blocker(m_editor.data());
        rehighlight();
    }
}

bool PayloadHighlighter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::Show) {
        // Hidden editors catch up when shown, without a global filter per document.
        if (m_editor && m_editor->isVisible() && !m_themeRefreshPending) {
            m_themeRefreshPending = true;
            QTimer::singleShot(0, this, [this] {
                m_themeRefreshPending = false;
                if (m_editor && m_editor->isVisible())
                    setThemeColors(QGuiApplication::palette().color(QPalette::Base).lightness() < 128);
            });
        }
    }
    return QSyntaxHighlighter::eventFilter(watched, event);
}

void PayloadHighlighter::setEscapesEnabled(bool enabled)
{
    if (m_escapesEnabled == enabled) {
        return;
    }
    m_escapesEnabled = enabled;
    rehighlight();
}

void PayloadHighlighter::setJsonEnabled(bool enabled)
{
    if (m_jsonEnabled == enabled) return;
    m_jsonEnabled = enabled;
    rehighlight();
}

void PayloadHighlighter::highlightBlock(const QString &text)
{
    // Large binary/plain-text responses may contain a single huge line. Keep the
    // editor responsive and leave that line undecorated, without truncating data.
    if (text.size() > 100'000) return;

    if (!m_escapesEnabled) {
        if (m_jsonEnabled) highlightJson(text);
        return;
    }

    const SocketPayload payload = parseSocketPayload(QStringView(text));
    for (const PayloadEscapeSpan &escape : payload.escapes) {
        setFormat(escape.start, escape.length,
                  escape.valid ? m_escapeFormat : m_invalidEscapeFormat);
    }
}

void PayloadHighlighter::highlightJson(const QString &text)
{
    const qsizetype length = text.size();
    qsizetype position = 0;
    const auto apply = [this](qsizetype start, qsizetype count, const QTextCharFormat &format) {
        setFormat(static_cast<int>(start), static_cast<int>(count), format);
    };
    while (position < length) {
        const QChar ch = text[position];
        if (ch == u'"') {
            const qsizetype start = position++;
            bool closed = false;
            while (position < length) {
                const QChar current = text[position++];
                if (current == u'\\' && position < length) {
                    ++position;
                } else if (current == u'"') {
                    closed = true;
                    break;
                }
            }
            qsizetype next = position;
            while (next < length && text[next].isSpace()) ++next;
            const bool key = closed && next < length && text[next] == u':';
            apply(start, position - start, key ? m_keyFormat : m_stringFormat);
            continue;
        }
        if (ch == u'{' || ch == u'}' || ch == u'[' || ch == u']'
            || ch == u':' || ch == u',') {
            apply(position++, 1, m_punctuationFormat);
            continue;
        }
        if (ch.isLetter() || ch == u'_') {
            const qsizetype start = position++;
            while (position < length && (text[position].isLetterOrNumber() || text[position] == u'_')) {
                ++position;
            }
            const QStringView word = QStringView(text).sliced(start, position - start);
            if (word == u"true" || word == u"false" || word == u"null") {
                apply(start, position - start, m_literalFormat);
            }
            continue;
        }
        if ((ch >= u'0' && ch <= u'9') || ch == u'-') {
            const qsizetype start = position;
            if (text[position] == u'-') ++position;
            const qsizetype digitsStart = position;
            while (position < length && text[position] >= u'0' && text[position] <= u'9') ++position;
            if (position == digitsStart) continue;
            if (position < length && text[position] == u'.') {
                ++position;
                while (position < length && text[position] >= u'0' && text[position] <= u'9') ++position;
            }
            if (position < length && (text[position] == u'e' || text[position] == u'E')) {
                ++position;
                if (position < length && (text[position] == u'+' || text[position] == u'-')) ++position;
                while (position < length && text[position] >= u'0' && text[position] <= u'9') ++position;
            }
            apply(start, position - start, m_numberFormat);
            continue;
        }
        ++position;
    }
}
