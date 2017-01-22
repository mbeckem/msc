#include "main_window.hpp"

#include <QtWidgets/QApplication>

#include <tpie/tpie.h>

int main(int argc, char *argv[]) {
    tpie::tpie_init();
    tpie::set_block_size(4096);

    int ret;
    {
        QApplication app(argc, argv);

        MainWindow mw;
        mw.show();

        ret = app.exec();
    }
    tpie::tpie_finish();
    return ret;
}
