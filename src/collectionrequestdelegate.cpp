#include "uilanguage.h"
#include "collectionrequestdelegate.h"

#include "apptheme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QWidget>

namespace {

constexpr int OperationRole = Qt::UserRole + 1;
constexpr int EndpointRole = Qt::UserRole + 2;
constexpr int BusyRole = Qt::UserRole + 3;

} // namespace

CollectionRequestDelegate::CollectionRequestDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void CollectionRequestDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
{
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    const QString name = item.text;
    const QString operation = index.data(OperationRole).toString();
    const QString endpoint = index.data(EndpointRole).toString();
    const bool busy = index.data(BusyRole).toBool();
    item.text.clear();
    item.icon = {};
    item.features &= ~(QStyleOptionViewItem::HasDecoration | QStyleOptionViewItem::HasCheckIndicator);
    const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);

    const auto visualRect = [&item](const QRect &rect) {
        return QStyle::visualRect(item.direction, item.rect, rect);
    };
    const QRect content = item.rect.adjusted(10, 0, -12, 0);

    QFont badgeFont = item.font;
    badgeFont.setPixelSize(8);
    badgeFont.setWeight(QFont::DemiBold);
    const int operationWidth = QFontMetrics(badgeFont).horizontalAdvance(operation);
    const int badgeWidth = operationWidth + (qMax(4, 42 - operationWidth) + 1) / 2;
    const QRect badge = visualRect(QRect(content.left(), content.center().y() - 8, badgeWidth, 17));

    painter->save();
    painter->setClipRect(item.rect);
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor tone = AppTheme::operationColor(operation);
    QColor fill = tone;
    fill.setAlpha(AppTheme::isDark() ? 28 : 22);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(QRectF(badge), 4, 4);
    painter->setFont(badgeFont);
    painter->setPen(tone);
    painter->drawText(badge, Qt::AlignCenter, operation);

    QFont statusFont = item.font;
    statusFont.setPixelSize(10);
    statusFont.setWeight(QFont::Normal);
    const QString status = busy ? UiLanguage::text("Работает") : UiLanguage::text("Остановлен");
    const int statusTextWidth = QFontMetrics(statusFont).horizontalAdvance(status);
    const bool showStatusText = content.width() >= 360;
    const int statusWidth = showStatusText ? statusTextWidth + 14 : 8;
    const QRect statusRect = QRect(content.right() - statusWidth + 1, content.top(),
                                    statusWidth, content.height());
    const QRect dot = visualRect(QRect(statusRect.left(), statusRect.center().y() - 3, 6, 6));
    painter->setPen(Qt::NoPen);
    painter->setBrush(AppTheme::color(busy ? u"accent" : u"muted"));
    painter->drawEllipse(QRectF(dot));
    if (showStatusText) {
        painter->setFont(statusFont);
        painter->setPen(AppTheme::color(busy ? u"accent" : u"muted"));
        painter->drawText(visualRect(statusRect.adjusted(14, 0, 0, 0)),
                          Qt::AlignVCenter | Qt::AlignAbsolute | (item.direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft),
                          status);
    }

    const int textLeft = content.left() + badgeWidth + 12;
    const int textWidth = qMax(0, statusRect.left() - 14 - textLeft);
    const Qt::Alignment alignment = Qt::AlignVCenter | Qt::AlignAbsolute
        | (item.direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft);
    QFont nameFont = item.font;
    nameFont.setPixelSize(11);
    nameFont.setWeight(QFont::Medium);
    painter->setFont(nameFont);
    painter->setPen(AppTheme::color(u"text"));
    const QRect nameRect = visualRect(QRect(textLeft, content.center().y() - 20, textWidth, 23));
    painter->drawText(nameRect, alignment, QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, textWidth));

    QFont endpointFont = item.font;
    endpointFont.setPixelSize(10);
    endpointFont.setWeight(QFont::Normal);
    painter->setFont(endpointFont);
    painter->setPen(AppTheme::color(u"muted"));
    const QRect endpointRect = visualRect(QRect(textLeft, content.center().y() + 2, textWidth, 18));
    painter->drawText(endpointRect, alignment,
                      QFontMetrics(endpointFont).elidedText(endpoint, Qt::ElideRight, textWidth));

    painter->setPen(QPen(AppTheme::color(u"lineSoft"), 1));
    painter->drawLine(QPointF(item.rect.left() + 10, item.rect.bottom() + 0.5),
                      QPointF(item.rect.right() - 10, item.rect.bottom() + 0.5));
    painter->restore();
}

QSize CollectionRequestDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    // Content is elided to the viewport, so long names cannot widen the list or its parent.
    return {100, 50};
}
