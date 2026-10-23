#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("TorchOfBuilding");
    MainWindow w;
    w.resize(1280, 800);
    w.show();
    return QApplication::exec();
}
