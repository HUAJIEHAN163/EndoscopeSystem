#include <QApplication>
#include "ui/mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("EndoscopeSystem");

    MainWindow window;
    window.show();

    return app.exec();
}
