#include "geodb/utility/stats_guard.hpp"

#include <iostream>

namespace geodb {

void stats_guard::print_impl(const std::string& str) {
    std::cout << str << std::flush;
}

thread_local int stats_guard::t_indent = 0;

} // namespace geodb
