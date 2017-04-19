#include "catch.hpp"

#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"
#include "geodb/irwi/tree_internal.hpp"
#include "geodb/utility/temp_dir.hpp"

#include <map>
#include <random>
#include <set>
#include <type_traits>

using namespace geodb;

/// A spatio-textual trajectory is a list of
/// spatio-textual trajectory units, together with
/// a unique identifier.
struct trajectory {
    trajectory_id_type id;
    std::vector<trajectory_unit> units;
};

struct trajectory_element {
public:
    vector3 spatial;
    label_type textual = 0;

    trajectory_element() {}

    trajectory_element(vector3 spatial, label_type textual)
        : spatial(spatial), textual(textual) {}

private:
    template<typename Dst>
    friend void serialize(Dst& dst, const trajectory_element& e) {
        using tpie::serialize;
        serialize(dst, e.spatial);
        serialize(dst, e.textual);
    }

    template<typename Dst>
    friend void unserialize(Dst& dst, trajectory_element& e) {
        using tpie::unserialize;
        unserialize(dst, e.spatial);
        unserialize(dst, e.textual);
    }
};

struct point_trajectory {
public:
    trajectory_id_type id = 0;
    std::string description;
    std::vector<trajectory_element> entries;

    point_trajectory() {}

    point_trajectory(trajectory_id_type id, std::string description,
                     std::vector<trajectory_element> entries)
        : id(id), description(std::move(description)), entries(std::move(entries))
    {}

private:
    template<typename Dst>
    friend void serialize(Dst& dst, const point_trajectory& p) {
        using tpie::serialize;
        serialize(dst, p.id);
        serialize(dst, p.description);
        serialize(dst, p.entries);
    }

    template<typename Src>
    friend void unserialize(Src& src, point_trajectory& p) {
        using tpie::unserialize;
        unserialize(src, p.id);
        unserialize(src, p.description);
        unserialize(src, p.entries);
    }
};


// validates the tree structure and records seen items.
template<typename Cursor>
void visit(const Cursor& c,
           const std::vector<trajectory>& trajectories,
           std::set<std::pair<trajectory_id_type, u32>>& seen)
{
    if (c.is_leaf()) {
        std::set<trajectory_id_type> leaf_ids;
        std::map<label_type, std::set<trajectory_id_type>> leaf_label_ids;
        for (u32 i = 0; i < c.size(); ++i) {
            tree_entry e = c.value(i);
            bool inserted;
            std::tie(std::ignore, inserted) = seen.insert({e.trajectory_id, e.unit_index});

            if (!inserted)
                throw std::logic_error("unit seen twice!");
            if (trajectories.at(e.trajectory_id).units.at(e.unit_index) != e.unit)
                throw std::logic_error("unit corrupted");

            leaf_ids.insert(e.trajectory_id);
            leaf_label_ids[e.unit.label].insert(e.trajectory_id);
        }

        u32 index_in_parent = c.index();
        Cursor parent = c.parent();
        while (1) {
            // Make sure that the index information for this node exists in all parents.
            auto index = parent.inverted_index();

            auto check_ids = [&](auto list, const auto& id_set) {
                auto iter = list->find(index_in_parent);
                if (iter == list->end())
                    throw std::logic_error("node not found in parent's inverted index");

                auto ids = iter->id_set();
                for (auto id : id_set) {
                    if (!ids.contains(id))
                        throw std::logic_error("id not found in parent's inverted index");
                }
            };

            check_ids(index->total(), leaf_ids);
            for (const auto& pair : leaf_label_ids) {
                auto iter = index->find(pair.first);
                if (iter == index->end())
                    throw std::logic_error("no such label in parent's inverted index");

                check_ids(iter->postings_list(), pair.second);
            }

            if (!parent.has_parent())
                break;
            index_in_parent = parent.index();
            parent.move_parent();
        }
    } else {
        for (u32 i = 0; i < c.size(); ++i) {
            visit(c.child(i), trajectories, seen);
        }
    }
}

template<typename Cursor>
void count_nodes(Cursor& c, size_t& internal, size_t& leaves) {
    if (c.is_leaf()) {
        ++leaves;
    } else {
        ++internal;
        for (u32 i = 0; i < c.size(); ++i) {
            c.move_child(i);
            count_nodes(c, internal, leaves);
            c.move_parent();
        }
    }
}

bool contains_all(const std::vector<trajectory>& trajectories,
                  const std::set<std::pair<trajectory_id_type, u32>>& seen) {
    size_t count = 0;
    for (const trajectory& t : trajectories) {
        for (u32 index = 0; index < t.units.size(); ++index) {
            if (seen.count({t.id, index}) == 0)
                return false;
            ++count;
        }
    }

    return count == seen.size();
}


template<typename Tree>
void insert(Tree& tree, const trajectory& t) {
    // A trajectory is inserted by inserting all its units.
    u32 index = 0;
    for (const trajectory_unit& unit : t.units) {
        tree_entry d{t.id, index, unit};
        tree.insert(d);
        ++index;
    }
}

template<typename Tree>
void insert(Tree& tree, const point_trajectory& t) {
    auto pos = t.entries.begin();
    auto end = t.entries.end();
    u32 index = 0;

    if (pos != end) {
        auto last = pos++;
        for (; pos != end; ++pos) {
            tree_entry e{t.id, index++, trajectory_unit{last->spatial, pos->spatial, last->textual}};
            tree.insert(e);
            last = pos;
        }
    }
}

using internal = tree_internal<8>;
using external = tree_external<512>;
constexpr u32 Lambda = 8;

using internal_tree = tree<internal, Lambda>;
using external_tree = tree<external, Lambda>;

template<typename Func>
void tree_test(Func&& f) {

    {
        INFO("internal");
        internal_tree t;
        f(t);
    }
    {
        INFO("external");
        temp_dir dir;
        external_tree t(external(dir.path()));
        f(t);
    }
}

TEST_CASE("irwi tree insertion", "[irwi]") {
    tree_test([](auto&& tree) {
        std::vector<trajectory> trajectories;
        for (int i = 0; i < 16; ++i) {
            trajectory t;
            t.id = i;

            int b1 = (i % 4) * 10;
            int b2 = (i % 6) * 10;

            for (int j = 0; j < 16; ++j) {
                trajectory_unit u;
                u.start = vector3(b1 + (j % 5), b2 + (j % 7), j);
                u.end = u.start + vector3(1, 1, 1);
                u.label = i / 4;
                t.units.push_back(u);
            }
            trajectories.push_back(std::move(t));
        }

        for (const trajectory& t : trajectories)
            insert(tree, t);

        std::set<std::pair<trajectory_id_type, u32>> seen;
        visit(tree.root(), trajectories, seen);
        REQUIRE(contains_all(trajectories, seen));

        size_t internal_nodes = 0;
        size_t leaf_nodes = 0;
        auto cursor = tree.root();
        count_nodes(cursor, internal_nodes, leaf_nodes);

        REQUIRE(internal_nodes == tree.internal_node_count());
        REQUIRE(leaf_nodes == tree.leaf_node_count());
    });
}

TEST_CASE("irwi tree query (simple)", "[irwi]") {
    tree_test([](auto&& tree) {
        trajectory t;
        t.id = 123;
        t.units.push_back({vector3(55, 33, 100), vector3(66, 44, 105), 1});
        t.units.push_back({vector3(66, 44, 106), vector3(62, 48, 115), 2});
        t.units.push_back({vector3(62, 48, 116), vector3(62, 48, 130), 1});
        t.units.push_back({vector3(62, 48, 131), vector3(55, 33, 140), 3});
        insert(tree, t);

        {
            bounding_box rect(vector3(0, 0, 105), vector3(100, 100, 110));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {2}});

            auto result = tree.find(q);
            REQUIRE(result.size() == 1);
            REQUIRE(result[0].id == 123);
            REQUIRE(result[0].units.size() == 1);
            REQUIRE(result[0].units[0].index == 1);
        }

        {
            bounding_box rect(vector3(67, 45, 0), vector3(68, 46, 200));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {2}});

            auto result = tree.find(q);
            REQUIRE(result.size() == 0);
        }

        {
            bounding_box rect(vector3(0, 0, 0), vector3(100, 100, 200));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {4}});

            auto result = tree.find(q);
            REQUIRE(result.size() == 0);
        }

        {
            bounding_box rect(vector3(60, 40, 100), vector3(100, 100, 110));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {1}});

            auto result = tree.find(q);
            REQUIRE(result[0].id == 123);
            REQUIRE(result[0].units.size() == 1);
            REQUIRE(result[0].units[0].index == 0);
        }
    });
}

TEST_CASE("irwi tree query (complex)", "[irwi]") {
    auto random = [&]() -> double {
        static std::mt19937 engine;   // no seed. this is deterministic.
        static std::uniform_real_distribution<double> dist(0, 1);

        return dist(engine);
    };

    auto random_point = [&](const bounding_box& area) -> vector3 {
        vector3 result;
        result.x() = area.min().x() + random() * area.widths().x();
        result.y() = area.min().y() + random() * area.widths().y();
        result.t() = area.min().t() + random() * area.widths().t();
        return result;
    };

    const bounding_box area1(vector3(0, 0, 0), vector3(50, 50, 50));
    const bounding_box area2(vector3(1000, 1000, 30), vector3(1200, 1100, 300));
    const bounding_box area3(vector3(400, 400, 100), vector3(500, 500, 1100));

    tree_test([&](auto&& tree) {
        // Trajectories 0-9 in area 1.
        for (trajectory_id_type id = 0; id < 10; ++id) {
            // With 50 units each.
            for (u32 unit_index = 100; unit_index < 125; ++unit_index) {
                trajectory_unit unit(random_point(area1), random_point(area1), unit_index % 10);
                tree.insert(tree_entry(id, unit_index, unit));
            }
        }
        tree.insert(tree_entry(5000, 0, trajectory_unit(random_point(area1), random_point(area1), 11)));

        // A whole lot of junk in area 2.
        for (trajectory_id_type id = 9000; id < 9010; ++id) {
            for (u32 unit_index = 123; unit_index < 144; ++unit_index) {
                trajectory_unit unit(random_point(area2), random_point(area2), unit_index % 13);
                tree.insert(tree_entry(id, unit_index, unit));
            }
        }

        // Only few entries in area 3.
        tree.insert(tree_entry(5000, 1, trajectory_unit(random_point(area3), random_point(area3), 1)));
        tree.insert(tree_entry(5000, 2, trajectory_unit(random_point(area3), random_point(area3), 2)));
        tree.insert(tree_entry(5000, 3, trajectory_unit(random_point(area3), random_point(area3), 1)));
        tree.insert(tree_entry(5050, 1, trajectory_unit(random_point(area3), random_point(area3), 3)));

        {
            sequenced_query sq;
            sq.queries.push_back(simple_query{area1, {0}});

            std::vector<trajectory_match> matches = tree.find(sq);
            REQUIRE(matches.size() == 10);

            trajectory_id_type expected = 0;
            for (const trajectory_match& match : matches) {
                if (match.id != expected) {
                    FAIL("expected trajectory " << expected << " but got " << match.id);
                }

                if (match.units.size() != 3) {
                    FAIL("expected three matching units, got " << match.units.size());
                }

                u32 expected_unit = 100;
                for (const unit_match& umatch : match.units) {
                    if (umatch.index != expected_unit) {
                        FAIL("expected unit index " << expected_unit << " but got " << umatch.index);
                    }
                    expected_unit += 10;
                }
                ++expected;
            }
        }

        {
            sequenced_query sq;
            sq.queries.push_back(simple_query{area3, {1, 2}});

            std::vector<trajectory_match> matches = tree.find(sq);

            REQUIRE(matches.size() == 1);

            const trajectory_match& match = matches.front();
            REQUIRE(match.id == 5000);
            REQUIRE(match.units.size() == 3);

            REQUIRE(match.units[0].index == 1);
            REQUIRE(match.units[1].index == 2);
            REQUIRE(match.units[2].index == 3);
        }

        {
            sequenced_query sq;
            sq.queries.push_back(simple_query{area1, {11, 1, 2, 3, 4, 5}});
            sq.queries.push_back(simple_query{area3, {2, 3}});

            std::vector<trajectory_match> matches = tree.find(sq);

            REQUIRE(matches.size() == 1);

            const trajectory_match& match = matches.front();
            REQUIRE(match.id == 5000);
            REQUIRE(match.units.size() == 2);

            // from area1
            REQUIRE(match.units[0].index == 0);

            // from area3
            REQUIRE(match.units[1].index == 2);
        }
    });
}

TEST_CASE("opening resource more than once returns same handle", "[irwi]") {
    tree_test([](auto&& tree) {
        using tree_t = std::decay_t<decltype(tree)>;

        point_trajectory pt;
        pt.id = 123;
        pt.description = "test";
        for (size_t i = 0; i < tree_t::max_leaf_entries() + 2; ++i) {
            pt.entries.push_back(trajectory_element{vector3(1, 2, 3), 123});
        }
        insert(tree, pt);

        REQUIRE(tree.height() == 2);

        auto c = tree.root();
        auto i1 = c.inverted_index();
        auto i2 = c.inverted_index();

        REQUIRE(&*i1 == &*i2);
    });
}
