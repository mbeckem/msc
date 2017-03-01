#ifndef COMMON_COMMON_HPP
#define COMMON_COMMON_HPP

#include "geodb/common.hpp"
#include "geodb/irwi/tree.hpp"
#include "geodb/irwi/tree_external.hpp"

#include <tpie/tpie.h>
#include <fmt/ostream.h>
#include <gsl/gsl_util>
#include <json.hpp>

#include <chrono>
#include <iostream>

using nlohmann::json;

using geodb::u32;
using geodb::u64;

using geodb::i32;
using geodb::i64;

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

struct measure_t {
    u64 reads = 0;        // Block reads
    u64 writes = 0;       // Block writes
    u64 duration = 0;     // Time taken (seconds)
    u64 block_size = 0;   // Block size in bytes.
};

inline void to_json(json& j, const measure_t& m) {
    j = json::object();
    j["reads"] = m.reads;
    j["writes"] = m.writes;
    j["duration"] = m.duration;
    j["block_size"] = m.block_size;
}

/// Calls the given function and measures time taken
/// and IOs performed.
template<typename Func>
measure_t measure_call(Func&& f) {
    using namespace std::chrono;

    u64 bytes_read = tpie::get_bytes_read();
    u64 bytes_written = tpie::get_bytes_written();
    auto start = steady_clock::now();

    f();

    measure_t m;
    m.reads = (tpie::get_bytes_read() - bytes_read) / block_size;
    m.writes = (tpie::get_bytes_written() - bytes_written) / block_size;
    m.duration = duration_cast<seconds>(steady_clock::now() - start).count();
    m.block_size = block_size;
    return m;
}

/// Writes the given json object into the given file.
inline void write_json(const std::string& file, const json& output) {
    std::fstream f;
    f.open(file, std::fstream::out |  std::fstream::trunc);
    if (!f) {
        throw std::runtime_error("Failed to open file");
    }

    f << output.dump(4) << std::endl;
}

#endif // COMMON_COMMON_HPP
