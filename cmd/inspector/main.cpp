#include "main_window.hpp"

#include "common/common.hpp"

#include <QtWidgets/QApplication>

int main(int argc, char *argv[]) {
    return tpie_main([&]{
        QApplication app(argc, argv);

        MainWindow mw;
        mw.show();

        return app.exec();
    });
}
