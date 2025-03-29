#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QDebug>
#include <QThread>

class PingWorker : public QObject {
    Q_OBJECT
public:
    explicit PingWorker(const QString &ip, QObject *parent = nullptr);
    ~PingWorker();
    QString targetIp;

public slots:
    void startPing();
    void stop();

signals:
    void resultReady(QString ip, int latency);

private:
    void ping();      // 单次Ping
    bool running;
    QTimer *timer;
    unsigned short sequence; // ping序列号

};
