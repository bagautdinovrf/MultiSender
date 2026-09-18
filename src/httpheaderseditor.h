#pragma once

#include "requesttypes.h"

#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;

class HttpHeadersEditor final : public QWidget
{
public:
    explicit HttpHeadersEditor(QWidget *parent = nullptr);

    void setHeaders(const QVector<HttpHeader> &headers);
    [[nodiscard]] QVector<HttpHeader> headers() const;
    void setChangeAction(std::function<void()> action);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void appendRow(const HttpHeader &header);
    void addHeader();
    void removeHeader();
    void updateAppearance();
    void notifyChanged();

    QTableWidget *m_table = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_empty = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    bool m_updating = false;
    std::function<void()> m_changeAction;
};
