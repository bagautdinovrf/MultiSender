#include "uilanguage.h"
#include "requestlogmodel.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>

namespace {

QColor resultColor(RequestResult result)
{
    const bool dark = QGuiApplication::palette().color(QPalette::Base).lightnessF() < 0.5;
    switch (result) {
    case RequestResult::Pending:
        return QColor(dark ? QStringLiteral("#89bdf6") : QStringLiteral("#326fb0"));
    case RequestResult::Success:
        return QColor(dark ? QStringLiteral("#79dfb5") : QStringLiteral("#16794e"));
    case RequestResult::HttpError:
    case RequestResult::NetworkError:
    case RequestResult::SerialError:
    case RequestResult::Cancelled:
        return QColor(dark ? QStringLiteral("#ef9290") : QStringLiteral("#bd4a48"));
    case RequestResult::Skipped:
        return QColor(dark ? QStringLiteral("#baa0e9") : QStringLiteral("#855abd"));
    }

    return QGuiApplication::palette().color(QPalette::Text);
}

} // namespace

RequestLogModel::RequestLogModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int RequestLogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int RequestLogModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RequestLogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const RequestLogEntry &entry = m_entries.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TimeColumn:
            return entry.sentAt.toString(QStringLiteral("HH:mm:ss.zzz"));
        case IdColumn:
            return QStringLiteral("#%1").arg(entry.id);
        case RequestColumn:
            return entry.requestName;
        case MethodColumn:
            return requestOperationName(entry.protocol, entry.method);
        case StatusColumn:
            return requestResultName(entry.result, entry.httpStatus);
        case DurationColumn:
            return entry.result == RequestResult::Pending || entry.result == RequestResult::Skipped
                ? QStringLiteral("—")
                : UiLanguage::text("%1 мс").arg(entry.elapsedMs);
        case UrlColumn:
            return entry.url.toString(QUrl::FullyEncoded);
        default:
            return {};
        }
    }

    if (role == Qt::ForegroundRole && index.column() == StatusColumn) {
        return QBrush(resultColor(entry.result));
    }

    if (role == Qt::FontRole
        && (index.column() == RequestColumn || index.column() == MethodColumn
            || index.column() == StatusColumn)) {
        QFont font;
        font.setWeight(QFont::DemiBold);
        return font;
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == TimeColumn || index.column() == IdColumn || index.column() == MethodColumn
            || index.column() == DurationColumn) {
            return Qt::AlignCenter;
        }
        return QVariant::fromValue(Qt::AlignVCenter | Qt::AlignLeft);
    }

    if (role == Qt::ToolTipRole) {
        if (!entry.errorText.isEmpty()) {
            return entry.errorText;
        }
        return entry.url.toString();
    }

    return {};
}

QVariant RequestLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole
        && (section == TimeColumn || section == DurationColumn)) {
        return Qt::AlignCenter;
    }
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case TimeColumn:
        return UiLanguage::text("Время");
    case IdColumn:
        return QStringLiteral("#");
    case RequestColumn:
        return UiLanguage::text("Запрос");
    case MethodColumn:
        return UiLanguage::text("Протокол / метод");
    case StatusColumn:
        return UiLanguage::text("Статус");
    case DurationColumn:
        return UiLanguage::text("Задержка");
    case UrlColumn:
        return UiLanguage::text("Адрес");
    default:
        return {};
    }
}

const RequestLogEntry *RequestLogModel::entryAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? &m_entries.at(row) : nullptr;
}

int RequestLogModel::rowForId(const QUuid &id) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).entryId == id) {
            return row;
        }
    }
    return -1;
}

void RequestLogModel::upsertEntry(const RequestLogEntry &entry)
{
    const int existingRow = rowForId(entry.entryId);
    if (existingRow >= 0) {
        m_entries[existingRow] = entry;
        emit dataChanged(index(existingRow, 0), index(existingRow, ColumnCount - 1));
        return;
    }

    if (m_maximumEntries > 0 && m_entries.size() >= m_maximumEntries) {
        const int removeCount = m_entries.size() - m_maximumEntries + 1;
        beginRemoveRows({}, 0, removeCount - 1);
        m_entries.remove(0, removeCount);
        endRemoveRows();
    }

    const int newRow = m_entries.size();
    beginInsertRows({}, newRow, newRow);
    m_entries.append(entry);
    endInsertRows();
}

const QVector<RequestLogEntry> &RequestLogModel::entries() const
{
    return m_entries;
}

void RequestLogModel::setMaximumEntries(int limit)
{
    m_maximumEntries = qMax(0, limit);
    if (m_maximumEntries > 0 && m_entries.size() > m_maximumEntries) {
        const int removeCount = m_entries.size() - m_maximumEntries;
        beginRemoveRows({}, 0, removeCount - 1);
        m_entries.remove(0, removeCount);
        endRemoveRows();
    }
}

void RequestLogModel::removeEntry(const QUuid &id)
{
    const int row = rowForId(id);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
}

void RequestLogModel::replaceEntries(const QVector<RequestLogEntry> &entries)
{
    beginResetModel();
    m_entries = m_maximumEntries > 0
        ? entries.mid(qMax(0, entries.size() - m_maximumEntries)) : entries;
    endResetModel();
}

void RequestLogModel::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }

    beginResetModel();
    m_entries.clear();
    endResetModel();
}
