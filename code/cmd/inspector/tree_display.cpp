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

#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTableWidgetItem>

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

TreeDisplay::TreeDisplay(const QString& path, external_tree tree, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TreeDisplay)
    , m_path(path)
    , m_tree(std::move(tree))
{
    ui->setupUi(this);
    ui->treeTabs->setCurrentWidget(ui->childrenTab);
    ui->leafChildrenTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->internalChildrenTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->indexTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->summaryTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    ui->pathText->setText(m_path);
    ui->sizeText->setText(QString::number(m_tree.size()));
    ui->heightText->setText(QString::number(m_tree.height()));
    ui->fanoutText->setText(QString("%1 / %2")
                            .arg(m_tree.max_internal_entries())
                            .arg(m_tree.max_leaf_entries()));
    ui->nodesText->setText(QString("%1 / %2")
                            .arg(m_tree.internal_node_count())
                            .arg(m_tree.leaf_node_count()));

    connect(ui->internalChildrenTree, &QTreeWidget::itemActivated, this, &TreeDisplay::internalChildActivated);
    connect(ui->rootButton, &QPushButton::clicked, this, &TreeDisplay::rootActivated);
    connect(ui->parentButton, &QPushButton::clicked, this, &TreeDisplay::parentActivated);
    connect(ui->gotoButton, &QPushButton::clicked, this, &TreeDisplay::gotoActivated);

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

static void visitNode(const external_tree::cursor& node, QTreeWidgetItem* parent, int depth, int maxdepth) {
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
            external_tree::value_type value = node.value(i);
            QString label = QString("%1 (trajectory: %2, unit: %3)")
                    .arg(i)
                    .arg(value.trajectory_id)
                    .arg(value.unit_index);
            new QTreeWidgetItem(parent, QStringList(label));
        }
    }
}

static QString get_path(external_tree::cursor node) {
    QString path;
    while (1) {
        QString id = QString::number(node.id());
        if (!path.isEmpty())
            id += "/";
        path = id + path;

        if (!node.has_parent())
            break;

        node.move_parent();
    }
    return path;
}

static QString get_type(const external_tree::cursor& node) {
    if (node.is_leaf()) {
        return "Leaf";
    }
    if (node.is_root()) {
        return "Internal (root)";
    }
    return "Internal";
}

static QString get_mbb(const external_tree::cursor& node) {
    std::stringstream ss;
    ss << node.mbb();
    return QString::fromStdString(ss.str());
}

void TreeDisplay::refreshNode() {
    if (m_current) {
        ui->nodeIdText->setText(QString::number(m_current->id()));
        ui->nodeTypeText->setText(get_type(*m_current));
        ui->nodePathText->setText(get_path(*m_current));
        ui->nodeMbbText->setText(get_mbb(*m_current));
        ui->nodeEntriesText->setText(QString("%1/%2")
                                     .arg(m_current->size())
                                     .arg(m_current->max_size()));

        ui->parentButton->setEnabled(m_current->has_parent());
        ui->rootButton->setEnabled(!m_current->is_root());
        ui->gotoButton->setEnabled(true);
    } else {
        ui->nodeIdText->setText("N/A");
        ui->nodeTypeText->setText("N/A");
        ui->nodePathText->setText("N/A");
        ui->nodeMbbText->setText("N/A");
        ui->nodeEntriesText->setText("N/A");

        ui->parentButton->setEnabled(false);
        ui->rootButton->setEnabled(false);
        ui->gotoButton->setEnabled(false);
    }

    refreshChildren();
    refreshIndex();
    refreshSummary();
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
        for (const external_tree::posting_type&  p : items) {
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

void TreeDisplay::refreshSummary() {
    QTreeWidget* tree = ui->summaryTree;
    tree->clear();
    tree->setSortingEnabled(false);

    if (!m_current) {
        return;
    }

    auto insert_summary = [&](auto&& label, auto&& summary) {
        auto* item = new QTreeWidgetItem;
        item->setData(0, Qt::DisplayRole, label);
        item->setData(1, Qt::DisplayRole, qint64(summary.count));
        item->setData(2, Qt::DisplayRole, to_string(summary.trajectories));
        tree->addTopLevelItem(item);
    };

    if (m_current->is_internal()) {
        auto index = m_current->inverted_index();

        insert_summary(QString("Total"), index->total()->summarize());
        for (const auto& entry : *index) {
            const auto label = entry.label();
            const auto summary = entry.postings_list()->summarize();
            insert_summary(qint64(label), summary);
        }
    } else {
        struct item {
            std::set<geodb::trajectory_id_type> trajectories;
            u64 count = 0;
        };

        u64 total_count = 0;
        std::set<geodb::trajectory_id_type> total_trajectories;
        std::map<geodb::label_type, item> items;
        for (size_t i = 0; i < m_current->size(); ++i) {
            const geodb::tree_entry entry = m_current->value(i);

            auto& item = items[entry.unit.label];
            ++item.count;
            item.trajectories.insert(entry.trajectory_id);

            ++total_count;
            total_trajectories.insert(entry.trajectory_id);
        }

        geodb::postings_list_summary<external_tree::lambda()> total_summary;
        total_summary.count = total_count;
        total_summary.trajectories.assign(total_trajectories.begin(), total_trajectories.end());
        insert_summary(QString("Total"), total_summary);
        for (const auto& pair : items) {
            geodb::postings_list_summary<external_tree::lambda()> summary;
            summary.count = pair.second.count;
            summary.trajectories.assign(pair.second.trajectories.begin(),
                                        pair.second.trajectories.end());

            insert_summary(pair.first, summary);
        }
    }

    tree->sortByColumn(1, Qt::DescendingOrder);
    tree->setSortingEnabled(true);
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

void TreeDisplay::rootActivated() {
    if (!m_current) {
        return;
    }
    m_current->move_root();
    refreshNode();
}

void TreeDisplay::parentActivated() {
    if (!m_current || !m_current->has_parent()) {
        return;
    }
    m_current->move_parent();
    refreshNode();
}

void TreeDisplay::gotoActivated() {
    if (!m_current) {
        return;
    }

    QString pathText = QInputDialog::getText(this, "Go to node",
                                             "Enter the node path (node IDs separated by \"/\"):");
    if (pathText.isNull()) {
        return;
    }

    QStringList path = pathText.split("/", QString::SkipEmptyParts);
    if (path.empty()) {
        m_current->move_root();
        refreshNode();
        return;
    }

    using node_id = external_tree::cursor::node_id;

    try {
        // Parses the node id from the text or throws QString.
        auto get_id = [](const QString& idText) {
            bool ok = false;
            node_id result = idText.toULongLong(&ok);
            if (!ok) {
                throw QString("Invalid node id: %1.").arg(idText);
            }
            return result;
        };

        // Returns the index of `child` in `c` or throws a QString.
        auto index_of = [](const external_tree::cursor& c, node_id child) {
            geodb_assert(c.is_internal(), "Must be an internal node");
            const size_t size = c.size();
            for (size_t i = 0; i < size; ++i) {
                if (c.child_id(i) == child)
                    return i;
            }
            throw QString("%1 is not a child of %2.").arg(child).arg(c.id());
        };

        external_tree::cursor c = m_current->root();
        if (get_id(path.front()) != c.id()) {
            throw QString("First node id must point to the root.");
        }
        path.pop_front();

        for (const QString& idText : path) {
            node_id childId = get_id(idText);
            if (!c.is_internal()) {
                throw QString("Cannot navigate to %1 because %2 is already a leaf.")
                        .arg(childId)
                        .arg(c.id());
            }
            size_t index = index_of(c, childId);
            c.move_child(index);
        }

        m_current = c;
        refreshNode();
    } catch (QString error) {
        QMessageBox::critical(this, "Error", error);
    }
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
    geodb::vector3 center = mbb.center();
    geodb::vector3 widths = mbb.max() - mbb.min();

    osg::Matrix m1 = osg::Matrix::scale(widths.x(), widths.y(), widths.t());
    osg::Matrix m2 = osg::Matrix::translate(center.x(), center.y(), center.t());
    osg::MatrixTransform* tx = new osg::MatrixTransform(m1 * m2 * global);
    tx->addChild(make_box(c));
    return tx;
}

static osg::Matrix global_transform(const geodb::bounding_box& root) {
    // Rescale everything to [0, 1000].
    geodb::vector3 width = root.max() - root.min();
    return osg::Matrix::scale(1000.0 / double(width.x()),
                              1000.0 / double(width.y()),
                              1000.0 / double(width.t()));
}

osg::Node* TreeDisplay::createScene(const external_tree::cursor& node) {
    osg::ref_ptr<osg::Group> group = new osg::Group;
    std::vector<QColor> colors = get_colors(node.id(), node.size());

    osg::Matrix global = global_transform(node.mbb());
    for (size_t i = 0; i < node.size(); ++i) {
        group->addChild(make_box(node.mbb(i), global, colors[i]));
    }
    return group.release();
}

static void count_nodes(external_tree::cursor& node, size_t& count) {
    count += 1;
    if (node.is_internal()) {
        for (size_t i = 0; i < node.size(); ++i) {
            node.move_child(i);
            count_nodes(node, count);
            node.move_parent();
        }
    }
}

static size_t count_nodes(const external_tree::cursor& node) {
    size_t count = 0;
    external_tree::cursor copy = node;
    count_nodes(copy, count);
    return count;
}

static osg::Node* make_tree(external_tree::cursor& node, const osg::Matrix& global, const std::vector<QColor> colors, size_t& index) {
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

osg::Node* TreeDisplay::createRecursiveScene(const external_tree::cursor& node) {
    std::vector<QColor> colors = get_colors(node.id(), count_nodes(node));
    external_tree::cursor copy = node;
    size_t index = 0;
    return make_tree(copy, global_transform(node.mbb()), colors, index);
}
