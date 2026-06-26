#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QApplication::setApplicationName("BOUN Quota Tracker");
    QApplication::setOrganizationName("boun-quota-tracker");

    MainWindow window;
    window.show();

    return app.exec();
}
