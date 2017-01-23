#include "tree_display.hpp"
#include "ui_tree_display.h"

#include <boost/numeric/conversion/cast.hpp>

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

    ui->pathText->setText(m_path);
    ui->sizeText->setText(QString::number(m_tree.size()));
    ui->heightText->setText(QString::number(m_tree.height()));
    ui->fanoutText->setText(QString("%1 / %2")
                            .arg(m_tree.max_internal_entries())
                            .arg(m_tree.max_leaf_entries()));

    if (!m_tree.empty()) {
        m_current = m_tree.root();
    }
    refreshNode();

    connect(ui->sceneRenderer, &SceneRenderer::eyeChanged, this, &TreeDisplay::refreshDirection);
    connect(ui->sceneRenderer, &SceneRenderer::centerChanged, this, &TreeDisplay::refreshDirection);
    connect(ui->sceneRenderer, &SceneRenderer::upChanged, this, &TreeDisplay::refreshUp);
    refreshDirection();
    refreshUp();
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
    } else {
        ui->nodeText->setText("None");
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
            QString box = to_string(m_current->mmb(i));
            QString ptr = QString::number(m_current->child_id(i));

            new QTreeWidgetItem(internal->invisibleRootItem(), QStringList{index, box, ptr});
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
        append(QString("Label %1").arg(item.label()), item.postings_list());
    }
}

void TreeDisplay::refreshScene() {
    if (!m_current) {
        ui->sceneRenderer->setSceneData(nullptr);
        return;
    }

    osg::ref_ptr<osg::Node> scene = createScene(*m_current, false);
    osg::StateSet* set = scene->getOrCreateStateSet();

    set->setAttribute(new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE));
    set->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

    osg::Material* material = new osg::Material;
    material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
    set->setAttributeAndModes(material, osg::StateAttribute::ON);

    ui->sceneRenderer->setSceneData(scene);
}

void TreeDisplay::refreshDirection() {
    auto dir = ui->sceneRenderer->center() - ui->sceneRenderer->eye();
    ui->directionText->setText(to_string(dir));
}

void TreeDisplay::refreshUp() {
    ui->upText->setText(to_string(ui->sceneRenderer->up()));
}

osg::Node* TreeDisplay::createScene(const tree_type::cursor& node, bool recurse) {
    (void) recurse; // TODO

    osg::ref_ptr<osg::Group> group = new osg::Group;
    std::vector<QColor> colors = get_colors(node.id(), node.size());

    for (size_t i = 0; i < node.size(); ++i) {
        geodb::bounding_box mmb = node.mmb(i);
        geodb::point center = mmb.center();
        geodb::point widths = mmb.max() - mmb.min();

        osg::Matrix m1 = osg::Matrix::scale(widths.x(), widths.y(), widths.t());
        osg::Matrix m2 = osg::Matrix::translate(center.x(), center.y(), center.t());
        osg::MatrixTransform* tx = new osg::MatrixTransform(m1 * m2);
        tx->addChild(make_box(colors[i]));

        group->addChild(tx);
    }
    return group.release();
}
