#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QUrl>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QUrl baseUrl, QWidget* parent = nullptr);

private:
    void buildUi();
    void refresh();
    void fetchMetrics();
    void fetchDevices();
    void fetchEvents();
    void sendCommand();
    void setConnectionState(bool online, const QString& detail = {});

    QUrl baseUrl_;
    QNetworkAccessManager network_;
    QTimer* timer_{};
    QLabel* connectionLabel_{};
    QLabel* onlineValue_{};
    QLabel* messagesValue_{};
    QLabel* bytesValue_{};
    QLabel* uptimeValue_{};
    QTableWidget* deviceTable_{};
    QListWidget* eventList_{};
    QComboBox* deviceSelector_{};
    QLineEdit* commandInput_{};
    QLabel* commandStatus_{};
};

