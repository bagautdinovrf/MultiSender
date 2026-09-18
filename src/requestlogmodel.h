#pragma once

#include "requesttypes.h"

#include <QAbstractTableModel>
#include <QVector>

class RequestLogModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        TimeColumn,
        IdColumn,
        RequestColumn,
        MethodColumn,
        StatusColumn,
        DurationColumn,
        UrlColumn,
        ColumnCount
    };

    explicit RequestLogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    const RequestLogEntry *entryAt(int row) const;
    int rowForId(const QUuid &id) const;
    const QVector<RequestLogEntry> &entries() const;
    // Zero removes the aggregate limit; individual request journals retain their default limit.
    void setMaximumEntries(int limit);

public slots:
    void upsertEntry(const RequestLogEntry &entry);
    void removeEntry(const QUuid &id);
    void clear();
    void replaceEntries(const QVector<RequestLogEntry> &entries);

private:
    int m_maximumEntries = 1000;
    QVector<RequestLogEntry> m_entries;
};
