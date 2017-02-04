#include "tree_display.hpp"
#include "ui_tree_display.h"

#include <boost/numeric/conversion/cast.hpp>

#include <osg/ComputeBoundsVisitor>
#include <osg/Geode>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osg/PolygonMode>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osg/io_utils>

#include <sstream>

static osg::Geode* make_box(const QColor& c) {
    auto box = new osg::Box();

    auto draw = new osg::ShapeDrawable(box);
    draw->setColor(osg::Vec4(c.redF(), c.greenF(), c.blueF(), c.alphaF()));

    auto geode = new osg::Geode();
    geode->addDrawable(draw);

    return geode;
}

static std::vector<QColor> get_colors(double seed, int count) {
    static constexpr double golden_ratio = 0.618033988749895;

    std::vector<QColor> result;
    double hue = seed * golden_ratio;
    for (int i = 0; i < count; i++) {
        hue = std::fmod(hue + golden_ratio, 1.0);
        result.push_back(QColor::fromHslF(hue, 1.0, 0.5));
    }
    return result;
}

template<typename T>
QString to_string(const T& t) {
    std::stringstream ss;
    ss << t;
    return QString::fromStdString(ss.str());
}

TreeDisplay::TreeDisplay(const QString& path, tree_type tree, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TreeDisplay)
    , m_path(path)
    , m_tree(std::move(tree))
{
    ui->setupUi(this);
    ui->leafChildrenTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->internalChildrenTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->indexTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    ui->pathText->setText(m_path);
    ui->sizeText->setText(QString::number(m_tree.size()));
    ui->heightText->setText(QString::number(m_tree.height()));
    ui->fanoutText->setText(QString("%1 / %2")
                            .arg(m_tree.max_internal_entries())
                            .arg(m_tree.max_leaf_entries()));
    connect(ui->internalChildrenTree, &QTreeWidget::itemActivated, this, &TreeDisplay::internalChildActivated);
    connect(ui->parentButton, &QPushButton::clicked, this, &TreeDisplay::parentActivated);

    connect(ui->sceneRenderer, &SceneRenderer::eyeChanged, this, &TreeDisplay::refreshDirection);
    connect(ui->sceneRenderer, &SceneRenderer::centerChanged, this, &TreeDisplay::refreshDirection);
    connect(ui->sceneRenderer, &SceneRenderer::upChanged, this, &TreeDisplay::refreshUp);
    connect(ui->resetButton, &QPushButton::clicked, ui->sceneRenderer, &SceneRenderer::reset);
    connect(ui->recurseBox, &QCheckBox::clicked, this, &TreeDisplay::recurseClicked);
    refreshDirection();
    refreshUp();

    // Initialize to root (if possible).
    if (!m_tree.empty()) {
        m_current = m_tree.root();
    }
    refreshNode();
}

TreeDisplay::~TreeDisplay()
{
    delete ui;
}

void TreeDisplay::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

static void visitNode(const tree_type::cursor& node, QTreeWidgetItem* parent, int depth, int maxdepth) {
    if (node.is_internal()) {
        for (size_t i = 0; i < node.size(); ++i) {
            QString label = QString::number(i) + " (id: " + QString::number(node.child_id(i)) + ")";
            QTreeWidgetItem* item = new QTreeWidgetItem(parent, QStringList(label));
            if (depth < maxdepth) {
                visitNode(node.child(i), item, depth + 1, maxdepth);
            }
        }
    } else {
        for (size_t i = 0; i < node.size(); ++i) {
            tree_type::value_type value = node.value(i);
            QString label = QString("%1 (trajectory: %2, unit: %3)")
                    .arg(i)
                    .arg(value.trajectory_id)
                    .arg(value.unit_index);
            new QTreeWidgetItem(parent, QStringList(label));
        }
    }
}

void TreeDisplay::refreshNode() {
    if (m_current) {
        QString type = m_current->is_internal() ? "internal" : "leaf";
        QString label = QString("id: %1, type: %2").arg(m_current->id()).arg(type);
        if (!m_current->has_parent()) {
            label += " (root)";
        }
        ui->nodeText->setText(label);
        ui->parentButton->setEnabled(m_current->has_parent());
    } else {
        ui->nodeText->setText("None");
        ui->parentButton->setEnabled(false);
    }

    refreshChildren();
    refreshIndex();
    refreshScene();
}

void TreeDisplay::refreshChildren() {
    QTreeWidget* internal = ui->internalChildrenTree;
    QTreeWidget* leaf = ui->leafChildrenTree;
    internal->clear();
    leaf->clear();

    if (!m_current) {
        ui->stackedWidget->setCurrentWidget(ui->internalPage);
        return;
    }

    if (m_current->is_internal()) {
        for (size_t i = 0; i < m_current->size(); ++i) {
            QString index = QString::number(i);
            QString box = to_string(m_current->mbb(i));
            QString ptr = QString::number(m_current->child_id(i));

            QTreeWidgetItem* item = new QTreeWidgetItem(internal->invisibleRootItem(), QStringList{index, box, ptr});
            item->setData(0, Qt::UserRole, qint64(i));
        }
        ui->stackedWidget->setCurrentWidget(ui->internalPage);
    } else {
        for (size_t i = 0; i < m_current->size(); ++i) {
            auto data = m_current->value(i);

            QString index = QString::number(i);
            QString id = QString("%1, %2").arg(data.trajectory_id).arg(data.unit_index);
            QString start = to_string(data.unit.start);
            QString end = to_string(data.unit.end);
            QString label = QString::number(data.unit.label);
            new QTreeWidgetItem(leaf->invisibleRootItem(), QStringList{index, id, start, end, label});
        }
        ui->stackedWidget->setCurrentWidget(ui->leafPage);
    }
}

void TreeDisplay::refreshIndex() {
    QTreeWidget* tree = ui->indexTree;
    tree->clear();

    if (!m_current || m_current->is_leaf()) {
        return;
    }

    auto append = [&](const QString& title, auto list) {
        QTreeWidgetItem* parent = new QTreeWidgetItem(QStringList(title));

        auto items = list->all();
        std::sort(items.begin(), items.end(), [&](auto&& a, auto&& b) { return a.node() < b.node(); });
        for (const tree_type::posting_type&  p : items) {
            QString index = QString::number(p.node());
            QString count = QString::number(p.count());
            QString trajectories = to_string(p.id_set());
            QTreeWidgetItem* item = new QTreeWidgetItem(QStringList{index, count, trajectories});
            parent->addChild(item);
        }

        tree->addTopLevelItem(parent);
    };

    auto index = m_current->inverted_index();
    append("Total", index->total());
    for (auto item : *index) {
        auto list = item.postings_list();
        if (!list->empty()) {
            append(QString("Label %1").arg(item.label()), list);
        }
    }
}

void TreeDisplay::refreshScene() {
    if (!m_current) {
        ui->sceneRenderer->setSceneData(nullptr);
        return;
    }

    osg::ref_ptr<osg::Node> scene = m_recurse ? createRecursiveScene(*m_current)
                                              : createScene(*m_current);
    osg::StateSet* set = scene->getOrCreateStateSet();

    set->setAttribute(new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE));
    set->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

    osg::Material* material = new osg::Material;
    material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
    set->setAttributeAndModes(material, osg::StateAttribute::ON);

    ui->sceneRenderer->setSceneData(scene);
}

void TreeDisplay::internalChildActivated(const QTreeWidgetItem* item, int column) {
    if (!item || column < 0) {
        return;
    }

    // Tree item stores child index at [0, UserRole]
    QVariant data = item->data(0, Qt::UserRole);
    assert(data.isValid());
    assert(data.canConvert<qint64>());

    size_t child = data.value<qint64>();
    assert(m_current);
    assert(child < m_current->size());
    m_current->move_child(child);
    refreshNode();
}

void TreeDisplay::parentActivated() {
    if (!m_current || !m_current->has_parent()) {
        return;
    }
    m_current->move_parent();
    refreshNode();
}

void TreeDisplay::recurseClicked(bool checked) {
    if (checked == m_recurse) {
        return;
    }

    m_recurse = checked;
    refreshScene();
}

void TreeDisplay::refreshDirection() {
    auto dir = ui->sceneRenderer->center() - ui->sceneRenderer->eye();
    ui->directionText->setText(to_string(dir));
}

void TreeDisplay::refreshUp() {
    ui->upText->setText(to_string(ui->sceneRenderer->up()));
}

static osg::Node* make_box(const geodb::bounding_box& mbb, const osg::Matrix& global, const QColor& c) {
    geodb::point center = mbb.center();
    geodb::point widths = mbb.max() - mbb.min();

    osg::Matrix m1 = osg::Matrix::scale(widths.x(), widths.y(), widths.t());
    osg::Matrix m2 = osg::Matrix::translate(center.x(), center.y(), center.t());
    osg::MatrixTransform* tx = new osg::MatrixTransform(m1 * m2 * global);
    tx->addChild(make_box(c));
    return tx;
}

static osg::Matrix global_transform(const geodb::bounding_box& root) {
    // Rescale everything to [0, 1000].
    geodb::point width = root.max() - root.min();
    return osg::Matrix::scale(1000.0 / double(width.x()),
                              1000.0 / double(width.y()),
                              1000.0 / double(width.t()));
}

osg::Node* TreeDisplay::createScene(const tree_type::cursor& node) {
    osg::ref_ptr<osg::Group> group = new osg::Group;
    std::vector<QColor> colors = get_colors(node.id(), node.size());

    osg::Matrix global = global_transform(node.mbb());
    for (size_t i = 0; i < node.size(); ++i) {
        group->addChild(make_box(node.mbb(i), global, colors[i]));
    }
    return group.release();
}

static void count_nodes(tree_type::cursor& node, size_t& count) {
    count += 1;
    if (node.is_internal()) {
        for (size_t i = 0; i < node.size(); ++i) {
            node.move_child(i);
            count_nodes(node, count);
            node.move_parent();
        }
    }
}

static size_t count_nodes(const tree_cursor& node) {
    size_t count = 0;
    tree_cursor copy = node;
    count_nodes(copy, count);
    return count;
}

static osg::Node* make_tree(tree_cursor& node, const osg::Matrix& global, const std::vector<QColor> colors, size_t& index) {
    QColor color = colors[index++];
    osg::ref_ptr<osg::Group> group = new osg::Group;

    if (node.is_internal()) {
        for (size_t i = 0; i < node.size(); ++i) {
            group->addChild(make_box(node.mbb(i), global, color));

            node.move_child(i);
            group->addChild(make_tree(node, global, colors, index));
            node.move_parent();
        }
    } else {
        for (size_t i = 0; i < node.size(); ++i) {
            group->addChild(make_box(node.mbb(i), global, color));
        }
    }
    return group.release();
}

osg::Node* TreeDisplay::createRecursiveScene(const tree_cursor& node) {
    std::vector<QColor> colors = get_colors(node.id(), count_nodes(node));
    tree_cursor copy = node;
    size_t index = 0;
    return make_tree(copy, global_transform(node.mbb()), colors, index);
}
