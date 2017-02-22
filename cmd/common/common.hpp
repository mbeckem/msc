#ifndef COMMON_COMMON_HPP
#define COMMON_COMMON_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <tpie/tpie.h>
#include <fmt/ostream.h>
#include <gsl/gsl_util>

#include <iostream>

class exit_main {
public:
    int code = 0;

    exit_main(int code = 0): code(code) {}
};

constexpr const size_t block_size = 4096;
constexpr const size_t lambda = 40;

using external_storage = geodb::tree_external<block_size>;
using external_tree = geodb::tree<external_storage, lambda>;

/// Initializes the tpie library, calls the function f and deinitializes tpie.
/// Returns the value returned by `f`, which should be an int.
template<typename Func>
int tpie_main(Func&& f) {
    tpie::tpie_init();
    tpie::set_block_size(block_size);
    auto cleanup = gsl::finally([]{ tpie::tpie_finish(); });

    try {
        return f();
    } catch (const exit_main& e) {
        return e.code;
    }
}

#endif // COMMON_COMMON_HPP
