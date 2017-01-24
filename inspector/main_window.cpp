#include "main_window.hpp"
#include "ui_main_window.h"

#include "tree_display.hpp"

#include <QtCore/QDir>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openTree);
    connect(ui->actionExit, &QAction::triggered, [&]{ qApp->exit(); });

    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTree);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::openTree() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select a directory containing an irwi tree.");
    if (dir.isEmpty()) {
        return;
    }

    QString basename = QDir(dir).dirName();
    if (basename.isEmpty()) {
        basename = "Unnamed";
    }

    try {
        // TODO: Read only support ?
        tree_type tree(tree_storage(dir.toStdString()));

        int index = ui->tabWidget->addTab(new TreeDisplay(dir, std::move(tree)), basename);
        ui->tabWidget->setTabToolTip(index, dir);
        ui->tabWidget->setCurrentIndex(index);
    } catch (const std::exception& e) {
        QString error;
        error += "Failed to open the tree directory: ";
        error += e.what();
        QMessageBox::critical(this, "Error", error);
    }
}

void MainWindow::closeTree(int index) {
    TreeDisplay* display = qobject_cast<TreeDisplay*>(ui->tabWidget->widget(index));
    ui->tabWidget->removeTab(index);
    delete display;
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}
