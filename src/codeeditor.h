#pragma once

#include <QPlainTextEdit>

class CodeEditorGutter;

// A plain-text editor: the gutter and syntax decoration never modify its text.
class CodeEditor final : public QPlainTextEdit
{
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void setPreferredHeight(int height);
    [[nodiscard]] QSize sizeHint() const override;
    void setLineNumbersVisible(bool visible);
    [[nodiscard]] bool lineNumbersVisible() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    friend class CodeEditorGutter;

    [[nodiscard]] int gutterWidth() const;
    void updateGutterGeometry();
    void paintGutter(QPaintEvent *event);
    void updateCurrentLine();

    CodeEditorGutter *m_gutter = nullptr;
    int m_preferredHeight = 0;
    bool m_lineNumbersVisible = true;
};
