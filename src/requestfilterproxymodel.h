#pragma once

#include <QSortFilterProxyModel>

enum class LogFilter
{
    All,
    Successful,
    Errors,
    Skipped,
    Other
};

class RequestFilterProxyModel final : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit RequestFilterProxyModel(QObject *parent = nullptr);

    void setSearchText(const QString &text);
    void setResultFilter(LogFilter filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    LogFilter m_resultFilter = LogFilter::All;
};
