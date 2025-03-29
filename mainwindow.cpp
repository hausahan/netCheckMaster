#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QtConcurrent/QtConcurrent>

// todo
// add sort function，sort by delay.
// fix: when minitor too much ip, some ip will show a error result as timeout


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow),
    pingTimer(new QTimer(this)),
    isPinging(false),
    bufferSize(1)
{
    ui->setupUi(this);

    setWindowTitle("netCheckMaster");

    // 连接信号和槽
    connect(ui->interfaceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInterfaceChanged);
    // 连接 IP 地址选择信号
    connect(ui->ipAddressComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onIpAddressChanged);
    // 触发一次默认选择（可选）
    onInterfaceChanged(ui->interfaceComboBox->currentIndex());

    // 初始化 pingResultArea
    ui->pingResultArea->setColumnCount(6);
    ui->pingResultArea->setHorizontalHeaderLabels({"目标IP", "延迟", "上移", "下移", "置顶", "删除"});
//        ui->pingResultArea->setHorizontalHeaderLabels({"目标IP", "延迟", "上移", "下移"});
    ui->pingResultArea->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    int totalWidth = 850;
    ui->pingResultArea->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed); // 固定模式
    ui->pingResultArea->setColumnWidth(0, totalWidth * 4 / 10); // Target IP: 60% = 360px
    ui->pingResultArea->setColumnWidth(1, totalWidth * 2 / 10); // Latency: 20% = 120px
    ui->pingResultArea->setColumnWidth(2, totalWidth * 1 / 10); // Move Up: 10% = 60px
    ui->pingResultArea->setColumnWidth(3, totalWidth * 1 / 10); // Move Down: 10% = 60px
    ui->pingResultArea->setColumnWidth(4, totalWidth * 1 / 10); // Pin to Top
    ui->pingResultArea->setColumnWidth(5, totalWidth * 1 / 10); // Delete

    populateInterfaces();    // 填充网络接口

    // 连接单选按钮信号
    connect(ui->radioButton1Time, &QRadioButton::toggled, this, &MainWindow::onDelayModeChanged);
    connect(ui->radioButton3Time, &QRadioButton::toggled, this, &MainWindow::onDelayModeChanged);
    connect(ui->radioButton5Time, &QRadioButton::toggled, this, &MainWindow::onDelayModeChanged);
    ui->radioButton1Time->setChecked(true);


}

MainWindow::~MainWindow()
{
    QMutexLocker locker(&mutex);
    for (PingWorker *worker : pingWorkers) {
        worker->stop();
        worker->thread()->quit();
        worker->thread()->wait(); // 等待线程退出
        worker->deleteLater();
    }
    delete ui;
}

void MainWindow::populateInterfaces() {
    interfaces = QNetworkInterface::allInterfaces();
    ui->interfaceComboBox->clear();

    // 调试：输出所有接口
//    qDebug() << "All Network Interfaces:";
    for (const QNetworkInterface &interface : interfaces) {
//        qDebug() << "----------------------------------------";
//        qDebug() << "Name:" << interface.name();
//        qDebug() << "Human Readable Name:" << interface.humanReadableName();
//        qDebug() << "Flags:" << interface.flags();
//        qDebug() << "Hardware Address:" << interface.hardwareAddress();
        QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
//            qDebug() << "  IP:" << entry.ip().toString()
//                     << "Netmask:" << entry.netmask().toString()
//                     << "Protocol:" << (entry.ip().protocol() == QAbstractSocket::IPv4Protocol ? "IPv4" : "IPv6");
        }
    }
//    qDebug() << "----------------------------------------";

    // 只添加活跃接口到 comboBox
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags() & QNetworkInterface::IsUp) { // 只检查 IsUp，移除 IsRunning
            ui->interfaceComboBox->addItem(interface.humanReadableName(), QVariant::fromValue(interface));
        }
    }
}

void MainWindow::onInterfaceChanged(int index) {
    if (index < 0) {
        ui->ipAddressComboBox->clear();
        return;
    }

    // 从 comboBox 的 itemData 中获取接口
    QNetworkInterface selectedInterface = ui->interfaceComboBox->itemData(index).value<QNetworkInterface>();
    QList<QNetworkAddressEntry> entries = selectedInterface.addressEntries();

    ui->ipAddressComboBox->clear();

    // 调试输出选中接口的所有 IP
//    qDebug() << "Selected Interface:" << selectedInterface.humanReadableName();
//    for (const QNetworkAddressEntry &entry : entries) {
//        qDebug() << "  IP:" << entry.ip().toString()
//                 << "Netmask:" << entry.netmask().toString()
//                 << "Protocol:" << (entry.ip().protocol() == QAbstractSocket::IPv4Protocol ? "IPv4" : "IPv6");
//    }

    // 优先添加有效的 IPv4 地址，排除 APIPA
    bool hasValidIp = false;
    for (const QNetworkAddressEntry &entry : entries) {
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            QString ipStr = entry.ip().toString();
            if (!ipStr.startsWith("169.254.")) {
                ui->ipAddressComboBox->addItem(ipStr);
                hasValidIp = true;
            }
        }
    }

    // 如果没有有效 IP，再添加其他 IPv4 地址
    if (!hasValidIp) {
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ui->ipAddressComboBox->addItem(entry.ip().toString());
            }
        }
    }

    // 如果需要，也可以添加 IPv6 地址（可选）
    for (const QNetworkAddressEntry &entry : entries) {
        if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol) {
            ui->ipAddressComboBox->addItem(entry.ip().toString());
        }
    }

    // 如果仍为空，显示提示
    if (ui->ipAddressComboBox->count() == 0) {
        ui->ipAddressComboBox->addItem("No valid IP addresses found");
    }
}

void MainWindow::onIpAddressChanged(int index) {
    if (index < 0) {
        ui->targetIPPart1->clear();
        ui->targetIPPart2->clear();
        ui->targetIPPart3->clear();
        ui->targetIPPart4->clear();
        return;
    }

    // 获取选中的 IP 地址
    QString ipAddress = ui->ipAddressComboBox->currentText();

    // 分割 IP 地址为四个部分
    QStringList ipParts = ipAddress.split(".");
    if (ipParts.size() == 4) {
        ui->targetIPPart1->setText(ipParts[0]); // 第一部分，例如 "192"
        ui->targetIPPart2->setText(ipParts[1]); // 第二部分，例如 "168"
        ui->targetIPPart3->setText(ipParts[2]); // 第三部分，例如 "1"
        ui->targetIPPart4->setText(ipParts[3]); // 第四部分，例如 "4"
    } else {
        // 如果 IP 格式不正确，清空所有框
        ui->targetIPPart1->clear();
        ui->targetIPPart2->clear();
        ui->targetIPPart3->clear();
        ui->targetIPPart4->clear();
    }
}

void MainWindow::onMoveUpClicked(int row)
{
    if (row <= 0 || row >= ui->pingResultArea->rowCount()) return;

    QString ipCurrent = ui->pingResultArea->item(row, 0)->text();
    QString latencyCurrent = ui->pingResultArea->item(row, 1)->text();
    QString ipAbove = ui->pingResultArea->item(row - 1, 0)->text();
    QString latencyAbove = ui->pingResultArea->item(row - 1, 1)->text();

    ui->pingResultArea->setItem(row, 0, new QTableWidgetItem(ipAbove));
    ui->pingResultArea->setItem(row, 1, new QTableWidgetItem(latencyAbove));
    ui->pingResultArea->setItem(row - 1, 0, new QTableWidgetItem(ipCurrent));
    ui->pingResultArea->setItem(row - 1, 1, new QTableWidgetItem(latencyCurrent));

    updateTableButtons();
}

void MainWindow::onMoveDownClicked(int row)
{
    if (row < 0 || row >= ui->pingResultArea->rowCount() - 1) return;

    QString ipCurrent = ui->pingResultArea->item(row, 0)->text();
    QString latencyCurrent = ui->pingResultArea->item(row, 1)->text();
    QString ipBelow = ui->pingResultArea->item(row + 1, 0)->text();
    QString latencyBelow = ui->pingResultArea->item(row + 1, 1)->text();

    ui->pingResultArea->setItem(row, 0, new QTableWidgetItem(ipBelow));
    ui->pingResultArea->setItem(row, 1, new QTableWidgetItem(latencyBelow));
    ui->pingResultArea->setItem(row + 1, 0, new QTableWidgetItem(ipCurrent));
    ui->pingResultArea->setItem(row + 1, 1, new QTableWidgetItem(latencyCurrent));

    updateTableButtons();
}
void MainWindow::on_addTargetButton_clicked()
{
    QString ip = ui->targetIPPart1->text() + "." +
                 ui->targetIPPart2->text() + "." +
                 ui->targetIPPart3->text() + "." +
                 ui->targetIPPart4->text();

    QStringList ipParts = ip.split(".");
    if (ipParts.size() != 4) {
        qDebug() << "Invalid IP format:" << ip;
        return;
    }
    bool valid = true;
    for (const QString &part : ipParts) {
        bool ok;
        int value = part.toInt(&ok);
        if (!ok || value < 0 || value > 255) {
            valid = false;
            break;
        }
    }
    if (!valid) {
        qDebug() << "Invalid IP values:" << ip;
        return;
    }

    int existingRow = -1;
    for (int row = 0; row < ui->pingResultArea->rowCount(); ++row) {
        if (ui->pingResultArea->item(row, 0)->text() == ip) {
            existingRow = row;
            break;
        }
    }

    if (existingRow != -1) {
        if (existingRow != 0) {
            QString latency = ui->pingResultArea->item(existingRow, 1)->text();
            ui->pingResultArea->removeRow(existingRow);
            ui->pingResultArea->insertRow(0);
            ui->pingResultArea->setItem(0, 0, new QTableWidgetItem(ip));
            ui->pingResultArea->setItem(0, 1, new QTableWidgetItem(latency));
            updateTableButtons();
        }
        return;
    }

    int row = ui->pingResultArea->rowCount();
    ui->pingResultArea->insertRow(row);
    ui->pingResultArea->setItem(row, 0, new QTableWidgetItem(ip));
    ui->pingResultArea->setItem(row, 1, new QTableWidgetItem("N/A"));

    updateTableButtons();
}

// 更新所有按钮的连接
void MainWindow::updateTableButtons()
{
    for (int row = 0; row < ui->pingResultArea->rowCount(); ++row) {
        // 上移按钮
        QPushButton *upButton = new QPushButton("↑", this);
        upButton->setMaximumSize(85, 60);
        connect(upButton, &QPushButton::clicked, this, [this]() {
            QPushButton *btn = qobject_cast<QPushButton*>(sender());
            if (btn) {
                int row = ui->pingResultArea->indexAt(btn->pos()).row();
                if (row != -1) onMoveUpClicked(row);
            }
        });
        ui->pingResultArea->setCellWidget(row, 2, upButton);

        // 下移按钮
        QPushButton *downButton = new QPushButton("↓", this);
        downButton->setMaximumSize(85, 60);
        connect(downButton, &QPushButton::clicked, this, [this]() {
            QPushButton *btn = qobject_cast<QPushButton*>(sender());
            if (btn) {
                int row = ui->pingResultArea->indexAt(btn->pos()).row();
                if (row != -1) onMoveDownClicked(row);
            }
        });
        ui->pingResultArea->setCellWidget(row, 3, downButton);

        // 置顶按钮
        QPushButton *pinToTopButton = new QPushButton("置顶", this);
        pinToTopButton->setMaximumSize(85, 60);
        connect(pinToTopButton, &QPushButton::clicked, this, [this]() {
            QPushButton *btn = qobject_cast<QPushButton*>(sender());
            if (btn) {
                int row = ui->pingResultArea->indexAt(btn->pos()).row();
                if (row != -1) onPinToTopClicked(row);
            }
        });
        ui->pingResultArea->setCellWidget(row, 4, pinToTopButton);

        // 删除按钮
        QPushButton *deleteButton = new QPushButton("删除", this);
        deleteButton->setMaximumSize(85, 60);
        connect(deleteButton, &QPushButton::clicked, this, [this]() {
            QPushButton *btn = qobject_cast<QPushButton*>(sender());
            if (btn) {
                int row = ui->pingResultArea->indexAt(btn->pos()).row();
                if (row != -1) onDeleteClicked(row);
            }
        });
        ui->pingResultArea->setCellWidget(row, 5, deleteButton);
    }
}

// 置顶功能
void MainWindow::onPinToTopClicked(int row)
{
    if (row <= 0 || row >= ui->pingResultArea->rowCount()) return;

    QString ip = ui->pingResultArea->item(row, 0)->text();
    QString latency = ui->pingResultArea->item(row, 1)->text();

    ui->pingResultArea->removeRow(row);
    ui->pingResultArea->insertRow(0);
    ui->pingResultArea->setItem(0, 0, new QTableWidgetItem(ip));
    ui->pingResultArea->setItem(0, 1, new QTableWidgetItem(latency));

    updateTableButtons();
    qDebug() << "Pinned IP" << ip << "to top";
}

// 删除功能
void MainWindow::onDeleteClicked(int row)
{
    if (row < 0 || row >= ui->pingResultArea->rowCount()) return;

    QString ip = ui->pingResultArea->item(row, 0)->text();

    QMutexLocker locker(&mutex);
    for (int i = 0; i < pingWorkers.size(); ++i) {
        PingWorker *worker = pingWorkers[i];
        if (worker->targetIp == ip) {
            worker->stop();
            worker->thread()->quit();
            worker->thread()->wait();
            worker->deleteLater();
            pingWorkers.removeAt(i);
            break;
        }
    }

    ui->pingResultArea->removeRow(row);
    updateTableButtons();
    cleanupLatencyBuffer();
    qDebug() << "Deleted IP" << ip << "from table";
}

void MainWindow::on_addAllTargetButton_clicked()
{
    // 获取当前选中的接口和 IP
    int interfaceIndex = ui->interfaceComboBox->currentIndex();
    if (interfaceIndex < 0) return;

    QNetworkInterface selectedInterface = ui->interfaceComboBox->itemData(interfaceIndex).value<QNetworkInterface>();
    QString selectedIp = ui->ipAddressComboBox->currentText();

    // 验证 IP 格式
    QStringList ipParts = selectedIp.split(".");
    if (ipParts.size() != 4) {
        qDebug() << "Invalid IP format:" << selectedIp;
        return;
    }

    // 获取子网掩码（从接口的 addressEntries 中查找匹配的 IP）
    QString subnetMask;
    QList<QNetworkAddressEntry> entries = selectedInterface.addressEntries();
    for (const QNetworkAddressEntry &entry : entries) {
        if (entry.ip().toString() == selectedIp && entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            subnetMask = entry.netmask().toString();
            break;
        }
    }
    if (subnetMask.isEmpty()) {
        qDebug() << "No subnet mask found for IP:" << selectedIp;
        return;
    }

    // 假设子网掩码是 255.255.255.0（即只扫描最后一部分 1-255）
    // 如果需要支持其他子网掩码，可以进一步解析
    if (subnetMask != "255.255.255.0") {
        qDebug() << "Only 255.255.255.0 subnet mask is supported for now";
        return;
    }

    // 生成并添加 1-255 的 IP
    for (int i = 1; i <= 255; ++i) {
        QString targetIp = QString("%1.%2.%3.%4")
            .arg(ipParts[0])
            .arg(ipParts[1])
            .arg(ipParts[2])
            .arg(i);

        // 检查是否已存在
        int existingRow = -1;
        for (int row = 0; row < ui->pingResultArea->rowCount(); ++row) {
            if (ui->pingResultArea->item(row, 0)->text() == targetIp) {
                existingRow = row;
                break;
            }
        }

        if (existingRow != -1) {
            // 如果已存在，移动到第一行
            if (existingRow != 0) {
                QString latency = ui->pingResultArea->item(existingRow, 1)->text();
                ui->pingResultArea->removeRow(existingRow);

                ui->pingResultArea->insertRow(0);
                ui->pingResultArea->setItem(0, 0, new QTableWidgetItem(targetIp));
                ui->pingResultArea->setItem(0, 1, new QTableWidgetItem(latency));

                QPushButton *upButton = new QPushButton("↑", this);
                upButton->setMaximumSize(85, 60);
                connect(upButton, &QPushButton::clicked, this, [this]() { onMoveUpClicked(0); });
                ui->pingResultArea->setCellWidget(0, 2, upButton);

                QPushButton *downButton = new QPushButton("↓", this);
                downButton->setMaximumSize(85, 60);
                connect(downButton, &QPushButton::clicked, this, [this]() { onMoveDownClicked(0); });
                ui->pingResultArea->setCellWidget(0, 3, downButton);
            }
        } else {
            // 如果不存在，添加新行
            int row = ui->pingResultArea->rowCount();
            ui->pingResultArea->insertRow(row);

            ui->pingResultArea->setItem(row, 0, new QTableWidgetItem(targetIp));
            ui->pingResultArea->setItem(row, 1, new QTableWidgetItem("N/A"));

            QPushButton *upButton = new QPushButton("↑", this);
            upButton->setMaximumSize(85, 60);
            connect(upButton, &QPushButton::clicked, this, [this, row]() { onMoveUpClicked(row); });
            ui->pingResultArea->setCellWidget(row, 2, upButton);

            QPushButton *downButton = new QPushButton("↓", this);
            downButton->setMaximumSize(85, 60);
            connect(downButton, &QPushButton::clicked, this, [this, row]() { onMoveDownClicked(row); });
            ui->pingResultArea->setCellWidget(row, 3, downButton);
        }
    }
    updateTableButtons();

}

void MainWindow::on_deleteAllTargetButton_clicked()
{
    ui->pingResultArea->setRowCount(0);
}

void MainWindow::onPingResult(QString ip, int latency)
{
    // 添加到缓冲区
    if (!latencyBuffer.contains(ip)) {
        latencyBuffer[ip] = QList<int>();
    }
    QList<int> &buffer = latencyBuffer[ip];
    buffer.append(latency);
    while (buffer.size() > bufferSize) {
        buffer.removeFirst();
    }

    // 根据模式计算显示值
    QString displayText;
    if (bufferSize == 1) {
        // 显示最新一次
        displayText = (latency >= 0) ? QString("%1 ms").arg(latency) : "Timeout";
    } else {
        // 计算平均值
        int validCount = 0;
        int sum = 0;
        for (int lat : buffer) {
            if (lat >= 0) {
                sum += lat;
                validCount++;
            }
        }
        if (validCount > 0) {
            int avgLatency = sum / validCount;
            displayText = QString("%1 ms").arg(avgLatency);
        } else {
            displayText = "Timeout";
        }
    }

    // 更新表格
    for (int row = 0; row < ui->pingResultArea->rowCount(); ++row) {
        if (ui->pingResultArea->item(row, 0)->text() == ip) {
            ui->pingResultArea->setItem(row, 1, new QTableWidgetItem(displayText));
//            qDebug() << "Ping result for" << ip << ":" << latency << ", displayed:" << displayText;
            break;
        }
    }
}

void MainWindow::on_startPingButton_clicked()
{
    if (ui->startPingButton->text() == "开始") {
        ui->startPingButton->setText("停止");
        isPinging = true;

        QMutexLocker locker(&mutex);
        for (PingWorker *worker : pingWorkers) {
            worker->stop();
            worker->thread()->quit();
            worker->thread()->wait();
            worker->deleteLater();
        }
        pingWorkers.clear();

        for (int row = 0; row < ui->pingResultArea->rowCount(); ++row) {
            QString ip = ui->pingResultArea->item(row, 0)->text();
            PingWorker *worker = new PingWorker(ip);
            QThread *thread = new QThread(this);
            worker->moveToThread(thread);

            connect(thread, &QThread::started, worker, &PingWorker::startPing);
            connect(worker, &PingWorker::resultReady, this, &MainWindow::onPingResult);
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);

            pingWorkers.append(worker);
            thread->start();
        }
        updateTableButtons();
    } else {
        ui->startPingButton->setText("开始");
        isPinging = false;

        QMutexLocker locker(&mutex);
        for (PingWorker *worker : pingWorkers) {
            worker->stop();
            worker->thread()->quit();
            worker->thread()->wait();
            worker->deleteLater();
        }
        pingWorkers.clear();

        updateTableButtons();
    }
}

void MainWindow::cleanupLatencyBuffer()
{
    QStringList activeIPs;
    for (int row = 0; row < ui->pingResultArea->rowCount(); ++row) {
        activeIPs.append(ui->pingResultArea->item(row, 0)->text());
    }

    QMutableMapIterator<QString, QList<int>> it(latencyBuffer);
    while (it.hasNext()) {
        it.next();
        if (!activeIPs.contains(it.key())) {
            qDebug() << "Removing buffer for inactive IP:" << it.key();
            it.remove();
        }
    }
}

// 处理延迟模式切换
void MainWindow::onDelayModeChanged()
{
    if (ui->radioButton1Time->isChecked()) {
        bufferSize = 1;
    } else if (ui->radioButton3Time->isChecked()) {
        bufferSize = 3;
    } else if (ui->radioButton5Time->isChecked()) {
        bufferSize = 5;
    }
    qDebug() << "Delay mode changed to buffer size:" << bufferSize;

    // 更新所有 IP 的显示
    for (const QString &ip : latencyBuffer.keys()) {
        onPingResult(ip, latencyBuffer[ip].isEmpty() ? -1 : latencyBuffer[ip].last());
    }
}

// 窗口置顶
void MainWindow::on_checkBox_stateChanged(int arg1)
{
    Qt::WindowFlags flags = windowFlags();
    if (arg1 == Qt::Checked) { // 勾选状态
        flags |= Qt::WindowStaysOnTopHint; // 添加置顶标志
        setWindowFlags(flags);
        show(); // 重新显示窗口以应用标志
        qDebug() << "Window set to always on top";
    } else { // 未勾选状态
        flags &= ~Qt::WindowStaysOnTopHint; // 移除置顶标志
        setWindowFlags(flags);
        show(); // 重新显示窗口以应用标志
        qDebug() << "Window set to normal";
    }
}
