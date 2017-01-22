#include "main_window.hpp"
#include "ui_main_window.h"

#include "tree_display.hpp"

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openTree);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::closeTree);
    connect(ui->actionExit, &QAction::triggered, [&]{ qApp->exit(); });
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

    try {
        // TODO: Read only support ?
        tree_type tree(tree_storage(dir.toStdString()));
        if (m_tree) {
            delete m_tree;
        }
        m_tree = new TreeDisplay(dir, std::move(tree), this);
        setCentralWidget(m_tree);
    } catch (const std::exception& e) {
        QString error;
        error += "Failed to open the tree directory: ";
        error += e.what();
        QMessageBox::critical(this, "Error", error);
    }
}

void MainWindow::closeTree() {
    delete m_tree;
    m_tree = nullptr;
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
