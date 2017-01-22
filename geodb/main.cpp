#include <iostream>
#include <vector>

#include <gsl/span>

#include "geodb/common.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"
#include "geodb/utility/id_allocator.hpp"
#include "geodb/utility/file_allocator.hpp"

using namespace geodb;

int main(int argc, char** argv) {
    unused(argc, argv);

    tpie::tpie_init();
    tpie::set_block_size(4096);

    {
        using external = tree_external<4096>;

        trajectory tr;
        tr.id = 1;
        tr.units.push_back(trajectory_unit{
            point(3, 4, 0), point(5, 3, 5), label_type(1)
        });
        tr.units.push_back(trajectory_unit{
            point(1, 5, 6), point(0, -5, 9), label_type(0)
        });
        tr.units.push_back(trajectory_unit{
            point(3, -5, 23), point(5, 3, 29), label_type(1)
        });
        tr.units.push_back(trajectory_unit{
            point(7, 3, 29), point(4, 11, 35), label_type(3)
        });

        tree<external, 40> e(external("asd"));

        for (int i = 0; i < 10; ++i) {
            trajectory t = tr;
            t.id += i;
            e.insert(t);

            std::cout << i << "\n";
        }

        dump(std::cout, e.root());

        sequenced_query q;
        q.queries.push_back({{}, {0}});
        e.find(q);
    }


    std::cout << "written: " << tpie::get_bytes_written() << "\n"
              << "read: " << tpie::get_bytes_read() << "\n"
              << std::flush;

    tpie::tpie_finish();
    return 0;
}
