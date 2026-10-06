#include "mainwindow.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QUrl>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("IndustrialLink Monitor");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Qt monitor client for the IndustrialLink gateway");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption urlOption({"u", "url"}, "Gateway base URL", "url", "http://127.0.0.1:8080");
    parser.addOption(urlOption);
    parser.process(application);
    MainWindow window(QUrl(parser.value(urlOption)));
    window.show();
    return application.exec();
}

