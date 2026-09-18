#pragma once

#include "requesttypes.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>

class QNetworkReply;
class QAbstractSocket;
class QSerialPort;

class RequestController final : public QObject
{
    Q_OBJECT

public:
    explicit RequestController(QObject *parent = nullptr);
    ~RequestController() override;

    bool isRunning() const;
    RequestCounters counters() const;
    void setNextRequestId(quint64 nextId);

public slots:
    void sendOnce(const RequestConfig &config);
    void start(const RequestConfig &config);
    void stop();
    void resetCounters();

signals:
    void requestStarted(const RequestLogEntry &entry);
    void requestCompleted(const RequestLogEntry &entry);
    void requestSkipped(const RequestLogEntry &entry);
    void countersChanged(const RequestCounters &counters);
    void runningChanged(bool running);

private:
    struct ActiveRequest;

    void dispatch(const RequestConfig &config);
    void dispatchHttp(const RequestConfig &config, const RequestLogEntry &entry);
    void dispatchSocket(const RequestConfig &config, const RequestLogEntry &entry);
    void dispatchSerial(const RequestConfig &config, const RequestLogEntry &entry);
    void finishReply(QNetworkReply *reply);
    void readHttpResponse(QNetworkReply *reply);
    void sendSocketPayload(QAbstractSocket *socket);
    void readSocketResponse(QAbstractSocket *socket);
    void finishSocket(QAbstractSocket *socket, RequestResult result,
                      const QString &errorText = {});
    void sendSerialPayload(QSerialPort *port);
    void readSerialResponse(QSerialPort *port);
    void finishSerial(QSerialPort *port, RequestResult result,
                      const QString &errorText = {});
    void recordCompletion(const RequestLogEntry &entry);
    int activeCount() const;
    void emitCounters();

    QNetworkAccessManager m_networkManager;
    QTimer m_timer;
    QHash<QNetworkReply *, ActiveRequest *> m_activeRequests;
    QHash<QAbstractSocket *, ActiveRequest *> m_activeSockets;
    QHash<QSerialPort *, ActiveRequest *> m_activeSerialPorts;
    RequestConfig m_periodicConfig;
    RequestCounters m_counters;
    quint64 m_nextRequestId = 1;
    bool m_running = false;
};
