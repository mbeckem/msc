#ifndef MAIN_WINDOW_HPP
#define MAIN_WINDOW_HPP

#include <QMainWindow>

class SceneRenderer;
class TreeDisplay;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    void changeEvent(QEvent *e);

private:
    void openTree();
    void closeTree(int index);
private:
    Ui::MainWindow *ui = nullptr;
};

#endif // MAIN_WINDOW_HPP
