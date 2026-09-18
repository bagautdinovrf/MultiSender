#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>
#include <QVector>

struct PayloadEscapeSpan
{
    int start = 0;
    int length = 0;
    bool valid = true;
};

struct SocketPayload
{
    QByteArray bytes;
    QVector<PayloadEscapeSpan> escapes;
    QString error;
};

[[nodiscard]] SocketPayload parseSocketPayload(QStringView text);
