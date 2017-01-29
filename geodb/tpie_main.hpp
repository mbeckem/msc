#ifndef TPIE_MAIN_HPP
#define TPIE_MAIN_HPP

#include <tpie/tpie.h>
#include <fmt/ostream.h>
#include <gsl/gsl_util>

#include <iostream>

namespace geodb {

static constexpr size_t block_size = 4096;

class exit_main {
public:
    int code = 0;

    exit_main(int code = 0): code(code) {}
};

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

} // namespace geodb

#endif // TPIE_MAIN_HPP
