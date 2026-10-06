#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QPointer>

class QWidget;

class PayloadHighlighter final : public QSyntaxHighlighter
{
public:
    explicit PayloadHighlighter(QTextDocument *document, QWidget *editor);

    void setEscapesEnabled(bool enabled);
    void setJsonEnabled(bool enabled);
    void setThemeColors(bool dark);

protected:
    void highlightBlock(const QString &text) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void highlightJson(const QString &text);

    bool m_escapesEnabled = false;
    bool m_jsonEnabled = false;
    bool m_themeRefreshPending = false;
    bool m_themeInitialized = false;
    bool m_dark = false;
    QPointer<QWidget> m_editor;
    QTextCharFormat m_escapeFormat;
    QTextCharFormat m_invalidEscapeFormat;
    QTextCharFormat m_keyFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_literalFormat;
    QTextCharFormat m_punctuationFormat;
};
