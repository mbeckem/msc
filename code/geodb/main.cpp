#include <iostream>
#include <random>
#include <vector>

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

        tree<external, 40> e(external("xxx"), 1);

        std::mt19937 mt(std::random_device{}());
        std::uniform_real_distribution<float> xdist(0, 400);
        std::uniform_real_distribution<float> ydist(0, 400);
        std::uniform_real_distribution<float> xstep(-2, 2);
        std::uniform_real_distribution<float> ystep(-2, 2);
        std::uniform_int_distribution<time_type> tdist(100, 500);
        std::uniform_int_distribution<time_type> tstep(1, 5);
        std::uniform_int_distribution<label_type> labeldist(1, 50);

        auto get_point = [&](const point& last) {
            return point(last.x() + xstep(mt), last.y() + ystep(mt), last.t() + tstep(mt));
        };

        for (int i = 1; i <= 2500; ++i) {
            trajectory tr;
            tr.id = i;

            point start(xdist(mt), ydist(mt), tdist(mt));
            for (int u = 0; u < 10; ++u) {
                auto stop = get_point(start);
                tr.units.push_back(trajectory_unit{start, stop, labeldist(mt)});
                start = stop;
            }

            e.insert(tr);

            std::cout << i << std::endl;
        }


        dump(std::cout, e.root());
    }


    std::cout << "written: " << tpie::get_bytes_written() << "\n"
              << "read: " << tpie::get_bytes_read() << "\n"
              << std::flush;

    tpie::tpie_finish();
    return 0;
}
