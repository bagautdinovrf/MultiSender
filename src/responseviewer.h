#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class CodeEditor;
class PayloadHighlighter;
class QTabBar;
class QLabel;
class QLineEdit;
class QToolButton;

enum class BodyViewMode { Text, Json, Hex };

// Raw data stays separate from its bounded, searchable presentation.
class ResponseViewer final : public QWidget
{
public:
    explicit ResponseViewer(QWidget *parent = nullptr);
    void setContent(const QByteArray &bytes, const QString &legacyText, bool bytesAvailable,
                    const QString &placeholder = {});
    void clear();
    void setMode(BodyViewMode mode);
    [[nodiscard]] BodyViewMode mode() const;
    [[nodiscard]] QByteArray fullBytes() const;
    [[nodiscard]] QString fullDisplayText() const;
    [[nodiscard]] bool hasExactBytes() const;
    void setModeChanged(std::function<void(BodyViewMode)> callback);
    void resetScroll();

private:
    void render();
    void updateSearch();
    void selectMatch(int direction);
    void showMatch(int index);

    QByteArray m_bytes;
    QString m_legacyText;
    bool m_bytesAvailable = false;
    bool m_hasContent = false;
    CodeEditor *m_editor = nullptr;
    PayloadHighlighter *m_highlighter = nullptr;
    QTabBar *m_mode = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_matches = nullptr;
    QLabel *m_notice = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
    QVector<int> m_matchPositions;
    int m_currentMatch = -1;
    bool m_moreMatches = false;
    std::function<void(BodyViewMode)> m_modeChanged;
};
