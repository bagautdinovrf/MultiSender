#include "requestfilterproxymodel.h"

#include "requestlogmodel.h"

RequestFilterProxyModel::RequestFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void RequestFilterProxyModel::setSearchText(const QString &text)
{
    const QString search = text.trimmed();
    if (m_searchText == search) return;
    m_searchText = search;
    invalidateRowsFilter();
}

void RequestFilterProxyModel::setResultFilter(LogFilter filter)
{
    if (m_resultFilter == filter) return;
    m_resultFilter = filter;
    invalidateRowsFilter();
}

bool RequestFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                const QModelIndex &sourceParent) const
{
    const auto *logModel = qobject_cast<const RequestLogModel *>(sourceModel());
    if (!logModel || sourceParent.isValid()) {
        return true;
    }

    const RequestLogEntry *entry = logModel->entryAt(sourceRow);
    if (!entry) {
        return false;
    }

    bool resultMatches = false;
    switch (m_resultFilter) {
    case LogFilter::All:
        resultMatches = true;
        break;
    case LogFilter::Successful:
        resultMatches = entry->result == RequestResult::Success;
        break;
    case LogFilter::Errors:
        resultMatches = entry->result == RequestResult::HttpError
            || entry->result == RequestResult::NetworkError
            || entry->result == RequestResult::SerialError;
        break;
    case LogFilter::Skipped:
        resultMatches = entry->result == RequestResult::Skipped;
        break;
    case LogFilter::Other:
        resultMatches = entry->result == RequestResult::Pending
            || entry->result == RequestResult::Cancelled
            || entry->result == RequestResult::Skipped;
        break;
    }

    if (!resultMatches || m_searchText.isEmpty()) {
        return resultMatches;
    }

    const auto matches = [this](const QString &value) {
        return value.contains(m_searchText, Qt::CaseInsensitive);
    };
    // Search each field directly instead of duplicating potentially large bodies
    // into a combined temporary string for every keystroke.
    if (matches(QString::number(entry->id)) || matches(entry->collectionName)
        || matches(entry->requestName) || matches(requestProtocolName(entry->protocol))
        || matches(requestOperationName(entry->protocol, entry->method))
        || matches(requestResultName(entry->result, entry->httpStatus))
        || matches(entry->url.toString()) || matches(entry->errorText)
        || matches(entry->requestBody) || matches(entry->responseBody)) return true;
    const auto headerMatches = [&matches](const QVector<HttpHeader> &headers) {
        for (const auto &header : headers)
            if (matches(QString::fromLatin1(header.name) + QStringLiteral(": ") + QString::fromLatin1(header.value)))
                return true;
        return false;
    };
    return headerMatches(entry->requestHeaders) || headerMatches(entry->responseHeaders);
}
