#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkInterface>
#include <QDebug>
#include <QPushButton>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include "pingworker.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class PingWorker;


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    void populateInterfaces();
    QList<QNetworkInterface> interfaces;
    QList<PingWorker*> pingWorkers;
    QTimer *pingTimer;
    QMutex mutex;
    bool isPinging;
    QMap<QString, QList<int>> latencyBuffer;
    int bufferSize;
    void cleanupLatencyBuffer();

private slots:
    void onInterfaceChanged(int index);
    void onIpAddressChanged(int index);
    void on_addTargetButton_clicked();
    void onMoveUpClicked(int row);
    void onMoveDownClicked(int row);
    void on_addAllTargetButton_clicked();
    void on_deleteAllTargetButton_clicked();
    void on_startPingButton_clicked();
    void onPingResult(QString ip, int latency);
    void onDelayModeChanged();
    void onPinToTopClicked(int row);
    void onDeleteClicked(int row);
    void updateTableButtons();

    void on_checkBox_stateChanged(int arg1);
};
#endif // MAINWINDOW_H
