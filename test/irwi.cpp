#include "catch.hpp"

#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"
#include "geodb/irwi/tree_internal.hpp"
#include "geodb/utility/temp_dir.hpp"

#include <map>
#include <set>

using namespace geodb;

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
            entry e = c.value(i);
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

template<typename Func>
void tree_test(Func&& f) {
    using internal = tree_internal<8>;
    using external = tree_external<512>;
    constexpr u32 Lambda = 8;

    {
        INFO("internal");
        tree<internal, Lambda> t;
        f(t);
    }
    {
        INFO("external");
        temp_dir dir;
        tree<external, Lambda> t(external(dir.path()));
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
                u.start = point(b1 + (j % 5), b2 + (j % 7), j);
                u.end = u.start + point(1, 1, 1);
                u.label = i / 4;
                t.units.push_back(u);
            }
            trajectories.push_back(std::move(t));
        }

        for (const trajectory& t : trajectories)
            tree.insert(t);

        std::set<std::pair<trajectory_id_type, u32>> seen;
        visit(tree.root(), trajectories, seen);
        REQUIRE(contains_all(trajectories, seen));
    });
}

TEST_CASE("irwi tree query (simple)", "[irwi]") {
    tree_test([](auto&& tree) {
        trajectory t;
        t.id = 123;
        t.units.push_back({point(55, 33, 100), point(66, 44, 105), 1});
        t.units.push_back({point(66, 44, 106), point(62, 48, 115), 2});
        t.units.push_back({point(62, 48, 116), point(62, 48, 130), 1});
        t.units.push_back({point(62, 48, 131), point(55, 33, 140), 3});
        tree.insert(t);

        {
            bounding_box rect(point(0, 0, 105), point(100, 100, 110));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {2}});

            auto result = tree.find(q);
            REQUIRE(result.size() == 1);
            REQUIRE(result[0].id == 123);
            REQUIRE(result[0].units == std::vector<u32>{1});
        }

        {
            bounding_box rect(point(67, 45, 0), point(68, 46, 200));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {2}});

            auto result = tree.find(q);
            REQUIRE(result.size() == 0);
        }

        {
            bounding_box rect(point(0, 0, 0), point(100, 100, 200));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {4}});

            auto result = tree.find(q);
            REQUIRE(result.size() == 0);
        }

        {
            bounding_box rect(point(60, 40, 100), point(100, 100, 110));
            sequenced_query q;
            q.queries.push_back(simple_query{rect, {1}});

            auto result = tree.find(q);
            REQUIRE(result[0].id == 123);
            REQUIRE(result[0].units == std::vector<u32>{0});
        }
    });
}
