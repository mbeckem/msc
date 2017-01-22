#ifndef TREE_DISPLAY_HPP
#define TREE_DISPLAY_HPP

#include <QWidget>

#include <boost/optional.hpp>

#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <osg/ref_ptr>
#include <osg/Node>

namespace Ui {
class TreeDisplay;
}

using tree_storage = geodb::tree_external<4096>;
using tree_type = geodb::tree<tree_storage, 40>;
using tree_cursor = typename tree_type::cursor;

class TreeDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit TreeDisplay(const QString& path, tree_type tree, QWidget *parent = 0);
    ~TreeDisplay();

protected:
    void changeEvent(QEvent *e);

private:
    void refreshNode();
    void refreshChildren();
    void refreshIndex();
    void refreshScene();

    osg::Node* createScene(const tree_cursor& node, bool recurse);

private:
    Ui::TreeDisplay *ui;
    QString m_path;
    tree_type m_tree;
    boost::optional<tree_cursor> m_current;
};

#endif // TREE_DISPLAY_HPP
