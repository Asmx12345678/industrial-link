#include "mainwindow.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QWidget* metricCard(const QString& title, QLabel*& value, QWidget* parent) {
    auto* card = new QGroupBox(parent);
    card->setObjectName("metricCard");
    auto* layout = new QVBoxLayout(card);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("metricTitle");
    value = new QLabel("—", card);
    value->setObjectName("metricValue");
    layout->addWidget(titleLabel);
    layout->addWidget(value);
    return card;
}

QString formatBytes(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024) return QString::number(bytes / 1024.0 / 1024 / 1024, 'f', 2) + " GB";
    if (bytes >= 1024LL * 1024) return QString::number(bytes / 1024.0 / 1024, 'f', 2) + " MB";
    if (bytes >= 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}
}  // namespace

MainWindow::MainWindow(QUrl baseUrl, QWidget* parent)
    : QMainWindow(parent), baseUrl_(std::move(baseUrl)) {
    buildUi();
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
    timer_->start();
    refresh();
}

void MainWindow::buildUi() {
    setWindowTitle("IndustrialLink Monitor");
    resize(1180, 760);
    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel("IndustrialLink  工业设备监控", root);
    title->setObjectName("pageTitle");
    connectionLabel_ = new QLabel("正在连接…", root);
    connectionLabel_->setObjectName("connectionState");
    auto* refreshButton = new QPushButton("立即刷新", root);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    header->addWidget(title);
    header->addStretch();
    header->addWidget(connectionLabel_);
    header->addWidget(refreshButton);
    rootLayout->addLayout(header);

    auto* cards = new QGridLayout;
    cards->addWidget(metricCard("在线设备", onlineValue_, root), 0, 0);
    cards->addWidget(metricCard("累计消息", messagesValue_, root), 0, 1);
    cards->addWidget(metricCard("接收数据", bytesValue_, root), 0, 2);
    cards->addWidget(metricCard("运行时间", uptimeValue_, root), 0, 3);
    rootLayout->addLayout(cards);

    auto* tabs = new QTabWidget(root);
    auto* devicesPage = new QWidget(tabs);
    auto* devicesLayout = new QVBoxLayout(devicesPage);
    deviceTable_ = new QTableWidget(0, 7, devicesPage);
    deviceTable_->setHorizontalHeaderLabels(
        {"设备 ID", "协议", "状态", "远端地址", "消息数", "接收字节", "最后序号"});
    deviceTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    deviceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    deviceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    deviceTable_->setAlternatingRowColors(true);
    devicesLayout->addWidget(deviceTable_);
    tabs->addTab(devicesPage, "设备");

    auto* eventsPage = new QWidget(tabs);
    auto* eventsLayout = new QVBoxLayout(eventsPage);
    eventList_ = new QListWidget(eventsPage);
    eventsLayout->addWidget(eventList_);
    tabs->addTab(eventsPage, "实时事件");

    auto* commandsPage = new QWidget(tabs);
    auto* commandsLayout = new QVBoxLayout(commandsPage);
    auto* commandRow = new QHBoxLayout;
    deviceSelector_ = new QComboBox(commandsPage);
    commandInput_ = new QLineEdit(commandsPage);
    commandInput_->setPlaceholderText("例如：set_rate=5 或 reboot");
    auto* sendButton = new QPushButton("发送指令", commandsPage);
    connect(sendButton, &QPushButton::clicked, this, [this] { sendCommand(); });
    connect(commandInput_, &QLineEdit::returnPressed, this, [this] { sendCommand(); });
    commandRow->addWidget(new QLabel("设备", commandsPage));
    commandRow->addWidget(deviceSelector_, 1);
    commandRow->addWidget(new QLabel("指令", commandsPage));
    commandRow->addWidget(commandInput_, 3);
    commandRow->addWidget(sendButton);
    commandStatus_ = new QLabel("从在线 TCP 设备中选择目标。", commandsPage);
    commandsLayout->addLayout(commandRow);
    commandsLayout->addWidget(commandStatus_);
    commandsLayout->addStretch();
    tabs->addTab(commandsPage, "指令下发");
    rootLayout->addWidget(tabs, 1);

    setCentralWidget(root);
    statusBar()->showMessage("API: " + baseUrl_.toString());
    setStyleSheet(R"CSS(
        QMainWindow { background: #f4f7fb; }
        #pageTitle { font-size: 24px; font-weight: 700; color: #152238; padding: 8px 0; }
        #connectionState { color: #64748b; padding-right: 10px; }
        #metricCard { background: white; border: 1px solid #e4e9f0; border-radius: 8px; margin: 2px; }
        #metricTitle { color: #64748b; font-size: 13px; }
        #metricValue { color: #152238; font-size: 26px; font-weight: 700; }
        QTabWidget::pane { background: white; border: 1px solid #e4e9f0; }
        QTableWidget, QListWidget { background: white; border: 0; }
        QPushButton { background: #2457f5; color: white; border: 0; border-radius: 5px; padding: 7px 14px; }
        QPushButton:hover { background: #1745ce; }
        QLineEdit, QComboBox { padding: 6px; border: 1px solid #cbd5e1; border-radius: 4px; }
    )CSS");
}

void MainWindow::refresh() {
    fetchMetrics();
    fetchDevices();
    fetchEvents();
}

void MainWindow::fetchMetrics() {
    auto* reply = network_.get(QNetworkRequest(baseUrl_.resolved(QUrl("/api/metrics"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
        if (reply->error() != QNetworkReply::NoError) {
            setConnectionState(false, reply->errorString());
            return;
        }
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        onlineValue_->setText(QString::number(data.value("online_devices").toInteger()));
        messagesValue_->setText(QString::number(data.value("messages_received").toInteger()));
        bytesValue_->setText(formatBytes(data.value("bytes_received").toInteger()));
        const qint64 seconds = data.value("uptime_seconds").toInteger();
        uptimeValue_->setText(QString("%1h %2m %3s").arg(seconds / 3600)
                                  .arg((seconds % 3600) / 60).arg(seconds % 60));
        setConnectionState(true);
    });
}

void MainWindow::fetchDevices() {
    auto* reply = network_.get(QNetworkRequest(baseUrl_.resolved(QUrl("/api/devices"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonArray devices = QJsonDocument::fromJson(reply->readAll()).object().value("devices").toArray();
        const QString selected = deviceSelector_->currentText();
        deviceSelector_->clear();
        deviceTable_->setRowCount(devices.size());
        for (int row = 0; row < devices.size(); ++row) {
            const QJsonObject device = devices[row].toObject();
            const QString id = device.value("id").toString();
            const QString protocol = device.value("protocol").toString();
            const QStringList values = {
                id, protocol, device.value("state").toString(), device.value("peer").toString(),
                QString::number(device.value("messages").toInteger()),
                formatBytes(device.value("bytes").toInteger()),
                QString::number(device.value("last_sequence").toInteger())};
            for (int column = 0; column < values.size(); ++column)
                deviceTable_->setItem(row, column, new QTableWidgetItem(values[column]));
            if (protocol != "udp") deviceSelector_->addItem(id);
        }
        const int selectedIndex = deviceSelector_->findText(selected);
        if (selectedIndex >= 0) deviceSelector_->setCurrentIndex(selectedIndex);
    });
}

void MainWindow::fetchEvents() {
    auto* reply = network_.get(QNetworkRequest(baseUrl_.resolved(QUrl("/api/events"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonArray events = QJsonDocument::fromJson(reply->readAll()).object().value("events").toArray();
        eventList_->clear();
        for (const auto& value : events) {
            const auto event = value.toObject();
            const QDateTime time = QDateTime::fromMSecsSinceEpoch(event.value("timestamp").toInteger());
            eventList_->addItem(QString("%1  %2  %3  %4")
                .arg(time.toString("HH:mm:ss.zzz"), event.value("type").toString(),
                     event.value("device_id").toString(), event.value("detail").toString()));
        }
    });
}

void MainWindow::sendCommand() {
    const QString device = deviceSelector_->currentText();
    const QString command = commandInput_->text().trimmed();
    if (device.isEmpty() || command.isEmpty()) {
        commandStatus_->setText("请选择在线设备并输入指令。");
        return;
    }
    QJsonObject payload{{"device_id", device}, {"command", command}};
    QNetworkRequest request(baseUrl_.resolved(QUrl("/api/command")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = network_.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    commandStatus_->setText("正在发送…");
    connect(reply, &QNetworkReply::finished, this, [this, reply, device] {
        const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
        if (reply->error() == QNetworkReply::NoError) {
            commandStatus_->setText("指令已发送至 " + device);
            commandInput_->clear();
        } else {
            commandStatus_->setText("发送失败：" + reply->errorString());
        }
    });
}

void MainWindow::setConnectionState(bool online, const QString& detail) {
    connectionLabel_->setText(online ? "● 网关在线" : "● 网关离线");
    connectionLabel_->setStyleSheet(online ? "color:#087f5b" : "color:#c92a2a");
    if (!detail.isEmpty()) statusBar()->showMessage(detail, 5000);
}
