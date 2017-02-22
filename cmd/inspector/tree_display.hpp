#ifndef TREE_DISPLAY_HPP
#define TREE_DISPLAY_HPP

#include <QWidget>

#include <boost/optional.hpp>

#include "common/common.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <osg/ref_ptr>
#include <osg/Node>

class QTreeWidgetItem;

namespace Ui {
class TreeDisplay;
}

class TreeDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit TreeDisplay(const QString& path, external_tree tree, QWidget *parent = 0);
    ~TreeDisplay();

protected:
    void changeEvent(QEvent *e);

private:
    void refreshNode();
    void refreshChildren();
    void refreshIndex();
    void refreshScene();

    void refreshDirection();
    void refreshUp();

    void internalChildActivated(const QTreeWidgetItem* item, int column);

    void rootActivated();
    void parentActivated();
    void gotoActivated();

    void recurseClicked(bool checked);

    osg::Node* createScene(const external_tree::cursor& node);
    osg::Node* createRecursiveScene(const external_tree::cursor& node);

private:
    Ui::TreeDisplay *ui;
    bool m_recurse = false;
    QString m_path;
    external_tree m_tree;
    boost::optional<external_tree::cursor> m_current;
};

#endif // TREE_DISPLAY_HPP
