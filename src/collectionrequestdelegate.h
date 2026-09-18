#pragma once

#include <QStyledItemDelegate>

class CollectionRequestDelegate final : public QStyledItemDelegate
{
public:
    explicit CollectionRequestDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;
};
